/**
 * voice_service.h — 语音播报服务 (aplay WAV 拼接)
 */
#ifndef _VOICE_SERVICE_H_
#define _VOICE_SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 播放指定 WAV 文件
 *
 * @param voice_file  WAV 文件绝对路径
 * @return 0 成功, -1 失败
 */
int play_voice(const char *voice_file);

/**
 * 播放系统就绪语音
 * @param voice_dir  WAV 文件目录, 如 "/userdata/voices/"
 * @return 0 成功
 */
int announce_system_ready(const char *voice_dir);

/**
 * 播报元器件计数结果
 *
 * 播报格式:
 *   "检测到 [类型] [数量] 个 ..."
 *   如有缺损: "发现缺损元件, 请检查"
 *   如有未知: "检测到未知元件 [数量] 个"
 *
 * @param counts[12]   12 类元器件稳定计数
 * @param text_filter  文字过滤类型: -1=无过滤, 0=电阻, 1=电容, 2=二极管
 *                      非 -1 时仅播报该类型
 * @param has_damaged   1=检测到缺损元件
 * @param has_unknown   1=检测到未知元件
 * @param voice_dir     WAV 文件目录 (以 / 结尾)
 * @return 0 成功, -1 失败
 */
int announce_components(const int counts[12], int text_filter,
                        int has_damaged, int has_unknown,
                        const char *voice_dir);

#ifdef __cplusplus
}
#endif

#endif
