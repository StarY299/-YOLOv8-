/**
 * tft_ui.h — 元器件计数 LVGL UI
 */
#ifndef TFT_UI_H
#define TFT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 创建 UI 控件 (屏幕 + 标签)
 */
void tft_ui_init(void);

/**
 * 更新元器件计数显示
 */
void tft_ui_stt_listening(void);
void tft_ui_stt_result(const char *text, const char *mode);
void tft_ui_update(const int counts[13], int text_filter,
                   int has_damaged, int has_unknown);

#ifdef __cplusplus
}
#endif

#endif
