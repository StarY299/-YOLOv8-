/**
 * tft_display.h — 1.8寸 TFT SPI 屏驱动 (ST7735, 128x160) + LVGL 对接
 */
#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <stdint.h>

#define TFT_W  128
#define TFT_H  160

#ifdef __cplusplus
extern "C" {
#endif

int  tft_display_init(const char *spi_dev, int gpio_dc, int gpio_rst);
void tft_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
