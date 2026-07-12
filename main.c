/**
 * main.c — RV1126B 电子元器件识别系统
 *
 *   capture_thread ──► frame_queue ──► ai_feed_thread ──► cv_branch → RKNN NPU
 *   ├── http_server → 浏览器查看检测画面
 *   └── LVGL + TFT → 1.8寸 SPI 屏实时计数
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include "capture.h"
#include "ai_processor.h"
#include "tft_display.h"
#include "tft_ui.h"
#include "voice_service.h"
#include "http_server.h"

/* ============================================================
 *  配置
 * ============================================================ */
#define CAP_WIDTH    1920
#define CAP_HEIGHT   1080
#define CAP_FPS      30
#define QUEUE_SIZE   4

/* TFT 屏硬件 */
#define TFT_SPI_DEV  "/dev/spidev1.0"   /* SPI 设备 (板端实测) */
#define TFT_GPIO_DC  44                 /* DC 引脚 GPIO 编号 */
#define TFT_GPIO_RST 43                 /* RST 引脚 GPIO 编号 */

#define MODEL_PATH       "/userdata/components-i8.rknn"
#define ANNOUNCE_INTERVAL 10   /* 语音播报间隔 (秒) */

static volatile int running = 1;

/* ============================================================
 *  帧队列 (线程安全)
 * ============================================================ */
typedef struct {
    uint8_t *data;
    size_t   size;
    int64_t  frame_id;
} queued_frame_t;

typedef struct {
    queued_frame_t frames[QUEUE_SIZE];
    int             head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} frame_queue_t;

static frame_queue_t g_queue;

