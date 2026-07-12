/**
 * voice_service.h — WAV 拼接语音播报 (完整状态机)
 */
#ifndef _VOICE_SERVICE_H_
#define _VOICE_SERVICE_H_

#define WAV_DIR "/userdata/tts/wavs/"

#ifdef __cplusplus
extern "C" {
#endif

int voice_play(const char *name);
int voice_ready(void);

/**
 * 播报 (三态状态机):
 *   text_filter>=0 (红色手写): 仅播报对应类型 + 数量
 *   text_filter<0  (通用模式): 按 R→C→D→T→L 顺序播报全部
 *                              然后如果有缺损, 播报缺损种类+数量
 */
int voice_announce(const int counts[12], int text_filter, int has_damaged);

#ifdef __cplusplus
}
#endif
#endif
