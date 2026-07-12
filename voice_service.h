/**
 * voice_service.h — sherpa-onnx TTS 语音播报服务
 *
 * 使用 vits-icefall-zh-aishell3 中文模型实现离线文字转语音.
 * 播报请求通过消息队列异步处理, 不阻塞主线程.
 */
#ifndef _VOICE_SERVICE_H_
#define _VOICE_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 TTS 引擎 (启动时调用一次, 阻塞约 2-5 秒加载模型)
 *
 * @param model_dir  模型目录路径, 如 "/userdata/tts/vits-icefall-zh-aishell3/"
 *                   目录下需包含: model.onnx, tokens.txt, lexicon.txt,
 *                   phone.fst, date.fst, number.fst, rule.far
 * @return 0 成功, -1 失败
 */
int tts_init(const char *model_dir);

/**
 * 异步播报文本 (非阻塞)
 *
 * 将文本放入消息队列后立即返回, 由独立 TTS 线程合成并播放.
 * 队列满时丢弃最旧消息.
 *
 * @param text  要播报的中文文本 (UTF-8)
 * @return 0 成功入队, -1 失败
 */
int tts_speak(const char *text);

/**
 * 播报元器件计数结果
 *
 * 根据检测结果自动构建中文播报语句并异步播放.
 * 语句格式: "检测到电阻5个, 电容3个..."
 * 如有缺损: 追加 "发现缺损管脚元件, 请检查"
 * 如有未知: 追加 "检测到未知元件 N 个"
 *
 * @param counts[12]   12 类元器件稳定计数
 * @param text_filter  文字过滤类型: -1=无过滤, 0=电阻, 1=电容, 2=二极管
 * @param has_damaged   1=检测到缺损元件
 * @param has_unknown   1=检测到未知元件
 * @return 0 成功, -1 失败
 */
int tts_announce_components(const int counts[12], int text_filter,
                             int has_damaged, int has_unknown);

/**
 * 检查 TTS 引擎是否正在播报
 * @return 1=正在播报, 0=空闲
 */
int tts_is_speaking(void);

/**
 * 销毁 TTS 引擎, 释放资源
 */
void tts_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
