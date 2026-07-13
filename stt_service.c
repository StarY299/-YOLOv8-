/**
 * stt_service.c — 语音识别 (ASR)
 * sherpa-onnx zipformer2 CTC streaming
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif
#include "stt_service.h"

#define AUDIO_RATE   16000
#define AUDIO_CHUNK  6400
#define TEXT_BUF_SZ  512

static pthread_t      g_thread;
static volatile int   g_running = 0;
static char           g_text[TEXT_BUF_SZ];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAS_SHERPA_ONNX
static const SherpaOnnxOnlineRecognizer *g_recognizer = NULL;
static const SherpaOnnxOnlineStream    *g_stream = NULL;
#endif

static void *stt_thread(void *arg)
{
    (void)arg;
    printf("[STT] thread started\n");
    float   buf[AUDIO_CHUNK];
    char    last_text[TEXT_BUF_SZ] = {0};

    while (g_running) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
            "arecord -q -f cd -d 2 -t raw - 2>/dev/null | "
            "sox -q -t raw -r 44100 -e signed -b 16 -c 2 - "
            "-t raw -r 16000 -c 1 -e float - 2>/dev/null");
        FILE *mic = popen(cmd, "r");
        if (!mic) { sleep(1); continue; }

#ifdef HAS_SHERPA_ONNX
        int n = fread(buf, sizeof(float), AUDIO_CHUNK, mic);
        pclose(mic);
        if (n <= 0) { usleep(100000); continue; }

        SherpaOnnxOnlineStreamAcceptWaveform(g_stream, AUDIO_RATE, buf, n);
        while (SherpaOnnxIsOnlineStreamReady(g_recognizer, g_stream))
            SherpaOnnxDecodeOnlineStream(g_recognizer, g_stream);

        const SherpaOnnxOnlineRecognizerResult *r =
            SherpaOnnxGetOnlineStreamResult(g_recognizer, g_stream);
        if (r && r->text && r->text[0] && strcmp(r->text, last_text)) {
            strncpy(last_text, r->text, TEXT_BUF_SZ - 1);
            pthread_mutex_lock(&g_lock);
            strncpy(g_text, r->text, TEXT_BUF_SZ - 1);
            pthread_mutex_unlock(&g_lock);
            fprintf(stderr, "[STT] %s\n", r->text);
        }
#else
        fread(buf, sizeof(float), AUDIO_CHUNK, mic);
        pclose(mic);
        usleep(100000);
#endif
    }
    printf("[STT] thread stopped\n");
    return NULL;
}

int stt_init(void)
{
    if (g_running) return -1;
#ifdef HAS_SHERPA_ONNX
    SherpaOnnxOnlineRecognizerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.feat_config.sample_rate = 16000;
    cfg.feat_config.feature_dim = 80;
    cfg.model_config.debug = 1;
    cfg.model_config.num_threads = 2;
    cfg.model_config.provider = "cpu";

    char path[256];
    snprintf(path, sizeof(path), "%s/model.int8.onnx", STT_MODEL_DIR);
    cfg.model_config.zipformer2_ctc.model = strdup(path);

    snprintf(path, sizeof(path), "%s/tokens.txt", STT_MODEL_DIR);
    cfg.model_config.tokens = strdup(path);

    printf("[STT] loading model...\n");
    g_recognizer = SherpaOnnxCreateOnlineRecognizer(&cfg);
    if (!g_recognizer) { fprintf(stderr, "[STT] init failed\n"); return -1; }
    g_stream = SherpaOnnxCreateOnlineStream(g_recognizer);
    if (!g_stream) { SherpaOnnxDestroyOnlineRecognizer(g_recognizer); g_recognizer=NULL; return -1; }
    printf("[STT] model OK\n");
#endif

    g_running = 1;
    if (pthread_create(&g_thread, NULL, stt_thread, NULL) != 0) { g_running = 0; return -1; }
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
}

const char *stt_get_text(void)
{
    static char buf[TEXT_BUF_SZ];
    pthread_mutex_lock(&g_lock);
    strncpy(buf, g_text, TEXT_BUF_SZ - 1);
    g_text[0] = '\0';
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