static void queue_init(frame_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_put(frame_queue_t *q, const uint8_t *data, size_t size, int64_t id)
{
    pthread_mutex_lock(&q->lock);
    while (q->count >= QUEUE_SIZE && running)
        pthread_cond_wait(&q->cond, &q->lock);
    if (!running) { pthread_mutex_unlock(&q->lock); return; }

    queued_frame_t *f = &q->frames[q->tail];
    f->data     = (uint8_t *)malloc(size);
    f->size     = size;
    f->frame_id = id;
    memcpy(f->data, data, size);
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

static int queue_get(frame_queue_t *q, queued_frame_t *out)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && running)
        pthread_cond_wait(&q->cond, &q->lock);
    if (!running && q->count == 0) { pthread_mutex_unlock(&q->lock); return -1; }

    *out = q->frames[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* ============================================================
 *  线程 1: 摄像头采集
 * ============================================================ */
static void *capture_thread(void *arg)
{
    (void)arg;
    printf("[THREAD] capture started\n");

    while (running) {
        capture_frame_t cap;
        if (capture_get_frame(&cap) != 0) {
            fprintf(stderr, "[THREAD] capture error, retrying...\n");
            usleep(100000);
            continue;
        }
        queue_put(&g_queue, cap.data, cap.size, cap.frame_id);
    }

    printf("[THREAD] capture stopped\n");
    return NULL;
}

/* ============================================================
 *  线程 2: AI 帧喂入
 * ============================================================ */
static void *ai_feed_thread(void *arg)
{
    (void)arg;
    printf("[THREAD] ai_feed started\n");

    while (running) {
        queued_frame_t f;
        if (queue_get(&g_queue, &f) != 0) break;

        if (f.size < 256) {
            static int empty_streak = 0;
            empty_streak++;
            if (empty_streak % 100 == 1)
                printf("[FEED] skip empty frame streak=%d\n", empty_streak);
            free(f.data);
            continue;
        }

        cv_frame_t cvf;
        cvf.data         = f.data;
        cvf.size         = f.size;
        cvf.width        = CAP_WIDTH;
        cvf.height       = CAP_HEIGHT;
        cvf.stride       = 0;
        cvf.format       = CV_FMT_JPEG;
        cvf.timestamp_us = 0;
        cvf.frame_id     = f.frame_id;
        cv_branch_push_frame(&cvf);

        free(f.data);
    }

    printf("[THREAD] ai_feed stopped\n");
    return NULL;
}

/* ============================================================
 *  AI 结果回调
 * ============================================================ */
static void on_ai_result(const cv_result_t *result, void *user_data)
{
    (void)user_data;
    if (!result || result->count == 0) return;

    printf("[AI] frame %lld: %d detections, elapsed=%lld us\n",
           (long long)result->frame_id, result->count,
           (long long)result->elapsed_us);
    for (int i = 0; i < result->count && i < 8; i++) {
        const cv_detection_t *d = &result->detections[i];
        printf("  [%d] %s: %.2f @ (%d,%d %dx%d)\n",
               d->class_id, d->label, d->confidence,
               d->x, d->y, d->w, d->h);
    }
}

/* ============================================================
 *  信号处理
 * ============================================================ */
static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ============================================================
 *  main
 * ============================================================ */
int main(void)
{
    printf("=== RV1126B Component Recognition System ===\n");

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);

    /* ---- 1. V4L2 摄像头 ---- */
    char cap_device[128];
    if (capture_find_device(cap_device, sizeof(cap_device)) != 0) {
        fprintf(stderr, "FATAL: USB camera not found\n");
        return -1;
    }
    printf("[MAIN] using camera: %s\n", cap_device);
    if (capture_init(cap_device, CAP_WIDTH, CAP_HEIGHT, CAP_FPS) != 0) {
        return -1;
    }

    /* ---- 2. AI 分支 ---- */
    cv_branch_config_t cv_cfg = {
        .max_queue_size    = 2,
        .processing_width  = 0,
        .processing_height = 0,
        .on_result         = on_ai_result,
        .user_data         = NULL,
        .model_path        = MODEL_PATH,
    };
    cv_branch_init(&cv_cfg);

    /* ---- 3. TFT 显示屏 + LVGL ---- */
    if (tft_display_init(TFT_SPI_DEV, TFT_GPIO_DC, TFT_GPIO_RST) != 0) {
        fprintf(stderr, "WARN: TFT init failed\n");
    } else {
        tft_ui_init();
    }

    /* ---- 4. 语音播报 (WAV拼接) ---- */

    /* ---- 5. HTTP 服务器 ---- */
    http_server_start(8080);

    /* ---- 6. 启动工作线程 ---- */
    queue_init(&g_queue);
    pthread_t cap_tid, feed_tid;
    pthread_create(&cap_tid,  NULL, capture_thread, NULL);
    pthread_create(&feed_tid, NULL, ai_feed_thread, NULL);

    /* ---- 7. 系统就绪 ---- */
    printf("\n=== System Ready ===\n");
    voice_ready();

    /* ---- 8. 主循环: 5ms LVGL 心跳 + 秒级定时任务 ---- */
    int64_t tick = 0;
    int     ms   = 0;   /* 毫秒累加器 (用于秒级定时) */

    while (running) {
        lv_task_handler();
        lv_tick_inc(5);
        usleep(5000);
        ms += 5;

        /* 每 1 秒 */
        if (ms >= 1000) {
            ms = 0;
            tick++;

            /* 每30秒打印统计 */
            if (tick % 30 == 0) {
                int64_t in, out, drop;
                cv_branch_get_stats(&in, &out, &drop);
                printf("[STATS] cv(in=%lld out=%lld drop=%lld)\n",
                       (long long)in, (long long)out, (long long)drop);
            }

            /* 每2秒: TFT 刷新计数 */
            if (tick % 2 == 0) {
                int counts[12], text_filter, has_damaged, has_unknown;
                cv_branch_get_component_result(counts, &text_filter,
                                                &has_damaged, &has_unknown);
                tft_ui_update(counts, text_filter, has_damaged, has_unknown);
            }

            /* 每10秒: 语音播报 */
            if (tick % ANNOUNCE_INTERVAL == 0) {
                int counts[12], text_filter, has_damaged, has_unknown;
                cv_branch_get_component_result(counts, &text_filter,
                                                &has_damaged, &has_unknown);
                voice_announce(counts, text_filter,
                                has_damaged, has_unknown);
            }
        }
    }

    /* ---- 9. 清理 ---- */
    printf("\n=== Shutting down ===\n");
    http_server_stop();
    pthread_cond_broadcast(&g_queue.cond);
    pthread_join(cap_tid,  NULL);
    pthread_join(feed_tid, NULL);
    cv_branch_deinit();
    tft_display_deinit();
    capture_deinit();
    printf("=== Done ===\n");
    return 0;
}
