/**
 * stt_service.c — 语音识别实现
 *
 * 后台线程: arecord 采集 → sherpa-onnx 流式识别 → 缓存结果
 * 主线程: stt_get_text() 获取最新识别文本
 *
 * 关键词: "开始统计" "仅统计电阻" "全部统计" 等
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif

#include "stt_service.h"

#define AUDIO_RATE   16000
#define AUDIO_CHUNK  6400    /* 400ms @ 16kHz */
#define TEXT_BUF_SZ  512
#define MAX_TEXT_LEN 256

/* ---- 全局状态 ---- */
static pthread_t      g_thread;
static volatile int   g_running = 0;
static char           g_text[TEXT_BUF_SZ];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAS_SHERPA_ONNX
static const SherpaOnnxOnlineRecognizer *g_recognizer = NULL;
static SherpaOnnxOnlineStream          *g_stream = NULL;
#endif

/* ---- 后台线程: 采集 + 识别 ---- */
static void *stt_thread(void *arg)
{
    (void)arg;
    printf("[STT] thread started\n");

    /* 打开麦克风: 16kHz, 16bit, mono */
    FILE *mic = popen("arecord -q -D plughw:0,0 -f S16_LE -r 16000 -c 1 -t raw - 2>/dev/null", "r");
    if (!mic) { perror("[STT] mic"); return NULL; }

    int16_t buf[AUDIO_CHUNK];
    char    last_text[TEXT_BUF_SZ] = {0};

    while (g_running) {
#ifdef HAS_SHERPA_ONNX
        int n = fread(buf, 2, AUDIO_CHUNK, mic);
        if (n <= 0) { usleep(10000); continue; }

        /* 喂给流式识别器 */
        SherpaOnnxOnlineStreamAcceptWaveform(g_stream, AUDIO_RATE, buf, n);
        while (SherpaOnnxIsOnlineStreamReady(g_stream)) {
            SherpaOnnxDecodeOnlineStream(g_stream);
        }

        /* 获取结果 */
        const SherpaOnnxOnlineRecognizerResult *r =
            SherpaOnnxGetOnlineStreamResult(g_stream);
        if (r && r->text && r->text[0] && strcmp(r->text, last_text)) {
            strncpy(last_text, r->text, TEXT_BUF_SZ - 1);
            pthread_mutex_lock(&g_lock);
            strncpy(g_text, r->text, TEXT_BUF_SZ - 1);
            pthread_mutex_unlock(&g_lock);
            printf("[STT] %s\n", r->text);
        }
#else
        /* 无模型时只消费音频 */
        fread(buf, 2, AUDIO_CHUNK, mic);
        usleep(100000);
#endif
    }

    pclose(mic);
    printf("[STT] thread stopped\n");
    return NULL;
}

/* ---- 公共 API ---- */
int stt_init(void)
{
    if (g_running) return -1;

#ifdef HAS_SHERPA_ONNX
    SherpaOnnxOnlineRecognizerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.model_config.debug = 0;
    cfg.model_config.num_threads = 2;
    cfg.model_config.provider = "cpu";

    /* 设置 zipformer2 CTC 模型路径 */
    char path[256];
    snprintf(path, sizeof(path), "%s/model.int8.onnx", STT_MODEL_DIR);
    cfg.model_config.transducer.encoder = path;
    snprintf(path, sizeof(path), "%s/model.int8.onnx", STT_MODEL_DIR);
    cfg.model_config.transducer.decoder = path;
    snprintf(path, sizeof(path), "%s/model.int8.onnx", STT_MODEL_DIR);
    cfg.model_config.transducer.joiner = path;

    /* tokens */
    snprintf(path, sizeof(path), "%s/tokens.txt", STT_MODEL_DIR);
    cfg.model_config.tokens = path;
    cfg.model_config.tokens_buf = NULL;
    cfg.model_config.tokens_size = 0;

    printf("[STT] loading model from %s...\n", STT_MODEL_DIR);
    g_recognizer = SherpaOnnxCreateOnlineRecognizer(&cfg);
    if (!g_recognizer) {
        fprintf(stderr, "[STT] model init failed\n");
        return -1;
    }
    g_stream = SherpaOnnxCreateOnlineStream(g_recognizer);
    if (!g_stream) {
        SherpaOnnxDestroyOnlineRecognizer(g_recognizer);
        g_recognizer = NULL;
        return -1;
    }
    printf("[STT] model loaded OK\n");
#endif

    g_running = 1;
    if (pthread_create(&g_thread, NULL, stt_thread, NULL) != 0) {
        g_running = 0; return -1;
    }
    return 0;
}

void stt_deinit(void)
{
    if (!g_running) return;
    g_running = 0;
    pthread_join(g_thread, NULL);
#ifdef HAS_SHERPA_ONNX
    if (g_stream)      { SherpaOnnxDestroyOnlineStream(g_stream); g_stream = NULL; }
    if (g_recognizer)  { SherpaOnnxDestroyOnlineRecognizer(g_recognizer); g_recognizer = NULL; }
#endif
    printf("[STT] deinit done\n");
}

const char *stt_get_text(void)
{
    static char buf[TEXT_BUF_SZ];
    pthread_mutex_lock(&g_lock);
    strncpy(buf, g_text, TEXT_BUF_SZ - 1);
    g_text[0] = '\0';  /* 消费后清空 */
    pthread_mutex_unlock(&g_lock);
    return buf[0] ? buf : NULL;
}

int stt_has_wake_word(void)
{
    const char *t = stt_get_text();
    if (!t) return 0;
    return strstr(t, "开始") || strstr(t, "统计") || strstr(t, "播报");
}

int stt_match_keyword(const char *kw)
{
    const char *t = stt_get_text();
    if (!t) return 0;
    return strstr(t, kw) != NULL;
}
