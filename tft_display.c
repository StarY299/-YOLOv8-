/**
 * tft_display.c — 基于 lcd.c 的 TFT 初始化封装
 */
#include <stdio.h>
#include "lcd.h"
#include "tft_display.h"

int tft_display_init(void)
{
    if (lcd_hw_init() < 0) {
        fprintf(stderr, "[TFT] HW init failed\n");
        return -1;
    }
    lcd_init();
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);
    printf("[TFT] init OK (%dx%d)\n", LCD_W, LCD_H);
    return 0;
}

void tft_display_deinit(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);
    lcd_backlight_off();
    lcd_hw_deinit();
}
