/**
 * ai_processor.c — 电子元器件 AI 识别处理模块
 *
 * 数据流:
 *   push_frame() → [ring queue (leaky)] → process_thread()
 *   → cv::Mat MJPEG解码 → RGA resize → RKNN NPU YOLOv8推理
 *   → NMS → EMA框平滑 → 元器件计数 → 画框 → JPEG编码 → 回调
 *
 * 队列策略: leaky ring buffer
 *   - 写入: 队列满时覆盖最旧的帧 (总是不阻塞)
 *   - 读取: 取最新帧处理
 *   保证: 采集线程永远不会被 AI 处理阻塞, AI 始终处理最新帧
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include "rknn_infer.h"
#include "ai_processor.h"

/* ============================================================
 *  元器件计数引擎
 *
 *  每帧检测结果经过 EMA 平滑, 消除单帧抖动.
 *  文字检测仅关注图像上方 TEXT_ROI_H 比例区域.
 *  连续多帧一致才视为"稳定计数".
 * ============================================================ */
#define COUNT_WINDOW     30    /* 稳定窗口帧数 */
#define TEXT_ROI_H       0.25f /* 文字检测 ROI: 图像上方 25% */
#define EMA_ALPHA        0.3f  /* EMA 平滑系数 */
#define TEXT_CONF_MIN    0.25f /* 文字检测也放宽 */
#define DET_CONF_MIN     0.25f /* 与缺损类最低阈值对齐 */

/* 类别索引 (匹配模型输出: 0=Capacitor 1=Diode 2=Transistor 3=Resister 4=LED
 *                       5=C-dam 6=R-dam 7=text_R 8=text_C 9=text_D 10=D-dam) */
enum {
    CLS_CAPACITOR = 0,
    CLS_DIODE     = 1,
    CLS_TRANSISTOR= 2,
    CLS_RESISTOR  = 3,
    CLS_LED       = 4,
    CLS_C_DAMAGED = 5,
    CLS_R_DAMAGED = 6,
    CLS_TEXT_R    = 7,
    CLS_TEXT_C    = 8,
    CLS_TEXT_D    = 9,
    CLS_D_DAMAGED = 10,
    CLS_POT        = 11,
    CLS_CONNECTER  = 12,
    CLS_XTAL       = 13,
    CLS_IC         = 14,
    CLS_COUNT     = 15
};

/* 显示用名称: 按模型ID索引 */
static const char *g_class_names[CLS_COUNT] = {
    "C","D","T","R","LED","C-dam","R-dam","txtR","txtC","txtD","D-dam","Pot","Con","Xtal","IC"
};

/* 判断是否缺损类 (Cap-dam=5, Res-dam=6, Diode-dam=10) */
static inline int is_damaged_cls(int cid) {
    return cid == CLS_C_DAMAGED || cid == CLS_R_DAMAGED || cid == CLS_D_DAMAGED;
}

typedef struct {
    float   ema_counts[CLS_COUNT];   /* 每类 EMA 平滑计数 */
    int     stable_counts[CLS_COUNT];/* 稳定后的整数计数 */
    int     text_filter;             /* -1=无, 0=电阻, 1=电容, 2=二极管 */
    int     has_damaged;
    int     has_unknown;
    int     frame_cnt;               /* 累计处理帧数 */
    pthread_mutex_t lock;
} component_counter_t;

