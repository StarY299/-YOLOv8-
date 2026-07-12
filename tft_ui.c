/**
 * tft_ui.c — 元器件计数显示
 *
 * 128x160 布局 (需求1-8):
 *   ┌──────────────────┐
 *   │  元器件识别       │  标题
 *   │                  │
 *   │ ■ 电阻     5     │  ← 需求1 核心三元件
 *   │ ■ 电容     3     │
 *   │ ■ 二极管   2     │
 *   │ ─────────────    │  分隔线
 *   │ ■ 电感     1     │  ← 需求6 扩展
 *   │ ■ LED      2     │
 *   │ ■ IC芯片   0     │
 *   │ ─────────────    │
 *   │ ⚠ 缺损!  ? 未知3 │  ← 需求7/8 状态
 *   │ [仅统计: 电阻]   │  ← 需求2/3/4 过滤
 *   └──────────────────┘
 */

#include <stdio.h>
#include "lcd.h"
#include "tft_display.h"
#include "tft_ui.h"

static const char *names[] = {"电阻","电容","二极管","电感","LED","IC芯片"};
static const uint16_t colors[] = {GREEN, BLUE, RED, YELLOW, MAGENTA, 0xFD20};

void tft_ui_init(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);

    /* 标题栏 */
    lcd_fill(0, 0, LCD_W, 18, DARKBLUE);
    lcd_show_string(8, 2, (const uint8_t *)"Component AI", WHITE, DARKBLUE, 16, 0);

    /* 分隔标题和内容 */
    lcd_draw_line(0, 18, LCD_W, 18, 0x4208);

    printf("[TFT-UI] init\n");
}

void tft_ui_update(const int counts[12], int text_filter,
                   int has_damaged, int has_unknown)
{
    char buf[32];

    /* ---- 核心三元件 (需求1-4): 电阻/电容/二极管 ---- */
    for (int i = 0; i < 3; i++) {
        int y = 22 + i * 18;
        uint16_t c = counts[i] > 0 ? colors[i] : 0x3186;

        /* 色块 + 名称 + 数量 */
        lcd_fill(4, y+3, 12, y+11, c);
        snprintf(buf, sizeof(buf), "%-6s %2d", names[i], counts[i]);
        uint16_t fg = (i == text_filter) ? YELLOW : WHITE;
        lcd_show_string(16, y+2, (const uint8_t *)buf, fg, BLACK, 16, 0);
    }

    /* 分隔线 */
    lcd_draw_line(0, 76, LCD_W, 76, 0x4208);

    /* ---- 扩展元件 (需求6) ---- */
    for (int i = 3; i < 6; i++) {
        int y = 80 + (i - 3) * 18;
        uint16_t c = counts[i] > 0 ? colors[i] : 0x3186;

        lcd_fill(4, y+3, 12, y+11, c);
        snprintf(buf, sizeof(buf), "%-6s %2d", names[i], counts[i]);
        lcd_show_string(16, y+2, (const uint8_t *)buf, 0x9CF3, BLACK, 16, 0);
    }

    /* 分隔线 */
    lcd_draw_line(0, 134, LCD_W, 134, 0x4208);

    /* ---- 状态行 (需求7/8) ---- */
    lcd_fill(0, 136, LCD_W, 160, BLACK);

    if (has_damaged) {
        snprintf(buf, sizeof(buf), "Damaged!");
        lcd_show_string(4, 138, (const uint8_t *)buf, RED, BLACK, 16, 0);
    }
    if (has_unknown) {
        snprintf(buf, sizeof(buf), "Unk:%d", counts[7]);
        int ux = has_damaged ? 80 : 4;
        lcd_show_string(ux, 138, (const uint8_t *)buf, YELLOW, BLACK, 16, 0);
    }
    if (!has_damaged && !has_unknown) {
        lcd_show_string(4, 138, (const uint8_t *)"OK", GREEN, BLACK, 16, 0);
    }

    /* ---- 过滤模式 (需求2/3/4) ---- */
    if (text_filter >= 0 && text_filter <= 2) {
        static const char *fn[] = {"Resistor","Capacitor","Diode"};
        snprintf(buf, sizeof(buf), "Flt:%s", fn[text_filter]);
        lcd_show_string(60, 148, (const uint8_t *)buf, 0x6688, BLACK, 12, 0);
    }
}
