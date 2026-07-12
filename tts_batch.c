#include <stdio.h>
#include <unistd.h>
#include <string.h>
#ifdef HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
int main(void) {
    int sids[] = {15,21,23,25,32,36,40,47,60,72,75,80,96,100,102,140,-1};
    SherpaOnnxOfflineTtsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model.vits.model = "/userdata/tts/vits-icefall-zh-aishell3/model.onnx";
    cfg.model.vits.tokens = "/userdata/tts/vits-icefall-zh-aishell3/tokens.txt";
    cfg.model.vits.lexicon = "/userdata/tts/vits-icefall-zh-aishell3/lexicon.txt";
    cfg.model.vits.length_scale = 1.2f;
    cfg.model.num_threads = 4;
    cfg.model.provider = "cpu";
    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&cfg);
    if (!tts) { printf("Init failed\n"); return -1; }
    printf("Ready. %d speakers to test.\n", (int)(sizeof(sids)/sizeof(sids[0])-1));
    for (int i = 0; sids[i] >= 0; i++) {
        printf("=== SID %d ===\n", sids[i]);
        SherpaOnnxGenerationConfig gen;
        memset(&gen, 0, sizeof(gen));
        gen.sid = sids[i];
        gen.speed = 0.2f;
        const SherpaOnnxGeneratedAudio *a =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts, "零一二三四五六七八九", &gen, NULL, NULL);
        if (a && a->n > 0) {
            char w[64]; snprintf(w, sizeof(w), "/tmp/sid_%d.wav", sids[i]);
            SherpaOnnxWriteWave(a->samples, a->n, a->sample_rate, w);
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                     "sox -v 5.0 %s -t wav - 2>/dev/null | aplay -q -D plughw:0,0 2>/dev/null", w);
            system(cmd);
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(a);
        }
        sleep(1);
    }
    SherpaOnnxDestroyOfflineTts(tts);
    return 0;
}
#else
int main(void) { return 0; }
#endif
