/**
 * main.c — RV1126B 电子元器件识别系统
 *
 *   capture_thread → frame_queue → ai_feed_thread → GStreamer RTSP + cv_branch
 *   TFT LCD → 1.8寸 SPI 屏
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "capture.h"
#include "rtsp_stream.h"
#include "rtsp_service.h"
#include "ai_processor.h"
#include "tft_display.h"
#include "tft_ui.h"
#include "voice_service.h"

#define CAP_WIDTH        1920
#define CAP_HEIGHT       1080
#define CAP_FPS          30
#define H264_BITRATE     4000000
#define QUEUE_SIZE       4
#define MODEL_PATH       "/userdata/components-i8.rknn"
#define ANNOUNCE_INTERVAL 10

static volatile int running = 1;

typedef struct { uint8_t *data; size_t size; int64_t frame_id; } queued_frame_t;
typedef struct {
    queued_frame_t frames[QUEUE_SIZE];
    int head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} frame_queue_t;

static frame_queue_t g_queue;

static void queue_init(frame_queue_t *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}
static void queue_put(frame_queue_t *q, const uint8_t *data, size_t sz, int64_t id) {
    pthread_mutex_lock(&q->lock);
    while (q->count >= QUEUE_SIZE && running) pthread_cond_wait(&q->cond, &q->lock);
    if (!running) { pthread_mutex_unlock(&q->lock); return; }
    queued_frame_t *f = &q->frames[q->tail];
    f->data = (uint8_t *)malloc(sz); f->size = sz; f->frame_id = id;
    memcpy(f->data, data, sz);
    q->tail = (q->tail + 1) % QUEUE_SIZE; q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}
static int queue_get(frame_queue_t *q, queued_frame_t *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && running) pthread_cond_wait(&q->cond, &q->lock);
    if (!running && q->count == 0) { pthread_mutex_unlock(&q->lock); return -1; }
    *out = q->frames[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE; q->count--;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static void *capture_thread(void *arg) {
    (void)arg;
    while (running) {
        capture_frame_t cap;
        if (capture_get_frame(&cap) != 0) { usleep(100000); continue; }
        queue_put(&g_queue, cap.data, cap.size, cap.frame_id);
    }
    return NULL;
}

/* 标注帧缓存 (照搬原始 ai_camera 模式) */
static uint8_t        *g_anno_buf = NULL;
static size_t          g_anno_len = 0;
static pthread_mutex_t g_anno_lock = PTHREAD_MUTEX_INITIALIZER;

static void *ai_feed_thread(void *arg) {
    (void)arg;
    while (running) {
        queued_frame_t f;
        if (queue_get(&g_queue, &f) != 0) break;
        if (f.size < 256) { free(f.data); continue; }

        /* 更新标注帧缓存 (拷贝, 不会被消费) */
        size_t ns = 0;
        const uint8_t *nd = cv_branch_get_annotated_frame(&ns);
        if (nd && ns > 256) {
            pthread_mutex_lock(&g_anno_lock);
            free(g_anno_buf);
            g_anno_buf = (uint8_t *)malloc(ns);
            if (g_anno_buf) { memcpy(g_anno_buf, nd, ns); g_anno_len = ns; }
            else g_anno_len = 0;
            pthread_mutex_unlock(&g_anno_lock);
        }

        /* 推送: 标注帧优先, 无缓存时原始帧 */
        int pushed = 0;
        pthread_mutex_lock(&g_anno_lock);
        if (g_anno_buf && g_anno_len > 256) {
            gst_pipeline_push_frame(g_anno_buf, g_anno_len, f.frame_id);
            pushed = 1;
        }
        pthread_mutex_unlock(&g_anno_lock);
        if (!pushed)
            gst_pipeline_push_frame(f.data, f.size, f.frame_id);

        /* 喂 AI 分支 */
        cv_frame_t cvf;
        cvf.data = f.data; cvf.size = f.size;
        cvf.width = CAP_WIDTH; cvf.height = CAP_HEIGHT; cvf.stride = 0;
        cvf.format = CV_FMT_JPEG; cvf.timestamp_us = 0; cvf.frame_id = f.frame_id;
        cv_branch_push_frame(&cvf);
        free(f.data);
    }
    return NULL;
}

static void on_ai_result(const cv_result_t *result, void *user_data) {
    (void)user_data;
    if (!result || result->count == 0) return;
    printf("[AI] frame %lld: %d detections, elapsed=%lld us\n",
           (long long)result->frame_id, result->count, (long long)result->elapsed_us);
    for (int i = 0; i < result->count && i < 8; i++) {
        const cv_detection_t *d = &result->detections[i];
        printf("  [%d] %s: %.2f @ (%d,%d %dx%d)\n", d->class_id, d->label, d->confidence, d->x, d->y, d->w, d->h);
    }
}

static void sig_handler(int sig) { (void)sig; running = 0; }

int main(void) {
    printf("=== RV1126B Component Recognition System ===\n");
    signal(SIGTERM, sig_handler); signal(SIGINT, sig_handler);

    char cap_dev[128];
    if (capture_find_device(cap_dev, sizeof(cap_dev)) != 0) { fprintf(stderr, "FATAL: no camera\n"); return -1; }
    printf("[MAIN] camera: %s\n", cap_dev);
    if (capture_init(cap_dev, CAP_WIDTH, CAP_HEIGHT, CAP_FPS) != 0) return -1;

    /* GStreamer RTSP */
    if (gst_pipeline_init(CAP_WIDTH, CAP_HEIGHT, CAP_FPS, H264_BITRATE) != 0) {
        capture_deinit(); return -1;
    }

    cv_branch_config_t cv_cfg = { .max_queue_size=2, .on_result=on_ai_result, .model_path=MODEL_PATH };
    cv_branch_init(&cv_cfg);

    if (tft_display_init() != 0) fprintf(stderr, "WARN: TFT init failed\n");
    else tft_ui_init();

    queue_init(&g_queue);
    pthread_t cap_tid, feed_tid;
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&feed_tid, NULL, ai_feed_thread, NULL);

    usleep(500000);
    if (start_mediamtx() != 0) { fprintf(stderr, "WARN: mediamtx\n"); }

    printf("\n=== System Ready ===\n");
    voice_ready();

    int64_t tick = 0;
    while (running) {
        sleep(1); tick++;
        if (tick % 30 == 0) { int64_t in, out, drop; cv_branch_get_stats(&in, &out, &drop);
            printf("[STATS] cv(in=%lld out=%lld drop=%lld)\n", (long long)in, (long long)out, (long long)drop); }
        if (tick % 2 == 0) { int counts[12], f, d, u;
            cv_branch_get_component_result(counts, &f, &d, &u);
            tft_ui_update(counts, f, d, u); }
        if (tick % ANNOUNCE_INTERVAL == 0) { int counts[12], f, d, u;
            cv_branch_get_component_result(counts, &f, &d, &u);
            voice_announce(counts, f, d, u); }
    }

    printf("\n=== Shutting down ===\n");
    stop_mediamtx();
    pthread_cond_broadcast(&g_queue.cond);
    pthread_join(cap_tid, NULL); pthread_join(feed_tid, NULL);
    cv_branch_deinit();
    tft_display_deinit();
    gst_pipeline_deinit();
    capture_deinit();
    printf("=== Done ===\n");
    return 0;
}