static component_counter_t g_cc = {
    .text_filter = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/* ---- 文字检测: 在图像上方 TEXT_ROI_H 区域内查找 text_* 类 ---- */
static int detect_text_filter(const rknn_detection_t *dets, int n, int img_h)
{
    (void)img_h;
    int   votes[3] = {0};  /* text_R, text_C, text_D */

    for (int i = 0; i < n; i++) {
        int cid = dets[i].class_id;
        if (cid < CLS_TEXT_R || cid > CLS_TEXT_D) continue;
        if (dets[i].confidence < TEXT_CONF_MIN) continue;
        votes[cid - CLS_TEXT_R]++;
    }

    /* 返回票数最多的文字类型, 需 ≥1 票 */
    int best = -1, best_v = 0;
    for (int i = 0; i < 3; i++) {
        if (votes[i] > best_v) { best_v = votes[i]; best = i; }
    }
    return best;  /* -1=无, 0=电阻, 1=电容, 2=二极管 */
}

/* ---- 每帧更新 EMA 计数 ---- */
static void update_component_counts(const rknn_detection_t *dets, int n, int img_h)
{
    pthread_mutex_lock(&g_cc.lock);

    /* 本帧原始计数 */
    float raw[CLS_COUNT] = {0};
    for (int i = 0; i < n; i++) {
        int cid = dets[i].class_id;
        if (cid < 0 || cid >= CLS_COUNT) continue;
        if (dets[i].confidence < DET_CONF_MIN) continue;
        /* 文字类别不参与元件计数, 由 detect_text_filter 单独处理 */
        if (cid == CLS_TRANSISTOR) continue;
        if (cid == CLS_TRANSISTOR) continue;
        raw[cid] += 1.0f;
    }

    /* EMA 更新, 首次直接赋值 */
    if (g_cc.frame_cnt == 0) {
        for (int i = 0; i < CLS_COUNT; i++)
            g_cc.ema_counts[i] = raw[i];
    } else {
        for (int i = 0; i < CLS_COUNT; i++)
            g_cc.ema_counts[i] = g_cc.ema_counts[i] * (1.0f - EMA_ALPHA)
                                 + raw[i] * EMA_ALPHA;
    }

    /* 四舍五入→稳定计数 */
    for (int i = 0; i < CLS_COUNT; i++)
        g_cc.stable_counts[i] = (int)(g_cc.ema_counts[i] + 0.5f);

    /* 文字过滤检测 (每 30 帧更新一次, 避免抖动) */
    if (g_cc.frame_cnt % 30 == 0)
        g_cc.text_filter = detect_text_filter(dets, n, img_h);

    /* 缺损标志: 5秒窗口内>30%帧有缺损 → 触发 (150帧窗口, >45帧) */
    #define DAM_WINDOW 150
    static int dam_hist[DAM_WINDOW] = {0};
    static int dam_idx = 0, dam_cnt = 0;
    int damaged_now = (raw[CLS_C_DAMAGED] > 0 || raw[CLS_R_DAMAGED] > 0 ||
                       raw[CLS_D_DAMAGED] > 0) ? 1 : 0;
    /* 滑动窗口: 减去旧值, 加新值 */
    dam_cnt -= dam_hist[dam_idx];
    dam_hist[dam_idx] = damaged_now;
    dam_cnt += damaged_now;
    dam_idx = (dam_idx + 1) % DAM_WINDOW;
    g_cc.has_damaged = (dam_cnt > DAM_WINDOW * 20 / 100) ? 1 : 0;  /* >20% */

    /* 强置缺损计数: 窗口内>30%的类设为至少1 */
    if (g_cc.has_damaged) {
        if (g_cc.stable_counts[CLS_C_DAMAGED] <= 0 &&
            g_cc.ema_counts[CLS_C_DAMAGED] > 0.2f) g_cc.stable_counts[CLS_C_DAMAGED] = 1;
        if (g_cc.stable_counts[CLS_R_DAMAGED] <= 0 &&
            g_cc.ema_counts[CLS_R_DAMAGED] > 0.2f) g_cc.stable_counts[CLS_R_DAMAGED] = 1;
        if (g_cc.stable_counts[CLS_D_DAMAGED] <= 0 &&
            g_cc.ema_counts[CLS_D_DAMAGED] > 0.2f) g_cc.stable_counts[CLS_D_DAMAGED] = 1;
    }
    /* has_unknown set by CV */

    if (g_cc.frame_cnt % 50 == 0) {
        fprintf(stderr, "[COUNT] raw[R=%d C=%d D=%d dam=%d,%d,%d] ema[dam=%.2f,%.2f,%.2f] has_dam=%d\n",
                (int)raw[3], (int)raw[0], (int)raw[1],
                (int)raw[6], (int)raw[5], (int)raw[10],
                g_cc.ema_counts[6], g_cc.ema_counts[5], g_cc.ema_counts[10],
                g_cc.has_damaged);
    }
    g_cc.frame_cnt++;
    pthread_mutex_unlock(&g_cc.lock);
}

/* ---- 对外 API: 获取计数结果 (线程安全) ---- */
void cv_branch_get_component_result(int counts[15], int *text_filter,
                                     int *has_damaged, int *has_unknown)
{
    pthread_mutex_lock(&g_cc.lock);

    /* 直接返回原始模型计数 (12类) */
    memcpy(counts, g_cc.stable_counts, sizeof(int) * 15);

    if (text_filter) *text_filter = g_cc.text_filter;
    if (has_damaged) *has_damaged = g_cc.has_damaged;
    if (has_unknown) *has_unknown = g_cc.has_unknown;
    pthread_mutex_unlock(&g_cc.lock);
}

/* ---- 获取文字过滤模式名称 ---- */
const char *cv_branch_get_filter_name(int text_filter)
{
    static const char *names[] = {"电阻", "电容", "二极管"};
    if (text_filter < 0 || text_filter > 2) return NULL;
    return names[text_filter];
}

/* ---- 获取类别名称 ---- */
const char *cv_branch_get_class_name(int class_id)
{
    if (class_id < 0 || class_id >= CLS_COUNT) return "?";
    return g_class_names[class_id];
}

/* ============================================================
 *  C/C++ 兼容的原子操作 (GCC builtins)
 * ============================================================ */
#define ATOMIC_LOAD(p)     __atomic_load_n((p), __ATOMIC_RELAXED)
#define ATOMIC_STORE(p,v)  __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define ATOMIC_ADD(p,v)    __atomic_fetch_add((p), (v), __ATOMIC_RELAXED)

/* ============================================================
 *  内部常量
 * ============================================================ */
#define DEFAULT_QUEUE_SIZE      8
#define MAX_DETECTIONS          64

/* ============================================================
 *  全局状态
 * ============================================================ */
static struct {
    /* 初始标志 */
    int   initialized;

    /* 配置 */
    cv_branch_config_t cfg;

    /* 处理线程 */
    pthread_t  thread;
    int        running;       // 原子访问 via ATOMIC_*

    /* 帧队列 — leaky ring buffer */
    cv_frame_t    *ring;
    int            ring_size;
    int            write_idx;  // 原子访问 via ATOMIC_* (生产者)
    int            read_idx;   // 仅在消费者线程访问
    pthread_mutex_t lock;
    pthread_cond_t  cond;

    /* 统计 */
    int64_t  total_in;        // 原子访问 via ATOMIC_*
    int64_t  total_out;
    int64_t  total_drop;

    /* 标注帧输出缓冲 (线程安全) */
    uint8_t        *out_jpeg;         // 最新标注 JPEG 数据
    size_t          out_jpeg_size;
    int64_t         out_jpeg_id;      // 对应的帧 ID, -1 = 无新帧
    pthread_mutex_t out_lock;         // 保护 out_jpeg / out_jpeg_size / out_jpeg_id

} g_cv = {0};

/* ============================================================
 *  内部: 帧拷贝 & 释放
 * ============================================================ */
static void free_frame_content(cv_frame_t *f)
{
    if (f->data) {
        free(f->data);
        f->data = NULL;
    }
    f->size = 0;
}

static int copy_frame(cv_frame_t *dst, const cv_frame_t *src)
{
    dst->width        = src->width;
    dst->height       = src->height;
    dst->stride       = src->stride;
    dst->format       = src->format;
    dst->timestamp_us = src->timestamp_us;
    dst->frame_id     = src->frame_id;
    dst->size         = src->size;

    dst->data = (uint8_t *)malloc(src->size);
    if (!dst->data) {
        fprintf(stderr, "[CV] malloc frame(%zu) failed\n", src->size);
        return -1;
    }
    memcpy(dst->data, src->data, src->size);
    return 0;
}

/* ============================================================
 *  内部: 默认结果回调 (空实现)
 * ============================================================ */
static void default_on_result(const cv_result_t *result, void *user_data)
{
    (void)result;
    (void)user_data;
}

/* ============================================================
 *  辅助: 获取当前时间 (微秒)
 * ============================================================ */
static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* ============================================================
 *  OpenCV 处理模块 (条件编译)
 *
 *  管线:
 *    MJPEG→BGR 解码 → RGA 硬件 resize (letterbox 640x640)
 *    → RKNN NPU 推理 → NMS → EMA 框平滑
 *    → 元器件计数 → 画框 → JPEG 编码 → 回调
 * ============================================================ */
#ifdef CV_BRANCH_HAS_OPENCV

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

/* ---- 帧格式 → cv::Mat (BGR) ---- */
static int frame_to_bgr(const cv_frame_t *frame, cv::Mat &mat)
{
    if (frame->format == CV_FMT_JPEG) {
        if (!frame->data || frame->size == 0) return -1;
        try {
            std::vector<uint8_t> jpeg_buf(frame->data, frame->data + frame->size);
            mat = cv::imdecode(jpeg_buf, cv::IMREAD_COLOR);
            return mat.empty() ? -1 : 0;
        } catch (const cv::Exception &e) {
            fprintf(stderr, "[CV] imdecode error: %s\n", e.what());
            return -1;
        }
    }
    if (frame->format == CV_FMT_BGR888) {
        mat = cv::Mat(frame->height, frame->width, CV_8UC3,
                      frame->data, frame->stride > 0 ? frame->stride : frame->width * 3);
        return 0;
    }
    if (frame->format == CV_FMT_GRAY8) {
        cv::Mat gray(frame->height, frame->width, CV_8UC1,
                     frame->data, frame->stride > 0 ? frame->stride : frame->width);
        cv::cvtColor(gray, mat, cv::COLOR_GRAY2BGR);
        return 0;
    }
    fprintf(stderr, "[CV] unsupported pixel format: %d\n", frame->format);
    return -1;
}

/* ---- 框平滑: EMA (旧框×0.7 + 新框×0.3), 消除检测框抖动 ---- */
#define SMOOTH_ALPHA 0.3f
#define SMOOTH_MIN_IOU 0.3f

static float box_iou(const rknn_detection_t *a, const rknn_detection_t *b)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = ((a->x + a->w) < (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    int y2 = ((a->y + a->h) < (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);
    if (x2 <= x1 || y2 <= y1) return 0.0f;
    float inter = (float)(x2 - x1) * (y2 - y1);
    float area_a = (float)a->w * a->h, area_b = (float)b->w * b->h;
    return inter / (area_a + area_b - inter + 1e-6f);
}

static void smooth_detections(rknn_result_t *res)
{
    static rknn_detection_t prev[20];
    static int prev_cnt = 0;

    for (int i = 0; i < res->count && i < 20; i++) {
        int best_j = -1; float best_iou = SMOOTH_MIN_IOU;
        for (int j = 0; j < prev_cnt; j++) {
            if (res->detections[i].class_id != prev[j].class_id) continue;
            float iou_val = box_iou(&res->detections[i], &prev[j]);
            if (iou_val > best_iou) { best_iou = iou_val; best_j = j; }
        }
        if (best_j >= 0) {
            rknn_detection_t *d = &res->detections[i];
            rknn_detection_t *p = &prev[best_j];
            d->x = (int)(p->x * (1.0f - SMOOTH_ALPHA) + d->x * SMOOTH_ALPHA);
            d->y = (int)(p->y * (1.0f - SMOOTH_ALPHA) + d->y * SMOOTH_ALPHA);
            d->w = (int)(p->w * (1.0f - SMOOTH_ALPHA) + d->w * SMOOTH_ALPHA);
            d->h = (int)(p->h * (1.0f - SMOOTH_ALPHA) + d->h * SMOOTH_ALPHA);
            d->confidence = p->confidence * (1.0f - SMOOTH_ALPHA) + d->confidence * SMOOTH_ALPHA;
        }
    }
    prev_cnt = res->count;
    for (int i = 0; i < res->count && i < 20; i++)
        prev[i] = res->detections[i];
}

/* ---- 画检测框 (元器件识别配色方案) ---- */
static void draw_detections(cv::Mat &mat, rknn_result_t *rknn_res)
{
    /* 13类检测框颜色 */
    const cv::Scalar colors[] = {
        cv::Scalar(255,  0,  0), // 0 Capacitor   蓝
        cv::Scalar(  0,  0,255), // 1 Diode       红
        cv::Scalar(128,128,128), // 2 Transistor  灰
        cv::Scalar(  0,255,  0), // 3 Resister    绿
        cv::Scalar(255,  0,255), // 4 LED         紫
        cv::Scalar(  0,165,255), // 5 C-dam       橙
        cv::Scalar(  0,165,255), // 6 R-dam       橙
        cv::Scalar(100,255,255), // 7 dianzu      黄
        cv::Scalar(100,255,255), // 8 dianrong    黄
        cv::Scalar(100,255,255), // 9 erjiguan    黄
        cv::Scalar(  0,165,255), //10 D-dam       橙
        cv::Scalar(255,255,  0), //11 Pot         青
        cv::Scalar(255,100,  0), //12 Connecter   天蓝
        cv::Scalar(255,255,  0), //13 Xtal        青
        cv::Scalar(200,200,  0), //14 IC          浅青
    };
    const int n_colors = sizeof(colors) / sizeof(colors[0]);

    for (int i = 0; i < rknn_res->count && i < 64; i++) {
        rknn_detection_t *d = &rknn_res->detections[i];
        int cid = d->class_id;
        if (cid < 0 || cid >= n_colors) cid = CLS_TRANSISTOR;
        cv::Scalar color = colors[cid];

        /* 文字类别用细框 */
        if (cid >= CLS_TEXT_R && cid <= CLS_TEXT_D) {
            cv::rectangle(mat,
                          cv::Point(d->x, d->y),
                          cv::Point(d->x + d->w, d->y + d->h),
                          color, 1);
        }
        /* 缺损类用虚线框 */
        else if (is_damaged_cls(cid)) {
            int thickness = 2;
            int dash_len = 8;
            /* 简化: 用多个短线段近似虚线 (OpenCV 无原生虚线) */
            for (int seg = 0; seg < 4; seg++) {
                cv::Point p1, p2;
                switch (seg) {
                    case 0: p1 = cv::Point(d->x, d->y); p2 = cv::Point(d->x + d->w, d->y); break;
                    case 1: p1 = cv::Point(d->x + d->w, d->y); p2 = cv::Point(d->x + d->w, d->y + d->h); break;
                    case 2: p1 = cv::Point(d->x + d->w, d->y + d->h); p2 = cv::Point(d->x, d->y + d->h); break;
                    case 3: p1 = cv::Point(d->x, d->y + d->h); p2 = cv::Point(d->x, d->y); break;
                }
                cv::LineIterator it(mat, p1, p2, 8);
                for (int k = 0; k < it.count; k++) {
                    if ((k / dash_len) % 2 == 0) {
                        cv::Point pt = it.pos();
                        cv::circle(mat, pt, thickness / 2, color, -1);
                    }
                    it++;
                }
            }
        }
        /* 正常元件: 实线框 */
        else {
            cv::rectangle(mat,
                          cv::Point(d->x, d->y),
                          cv::Point(d->x + d->w, d->y + d->h),
                          color, 2);
        }

        /* 标签 + 置信度 */
        char label[128];
        const char *name = (cid >= 0 && cid < CLS_COUNT) ? g_class_names[cid] : "?";
        snprintf(label, sizeof(label), "%s %.2f", name, d->confidence);
        cv::putText(mat, label,
                    cv::Point(d->x, d->y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

/* ---- 单帧处理 (OpenCV + RKNN NPU) ---- */
static int64_t micros(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void process_one_frame_cv(const cv_frame_t *frame)
{
    cv_result_t result;
    memset(&result, 0, sizeof(result));
    result.frame_id = frame->frame_id;
    int64_t t0 = micros(), t1;

    // 1. MJPEG → BGR 解码
    cv::Mat mat_full;
    if (frame_to_bgr(frame, mat_full) != 0) {
        if (g_cv.cfg.on_result)
            g_cv.cfg.on_result(&result, g_cv.cfg.user_data);
        return;
    }
    t1 = micros();

    // 2. RKNN NPU 推理
    int64_t t2 = micros();
    rknn_result_t rknn_res;
    memset(&rknn_res, 0, sizeof(rknn_res));
    int has_detection = 0;
    int64_t t3 = 0;
    if (rknn_infer_run(mat_full.data, mat_full.cols, mat_full.rows, &rknn_res) == 0) {
        has_detection = 1;
        t3 = micros();

        // EMA平滑消抖
        smooth_detections(&rknn_res);

        /* ==== CV 灰度未知元件检测 ==== */
        {
            cv::Mat gray, otsu, morph;
            cv::cvtColor(mat_full, gray, cv::COLOR_BGR2GRAY);
            cv::threshold(gray, otsu, 0, 255, cv::THRESH_BINARY_INV|cv::THRESH_OTSU);
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7,7));
            cv::morphologyEx(otsu, morph, cv::MORPH_CLOSE, kernel);
            cv::morphologyEx(morph, morph, cv::MORPH_OPEN, kernel);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(morph, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            int unk_cnt = 0;
            for (size_t i = 0; i < contours.size() && unk_cnt < 32; i++) {
                cv::Rect r = cv::boundingRect(contours[i]);
                if (r.width*r.height < 400 || r.width*r.height > 80000) continue;
                bool known = false;
                for (int j = 0; j < rknn_res.count; j++) {
                    int cid = rknn_res.detections[j].class_id;
                    if (cid == CLS_TRANSISTOR) continue;
                    if (cid == CLS_TRANSISTOR) continue;
                    cv::Rect kr(rknn_res.detections[j].x, rknn_res.detections[j].y,
                                rknn_res.detections[j].w, rknn_res.detections[j].h);
                    int dw = kr.width*20/100, dh = kr.height*20/100;
                    kr.x -= dw; kr.width += dw*2;
                    kr.y -= dh; kr.height += dh*2;
                    cv::Rect inter = r & kr;
                    if (inter.area() > 0 && (float)inter.area()/r.area() >= 0.50f)
                        { known = true; break; }
                }
                if (known) continue;
                int idx = rknn_res.count + unk_cnt;
                if (idx >= 52) break;
                rknn_res.detections = (rknn_detection_t*)realloc(
                    rknn_res.detections, (idx+1)*sizeof(rknn_detection_t));
                rknn_res.detections[idx].class_id = -1;
                rknn_res.detections[idx].confidence = 0.0f;
                rknn_res.detections[idx].x=r.x; rknn_res.detections[idx].y=r.y;
                rknn_res.detections[idx].w=r.width; rknn_res.detections[idx].h=r.height;
                snprintf(rknn_res.detections[idx].label,64,"Unknown");
                unk_cnt++;
            }
            rknn_res.count += unk_cnt;
            #define UNK_WINDOW 150
            static int unk_hist[UNK_WINDOW]={0}, unk_idx=0, unk_win_cnt=0;
            unk_win_cnt -= unk_hist[unk_idx];
            unk_hist[unk_idx] = (unk_cnt > 0) ? 1 : 0;
            unk_win_cnt += unk_hist[unk_idx];
            unk_idx = (unk_idx+1) % UNK_WINDOW;
            static float unk_ema = 0.0f;
            unk_ema = unk_ema * 0.7f + (float)unk_cnt * 0.3f;
            int unk_stable = (int)(unk_ema + 0.5f);
            pthread_mutex_lock(&g_cc.lock);
            g_cc.has_unknown = (unk_win_cnt > UNK_WINDOW*20/100) ? unk_stable : 0;
            pthread_mutex_unlock(&g_cc.lock);
        }

        // 元器件计数 (在画框之前, 使用平滑后的检测结果)
        update_component_counts(rknn_res.detections, rknn_res.count, mat_full.rows);

        // 画检测框
        draw_detections(mat_full, &rknn_res);

        // 组装回调结果
        result.count       = rknn_res.count;
        result.elapsed_us  = rknn_res.elapsed_us;
        result.detections  = (cv_detection_t *)calloc(rknn_res.count,
                                                       sizeof(cv_detection_t));
        for (int i = 0; i < rknn_res.count; i++) {
            cv_detection_t *d = &result.detections[i];
            d->class_id   = rknn_res.detections[i].class_id;
            d->confidence = rknn_res.detections[i].confidence;
            d->x = rknn_res.detections[i].x;
            d->y = rknn_res.detections[i].y;
            d->w = rknn_res.detections[i].w;
            d->h = rknn_res.detections[i].h;
            snprintf(d->label, sizeof(d->label), "%s",
                     rknn_res.detections[i].label);
        }

        free(rknn_res.detections);
    }

    // 3. 编码 JPEG 输出 (有检测画框, 无检测原图)
    {
        std::vector<uchar> jpeg_buf;
        std::vector<int>  jpeg_params = { cv::IMWRITE_JPEG_QUALITY, 60 };
        cv::imencode(".jpg", mat_full, jpeg_buf, jpeg_params);
        pthread_mutex_lock(&g_cv.out_lock);
        free(g_cv.out_jpeg);
        g_cv.out_jpeg      = (uint8_t *)malloc(jpeg_buf.size());
        g_cv.out_jpeg_size = jpeg_buf.size();
        g_cv.out_jpeg_id   = frame->frame_id;
        if (g_cv.out_jpeg) memcpy(g_cv.out_jpeg, jpeg_buf.data(), jpeg_buf.size());
        pthread_mutex_unlock(&g_cv.out_lock);
    }
    int64_t t4 = micros();

    /* 每100帧打印一次阶段耗时分布 */
    static int perf_cnt = 0;
    if (++perf_cnt % 100 == 0) {
        printf("[PERF] decode=%lldms rknn=%lldms encode=%lldms total=%lldms\n",
               (long long)(t1-t0)/1000,
               (long long)(has_detection ? (t3-t2)/1000 : 0),
               (long long)(t4-(has_detection ? t3 : t2))/1000,
               (long long)(t4-t0)/1000);
    }

    if (g_cv.cfg.on_result)
        g_cv.cfg.on_result(&result, g_cv.cfg.user_data);

    free(result.detections);
}

#else // !CV_BRANCH_HAS_OPENCV

/* ---- 单帧处理 (无 OpenCV: 空实现) ---- */
static void process_one_frame_cv(const cv_frame_t *frame)
{
    cv_result_t result;
    memset(&result, 0, sizeof(result));
    result.frame_id = frame->frame_id;
    (void)frame;

    if (g_cv.cfg.on_result)
        g_cv.cfg.on_result(&result, g_cv.cfg.user_data);
}

#endif // CV_BRANCH_HAS_OPENCV

/* ============================================================
 *  内部: 单帧处理入口 (统一)
 * ============================================================ */
static void process_one_frame(const cv_frame_t *frame)
{
    process_one_frame_cv(frame);
}

/* ============================================================
 *  内部: 处理线程主循环
 * ============================================================ */
static void *process_thread(void *arg)
{
    (void)arg;
    printf("[CV] Process thread started\n");

    while (ATOMIC_LOAD(&g_cv.running)) {
        /* ---- 等待新帧 ---- */
        pthread_mutex_lock(&g_cv.lock);

        while (ATOMIC_LOAD(&g_cv.write_idx) == g_cv.read_idx &&
               ATOMIC_LOAD(&g_cv.running)) {
            pthread_cond_wait(&g_cv.cond, &g_cv.lock);
        }

        if (!ATOMIC_LOAD(&g_cv.running)) {
            pthread_mutex_unlock(&g_cv.lock);
            break;
        }

        int idx = g_cv.read_idx;
        pthread_mutex_unlock(&g_cv.lock);

        /* ---- 处理帧 ---- */
        process_one_frame(&g_cv.ring[idx]);

        ATOMIC_ADD(&g_cv.total_out, 1);

        /* ---- 释放该槽帧数据 ---- */
        pthread_mutex_lock(&g_cv.lock);
        free_frame_content(&g_cv.ring[idx]);
        g_cv.read_idx = (g_cv.read_idx + 1) % g_cv.ring_size;
        pthread_mutex_unlock(&g_cv.lock);
    }

    printf("[CV] Process thread stopped\n");
    return NULL;
}

/* ============================================================
 *  API 实现
 * ============================================================ */

int cv_branch_init(const cv_branch_config_t *cfg)
{
    if (g_cv.initialized) {
        fprintf(stderr, "[CV] Already initialized\n");
        return -1;
    }

    memset(&g_cv, 0, sizeof(g_cv));

    /* ---- 拷贝配置 ---- */
    if (cfg) {
        g_cv.cfg = *cfg;
    }
    if (g_cv.cfg.max_queue_size <= 0) {
        g_cv.cfg.max_queue_size = DEFAULT_QUEUE_SIZE;
    }
    if (!g_cv.cfg.on_result) {
        g_cv.cfg.on_result = default_on_result;
    }

    /* ---- 分配环形缓冲区 ---- */
    g_cv.ring_size = g_cv.cfg.max_queue_size + 1;
    g_cv.ring = (cv_frame_t *)calloc(g_cv.ring_size, sizeof(cv_frame_t));
    if (!g_cv.ring) {
        fprintf(stderr, "[CV] ring alloc failed\n");
        return -1;
    }
    g_cv.write_idx = 0;
    g_cv.read_idx  = 0;

    /* ---- 同步原语 ---- */
    if (pthread_mutex_init(&g_cv.lock, NULL) != 0) {
        perror("[CV] mutex_init");
        free(g_cv.ring);
        g_cv.ring = NULL;
        return -1;
    }
    if (pthread_cond_init(&g_cv.cond, NULL) != 0) {
        perror("[CV] cond_init");
        pthread_mutex_destroy(&g_cv.lock);
        free(g_cv.ring);
        g_cv.ring = NULL;
        return -1;
    }
    if (pthread_mutex_init(&g_cv.out_lock, NULL) != 0) {
        perror("[CV] out_lock_init");
        pthread_cond_destroy(&g_cv.cond);
        pthread_mutex_destroy(&g_cv.lock);
        free(g_cv.ring);
        g_cv.ring = NULL;
        return -1;
    }

    /* ---- 初始化 RKNN 推理 ---- */
#ifdef CV_BRANCH_HAS_OPENCV
    if (g_cv.cfg.model_path && g_cv.cfg.model_path[0]) {
        if (rknn_infer_init(g_cv.cfg.model_path) != 0) {
            fprintf(stderr, "[CV] WARN: RKNN init failed, AI disabled\n");
        }
    }
#endif

    /* ---- 重置计数引擎 ---- */
    pthread_mutex_lock(&g_cc.lock);
    memset(&g_cc, 0, sizeof(g_cc));
    g_cc.text_filter = -1;
    pthread_mutex_unlock(&g_cc.lock);

    /* ---- 启动处理线程 ---- */
    ATOMIC_STORE(&g_cv.running, 1);
    if (pthread_create(&g_cv.thread, NULL, process_thread, NULL) != 0) {
        perror("[CV] pthread_create");
        pthread_cond_destroy(&g_cv.cond);
        pthread_mutex_destroy(&g_cv.lock);
        free(g_cv.ring);
        g_cv.ring = NULL;
        return -1;
    }

    g_cv.initialized = 1;
    printf("[CV] Initialized OK (queue_size=%d, ring_size=%d)\n",
           g_cv.cfg.max_queue_size, g_cv.ring_size);
    return 0;
}

int cv_branch_push_frame(const cv_frame_t *frame)
{
    if (!g_cv.initialized || !ATOMIC_LOAD(&g_cv.running)) {
        return -2;
    }

    ATOMIC_ADD(&g_cv.total_in, 1);

    size_t min_size = frame->size;
    if (min_size == 0) {
        int bpp = 3;
        switch (frame->format) {
            case CV_FMT_JPEG:  bpp = 0; break;
            case CV_FMT_GRAY8: bpp = 1; break;
            case CV_FMT_NV12:
            case CV_FMT_NV21:  bpp = 1; min_size = frame->width * frame->height * 3 / 2; break;
            case CV_FMT_YUYV:  bpp = 2; break;
            default:           bpp = 3; break;
        }
        if (bpp > 0) min_size = frame->width * frame->height * bpp;
    }

    pthread_mutex_lock(&g_cv.lock);

    int w_idx = ATOMIC_LOAD(&g_cv.write_idx);
    int next_w = (w_idx + 1) % g_cv.ring_size;

    if (next_w == g_cv.read_idx) {
        int drop_idx = g_cv.read_idx;
        free_frame_content(&g_cv.ring[drop_idx]);
        g_cv.read_idx = (g_cv.read_idx + 1) % g_cv.ring_size;
        ATOMIC_ADD(&g_cv.total_drop, 1);
    }

    if (copy_frame(&g_cv.ring[w_idx], frame) != 0) {
        pthread_mutex_unlock(&g_cv.lock);
        return -1;
    }

    ATOMIC_STORE(&g_cv.write_idx, next_w);
    pthread_cond_signal(&g_cv.cond);
    pthread_mutex_unlock(&g_cv.lock);

    return 0;
}

int cv_branch_is_running(void)
{
    return (g_cv.initialized && ATOMIC_LOAD(&g_cv.running)) ? 1 : 0;
}

void cv_branch_get_stats(int64_t *total_in, int64_t *total_out, int64_t *total_drop)
{
    if (total_in)  *total_in  = ATOMIC_LOAD(&g_cv.total_in);
    if (total_out) *total_out = ATOMIC_LOAD(&g_cv.total_out);
    if (total_drop) *total_drop = ATOMIC_LOAD(&g_cv.total_drop);
}

const uint8_t *cv_branch_get_annotated_frame(size_t *out_size)
{
    if (!g_cv.initialized) { *out_size = 0; return NULL; }

    pthread_mutex_lock(&g_cv.out_lock);
    if (g_cv.out_jpeg_id >= 0 && g_cv.out_jpeg) {
        *out_size = g_cv.out_jpeg_size;
        uint8_t *ptr = g_cv.out_jpeg;
        g_cv.out_jpeg_id = -1;
        pthread_mutex_unlock(&g_cv.out_lock);
        return ptr;
    }
    pthread_mutex_unlock(&g_cv.out_lock);
    *out_size = 0;
    return NULL;
}

void cv_branch_deinit(void)
{
    if (!g_cv.initialized) return;

    printf("[CV] Deinitializing...\n");

    ATOMIC_STORE(&g_cv.running, 0);
    pthread_cond_signal(&g_cv.cond);
    pthread_join(g_cv.thread, NULL);

    pthread_mutex_lock(&g_cv.lock);
    while (g_cv.read_idx != ATOMIC_LOAD(&g_cv.write_idx)) {
        free_frame_content(&g_cv.ring[g_cv.read_idx]);
        g_cv.read_idx = (g_cv.read_idx + 1) % g_cv.ring_size;
    }
    pthread_mutex_unlock(&g_cv.lock);

    pthread_mutex_lock(&g_cv.out_lock);
    free(g_cv.out_jpeg);
    g_cv.out_jpeg = NULL;
    pthread_mutex_unlock(&g_cv.out_lock);

    pthread_cond_destroy(&g_cv.cond);
    pthread_mutex_destroy(&g_cv.lock);
    pthread_mutex_destroy(&g_cv.out_lock);

#ifdef CV_BRANCH_HAS_OPENCV
    rknn_infer_deinit();
#endif

    free(g_cv.ring);
    g_cv.ring = NULL;
    g_cv.initialized = 0;

    printf("[CV] Deinit done. "
           "in=%lld out=%lld drop=%lld\n",
           (long long)ATOMIC_LOAD(&g_cv.total_in),
           (long long)ATOMIC_LOAD(&g_cv.total_out),
           (long long)ATOMIC_LOAD(&g_cv.total_drop));
}
