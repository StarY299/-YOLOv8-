/**
 * voice_service.h — 五态语音播报状态机
 *
 * WAIT → JUDGE → TEXT/DAMAGED/GENERAL → WAIT
 */
#ifndef _VOICE_SERVICE_H_
#define _VOICE_SERVICE_H_

#define WAV_DIR "/userdata/tts/wavs/"

#ifdef __cplusplus
extern "C" {
#endif

int voice_play(const char *name);
int voice_ready(void);

/* 文字模式: 仅播报指定类型 (text_filter: 0=R,1=C,2=D) */
void voice_text_mode(int text_filter, const int counts[15]);

/* 缺损模式: 仅播报缺损元件 */
void voice_damaged_mode(const int counts[15]);

/* 通用模式: 播报全部正常元件 */
void voice_general_mode(const int counts[15]);

/* 未知模式 */
void voice_unknown_mode(int unknown_count);

#ifdef __cplusplus
}
#endif
#endif
