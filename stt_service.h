/**
 * stt_service.h — 语音识别 (ASR) 服务
 * sherpa-onnx zipformer CTC 流式中文识别
 */
#ifndef STT_SERVICE_H
#define STT_SERVICE_H

#define STT_MODEL_DIR "/userdata/tts/asr/"

#ifdef __cplusplus
extern "C" {
#endif

int  stt_init(void);
void stt_deinit(void);

/* 获取最新识别文本 (非阻塞), 无新结果返回 NULL */
const char *stt_get_text(void);

/* 是否检测到唤醒词 */
int stt_has_wake_word(void);

/* 匹配关键词 (检测到后自动清除) */
int stt_match_keyword(const char *kw);

#ifdef __cplusplus
}
#endif
#endif
