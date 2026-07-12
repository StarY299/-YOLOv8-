/**
 * voice_service.h — WAV 拼接语音播报
 */
#ifndef _VOICE_SERVICE_H_
#define _VOICE_SERVICE_H_

#define WAV_DIR "/userdata/tts/wavs/"

#ifdef __cplusplus
extern "C" {
#endif

/* 播报系统就绪 */
int voice_ready(void);

/* 播报元器件计数 */
int voice_announce(int counts[6], int text_filter,
                   int has_damaged, int has_unknown);

/* 播放单个 WAV (同步, 阻塞) */
int voice_play(const char *name);

#ifdef __cplusplus
}
#endif
#endif
