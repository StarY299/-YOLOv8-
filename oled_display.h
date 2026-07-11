/**
 * oled_display.h — OLED 元器件计数显示模块 (128x64 I2C)
 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 OLED 显示屏
 * @return 0 成功
 */
int  oled_display_init(void);

/**
 * 更新元器件计数显示 (每2秒调用一次)
 *
 * @param counts[12]   12 类元器件稳定计数
 * @param text_filter  文字过滤类型: -1=无, 0=电阻, 1=电容, 2=二极管
 * @param has_damaged   1=检测到缺损元件
 * @param has_unknown   1=检测到未知元件
 */
void oled_display_update_components(const int counts[12], int text_filter,
                                     int has_damaged, int has_unknown);

/**
 * 关闭 OLED 显示屏
 */
void oled_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
