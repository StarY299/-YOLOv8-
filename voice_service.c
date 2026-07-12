/**
 * voice_service.c — sherpa-onnx TTS 语音播报实现
 *
 * 架构:
 *   主线程调用 tts_speak() → 文本放入消息队列 → 立即返回
 *   TTS 后台线程 → 取消息 → sherpa-onnx 合成 → aplay 播放
 *
 * 依赖:
 *   - libsherpa-onnx.so + libonnxruntime.so (放在 /userdata/tts/)
 *   - vits-icefall-zh-aishell3 模型文件
 *   - 编译时定义 HAS_SHERPA_ONNX 启用 TTS 功能
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>

#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif

#include "voice_service.h"

/* ============================================================
 *  消息队列 (线程安全, leaky ring buffer)
 * ============================================================ */
#define QUEUE_SIZE    8        /* 最多缓存 8 条待播报消息 */
#define MAX_TEXT_LEN  512      /* 单条消息最大长度 (UTF-8) */

typedef struct {
    char text[MAX_TEXT_LEN];
    int  valid;
} tts_msg_t;

typedef struct {
    tts_msg_t       msgs[QUEUE_SIZE];
    int             head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} tts_queue_t;

static volatile int g_queue_running = 0;
static tts_queue_t g_queue;

static void queue_init(tts_queue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static int queue_put(tts_queue_t *q, const char *text)
{
    if (!text || text[0] == '\0') return -1;

    pthread_mutex_lock(&q->lock);

    /* 队列满: 丢弃最旧消息 */
    if (q->count >= QUEUE_SIZE) {
        q->head = (q->head + 1) % QUEUE_SIZE;
        q->count--;
        printf("[TTS] queue full, dropped oldest\n");
    }

    tts_msg_t *m = &q->msgs[q->tail];
    strncpy(m->text, text, MAX_TEXT_LEN - 1);
    m->text[MAX_TEXT_LEN - 1] = '\0';
    m->valid = 1;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static int queue_get(tts_queue_t *q, char *out, int max_len)
{
    pthread_mutex_lock(&q->lock);

    while (q->count == 0 && g_queue_running)
        pthread_cond_wait(&q->cond, &q->lock);

    if (q->count == 0 && !g_queue_running) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    tts_msg_t *m = &q->msgs[q->head];
    strncpy(out, m->text, max_len - 1);
    out[max_len - 1] = '\0';
    m->valid = 0;
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;

    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* ============================================================
 *  TTS 引擎全局状态
 * ============================================================ */
static struct {
#ifdef HAS_SHERPA_ONNX
    const SherpaOnnxOfflineTts *tts;
#endif
    pthread_t  thread;
    int        running;
    int        speaking;   /* 1=正在合成/播放 */
    char       model_dir[256];
    pthread_mutex_t state_lock;
} g_tts = {
    .running = 0,
    .speaking = 0,
    .model_dir = {0},
    .state_lock = PTHREAD_MUTEX_INITIALIZER,
};

/* ============================================================
 *  TTS 工作线程
 * ============================================================ */
static void *tts_thread(void *arg)
{
    (void)arg;
    char text[MAX_TEXT_LEN];
    char wav_path[256];
    printf("[TTS] worker thread started\n");

    while (g_tts.running) {
        /* 等待新消息 */
        if (queue_get(&g_queue, text, sizeof(text)) != 0)
            continue;

        printf("[TTS] synthesizing: \"%s\"\n", text);

        /* 标记正在播放 */
        pthread_mutex_lock(&g_tts.state_lock);
        g_tts.speaking = 1;
        pthread_mutex_unlock(&g_tts.state_lock);

#ifdef HAS_SHERPA_ONNX
        if (g_tts.tts) {
            /* ---- sherpa-onnx TTS 合成 ---- */
            SherpaOnnxGenerationConfig gen_cfg;
            memset(&gen_cfg, 0, sizeof(gen_cfg));
            gen_cfg.sid = 0;
            gen_cfg.speed = 1.0f;

            const SherpaOnnxGeneratedAudio *audio =
                SherpaOnnxOfflineTtsGenerateWithConfig(g_tts.tts, text,
                                                       &gen_cfg, NULL, NULL);
            if (audio && audio->n > 0) {
                /* 写出 WAV → aplay 播放 */
                snprintf(wav_path, sizeof(wav_path), "/tmp/tts_output_%lu.wav",
                         (unsigned long)pthread_self());
                SherpaOnnxWriteWave(audio->samples, audio->n,
                                    audio->sample_rate, wav_path);

                char cmd[512];
                snprintf(cmd, sizeof(cmd), "aplay -q -D plughw:0,0 %s 2>/dev/null", wav_path);
                system(cmd);

                /* 清理临时文件 */
                unlink(wav_path);
                SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            } else {
                fprintf(stderr, "[TTS] synthesis failed for: \"%s\"\n", text);
            }
        }
#else
        /* ---- 无 TTS 时的回退: 直接 aplay 播放预录制文件 ---- */
        printf("[TTS] (no sherpa-onnx, using fallback)\n");
        /* 尝试播放对应文本的 WAV 文件 */
        char fallback[512];
        snprintf(fallback, sizeof(fallback),
                 "aplay -q -D plughw:0,0 /userdata/voices/detect.wav 2>/dev/null");
        system(fallback);
#endif

        /* 标记播放完成 */
        pthread_mutex_lock(&g_tts.state_lock);
        g_tts.speaking = 0;
        pthread_mutex_unlock(&g_tts.state_lock);

        printf("[TTS] done\n");
    }

    printf("[TTS] worker thread stopped\n");
    return NULL;
}

/* ============================================================
 *  公共 API
 * ============================================================ */

int tts_init(const char *model_dir)
{
    if (g_tts.running) {
        fprintf(stderr, "[TTS] already initialized\n");
        return -1;
    }

    if (model_dir) {
        strncpy(g_tts.model_dir, model_dir, sizeof(g_tts.model_dir) - 1);
    }

#ifdef HAS_SHERPA_ONNX
    /* ---- 配置 sherpa-onnx Offline TTS ---- */
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    /* VITS 模型路径 */
    char path_buf[512];

    snprintf(path_buf, sizeof(path_buf), "%s/model.onnx", model_dir);
    config.model.vits.model = strdup(path_buf);

    snprintf(path_buf, sizeof(path_buf), "%s/tokens.txt", model_dir);
    config.model.vits.tokens = strdup(path_buf);

    snprintf(path_buf, sizeof(path_buf), "%s/lexicon.txt", model_dir);
    config.model.vits.lexicon = strdup(path_buf);

    /* 音素参数 */
    config.model.vits.noise_scale = 0.667f;
    config.model.vits.noise_scale_w = 0.8f;
    config.model.vits.length_scale = 1.0f;

    /* 推理配置 */
    config.model.num_threads = 2;     /* RV1126 双核 A7 */
    config.model.provider = "cpu";
    config.model.debug = 0;

    /* 中文规则 FST (处理数字/日期/电话号码) */
    {
        snprintf(path_buf, sizeof(path_buf),
                 "%s/phone.fst,%s/date.fst,%s/number.fst",
                 model_dir, model_dir, model_dir);
        config.rule_fsts = strdup(path_buf);
    }
    {
        snprintf(path_buf, sizeof(path_buf), "%s/rule.far", model_dir);
        config.rule_fars = strdup(path_buf);
    }
    config.max_num_sentences = 2;     /* 最多2句合并 */

    printf("[TTS] loading model from %s ...\n", model_dir);
    printf("[TTS]   model:  %s\n", config.model.vits.model);
    printf("[TTS]   tokens: %s\n", config.model.vits.tokens);

    g_tts.tts = SherpaOnnxCreateOfflineTts(&config);

    /* 释放 strdup 的临时字符串 */
    free((void*)config.model.vits.model);
    free((void*)config.model.vits.tokens);
    free((void*)config.model.vits.lexicon);
    free((void*)config.rule_fsts);
    free((void*)config.rule_fars);

    if (!g_tts.tts) {
        fprintf(stderr, "[TTS] SherpaOnnxCreateOfflineTts failed\n");
        return -1;
    }
    printf("[TTS] model loaded OK\n");
#endif

    /* 初始化消息队列 */
    queue_init(&g_queue);

    /* 启动 TTS 工作线程 */
    g_tts.running = 1;
    if (pthread_create(&g_tts.thread, NULL, tts_thread, NULL) != 0) {
        fprintf(stderr, "[TTS] failed to create thread\n");
        g_tts.running = 0;
#ifdef HAS_SHERPA_ONNX
        if (g_tts.tts) { SherpaOnnxDestroyOfflineTts(g_tts.tts); g_tts.tts = NULL; }
#endif
        return -1;
    }

    printf("[TTS] initialized OK\n");
    return 0;
}

int tts_speak(const char *text)
{
    if (!g_tts.running) return -1;
    if (!text || text[0] == '\0') return -1;

    printf("[TTS] queue: \"%s\"\n", text);
    return queue_put(&g_queue, text);
}

int tts_is_speaking(void)
{
    int ret;
    pthread_mutex_lock(&g_tts.state_lock);
    ret = g_tts.speaking;
    pthread_mutex_unlock(&g_tts.state_lock);
    return ret;
}

void tts_deinit(void)
{
    if (!g_tts.running) return;

    printf("[TTS] deinitializing...\n");

    /* 通知线程退出 */
    g_tts.running = 0;
    pthread_cond_signal(&g_queue.cond);
    pthread_join(g_tts.thread, NULL);

#ifdef HAS_SHERPA_ONNX
    if (g_tts.tts) {
        SherpaOnnxDestroyOfflineTts(g_tts.tts);
        g_tts.tts = NULL;
    }
#endif

    pthread_mutex_destroy(&g_queue.lock);
    pthread_cond_destroy(&g_queue.cond);
    pthread_mutex_destroy(&g_tts.state_lock);

    printf("[TTS] deinit done\n");
}

/* ============================================================
 *  元器件播报语句构建
 * ============================================================ */
static const char *comp_names_cn[] = {
    "电阻",     /* 0 CLS_RESISTOR  */
    "电容",     /* 1 CLS_CAPACITOR */
    "二极管",   /* 2 CLS_DIODE     */
    "电感",     /* 3 CLS_INDUCTOR  */
    "LED",      /* 4 CLS_LED       */
    "IC芯片",   /* 5 CLS_IC_CHIP   */
};

int tts_announce_components(const int counts[12], int text_filter,
                             int has_damaged, int has_unknown)
{
    char text[MAX_TEXT_LEN];
    int  off = 0;
    int  has_any = 0;

    /* ---- 1. 构建元件计数语句 ---- */
    off += snprintf(text + off, sizeof(text) - off, "检测到");

    /* 文字过滤模式下仅播报对应类型 */
    int start = 0, end = 6;
    if (text_filter >= 0 && text_filter <= 2) {
        start = text_filter;
        end   = text_filter + 1;
    }

    for (int i = start; i < end && off < (int)(sizeof(text) - 60); i++) {
        if (counts[i] <= 0) continue;
        has_any = 1;
        off += snprintf(text + off, sizeof(text) - off,
                        "%s%d个,", comp_names_cn[i], counts[i]);
    }

    /* 去掉末尾逗号 */
    if (off > 0 && text[off - 1] == ',') off--;

    if (!has_any) {
        snprintf(text, sizeof(text), "未检测到元件");
    }
    tts_speak(text);

    /* ---- 2. 缺损告警 ---- */
    if (has_damaged) {
        tts_speak("发现缺损管脚元件，请检查");
    }

    /* ---- 3. 未知元件 ---- */
    if (has_unknown) {
        char unk[64];
        snprintf(unk, sizeof(unk), "检测到未知元件%d个", counts[7]);
        tts_speak(unk);
    }

    return 0;
}
