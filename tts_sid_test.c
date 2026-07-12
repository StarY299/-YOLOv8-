/**
 * tts_sid_test.c — 遍历测试音色
 * 用法: ./tts_sid_test [start] [step]
 *   不传参数: 全部 174 种 (0-173)
 *   ./tts_sid_test 0 10: 每10个测一次 (0,10,20...)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"

int main(int argc, char **argv)
{
    int start = 0, step = 1;

    if (argc >= 3) { start = atoi(argv[1]); step = atoi(argv[2]); }
    else if (argc >= 2) { start = atoi(argv[1]); }

    printf("=== Speaker Test: start=%d step=%d ===\n", start, step);
    printf("Press Ctrl+C to stop, or wait for all %d speakers\n",
           1 + (173 - start) / step);

    /* 初始化一次 */
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    config.model.vits.model   = "/userdata/tts/vits-icefall-zh-aishell3/model.onnx";
    config.model.vits.tokens  = "/userdata/tts/vits-icefall-zh-aishell3/tokens.txt";
    config.model.vits.lexicon = "/userdata/tts/vits-icefall-zh-aishell3/lexicon.txt";
    config.model.vits.noise_scale = 0.667f;
    config.model.vits.noise_scale_w = 0.8f;
    config.model.vits.length_scale = 1.2f;
    config.model.num_threads = 4;
    config.model.provider = "cpu";
    config.model.debug = 0;

    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&config);
    if (!tts) { fprintf(stderr, "init failed\n"); return -1; }
    printf("Model loaded\n\n");

    for (int sid = start; sid <= 173; sid += step) {
        printf("--- SID %d ---\n", sid);
        fflush(stdout);

        SherpaOnnxGenerationConfig gen;
        memset(&gen, 0, sizeof(gen));
        gen.sid = sid;
        gen.speed = 1.0f;

        char text[64];
        snprintf(text, sizeof(text), "第%d号音色测试", sid);

        const SherpaOnnxGeneratedAudio *audio =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts, text, &gen, NULL, NULL);

        if (audio && audio->n > 0) {
            char wav[64];
            snprintf(wav, sizeof(wav), "/tmp/sid_%d.wav", sid);
            SherpaOnnxWriteWave(audio->samples, audio->n, audio->sample_rate, wav);

            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                     "sox -v 10.0 %s -t wav - 2>/dev/null | aplay -q -D plughw:0,0 2>/dev/null",
                     wav);
            system(cmd);
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }

        /* 每播完一个等 1 秒 */
        sleep(1);
    }

    SherpaOnnxDestroyOfflineTts(tts);
    printf("\n=== Done ===\n");
    return 0;
}
#else
int main(void) { printf("No sherpa-onnx\n"); return 0; }
#endif
