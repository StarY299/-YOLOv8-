/**
 * tft_ui.c — 元器件计数显示 (128x160 ST7735S)
 *
 * 借鉴 1.8LCD/comps.c 风格: 电路符号 + 交替行色 + 总计数
 *
 * 布局:
 *   ┌──────────────────────┐
 *   │   COMPONENT AI       │ 标题栏 DARKBLUE 18px
 *   ├──────────────────────┤
 *   │ ■ 5  Resistor  ╱╲╱╲  │ 行高24px, 交替背景色
 *   │ ■ 3  Capacitor ─┤├─  │ 数量+英文名+电路符号
 *   │ ■ 2  Diode     ─▷├─  │
 *   │ ■ 2  LED       ─▷├→  │
 *   ├──────────────────────┤
 *   │ Total:12  DAM:2      │ 状态栏
 *   │ FLT:RESISTOR         │ 过滤模式(有过滤时)
 *   └──────────────────────┘
 */

#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "tft_display.h"
#include "tft_ui.h"

/* 元件信息 */
static const struct {
    int      mid;
    char     *name;
    uint16_t color;
} g_comp[] = {
    { 3, "Resistor ", GREEN   },
    { 0, "Capacitor", BLUE    },
    { 1, "Diode    ", RED     },
    { 4, "LED      ", MAGENTA },
    {11, "Pot      ", YELLOW  },
    {12, "Connecter", 0x7D7C  },
};
#define N_COMPS 6

/* ====== 电路符号绘制 (参考 1.8LCD/comps.c) ====== */

static void draw_resistor(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x,    y+6, x+3,  y+6, c);
    lcd_draw_line(x+3,  y+6, x+5,  y+2, c);
    lcd_draw_line(x+5,  y+2, x+8,  y+11,c);
    lcd_draw_line(x+8,  y+11,x+11, y+2, c);
    lcd_draw_line(x+11, y+2, x+14, y+11,c);
    lcd_draw_line(x+14, y+11,x+17, y+6, c);
    lcd_draw_line(x+17, y+6, x+20, y+6, c);
}

static void draw_capacitor(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x,    y+6, x+6,  y+6, c);
    lcd_draw_line(x+14, y+6, x+20, y+6, c);
    lcd_draw_line(x+6,  y+2, x+6,  y+11,c);
    lcd_draw_line(x+14, y+2, x+14, y+11,c);
}

static void draw_diode(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x,    y+6, x+5,  y+6, c);
    lcd_draw_line(x+5,  y+2, x+12, y+6, c);
    lcd_draw_line(x+5,  y+11,x+12, y+6, c);
    lcd_draw_line(x+12, y+2, x+12, y+11,c);
    lcd_draw_line(x+12, y+6, x+20, y+6, c);
}

static void draw_led(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x,    y+6, x+4,  y+6, c);
    lcd_draw_line(x+4,  y+2, x+10, y+6, c);
    lcd_draw_line(x+4,  y+11,x+10, y+6, c);
    lcd_draw_line(x+10, y+2, x+10, y+11,c);
    /* 发光箭头 */
    lcd_draw_line(x+6,  y+5, x+8,  y+8, c);
    lcd_draw_line(x+6,  y+7, x+8,  y+10,c);
    lcd_draw_line(x+10, y+6, x+18, y+6, c);
}

static void (*g_symbols[])(uint16_t,uint16_t,uint16_t) = {
    draw_resistor, draw_capacitor, draw_diode, draw_led, NULL, NULL
};

void tft_ui_init(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);

    /* 标题栏 */
    lcd_fill(0, 0, LCD_W, 18, DARKBLUE);
    lcd_show_string(8, 2, (const uint8_t *)"COMPONENT AI", WHITE, DARKBLUE, 16, 0);
    lcd_draw_line(0, 18, LCD_W, 18, 0x4208);

    printf("[TFT-UI] init\n");
}

void tft_ui_update(const int counts[13], int text_filter,
                   int has_damaged, int has_unknown)
{
    char buf[16];
    int  row_h = 22;
    int  y0    = 21;
    int  total = 0;

    for (int i = 0; i < N_COMPS; i++) {
        int y   = y0 + i * row_h;
        int cid = g_comp[i].mid;
        int c   = counts[cid];
        total += c;

        /* 交替行背景 */
        uint16_t bg = (i % 2 == 0) ? BLACK : 0x1082;
        lcd_fill(0, y, LCD_W - 1, y + row_h - 1, bg);
        if (i > 0) lcd_draw_line(8, y, LCD_W - 8, y, 0x4208);

        uint16_t fg  = (cid == text_filter) ? YELLOW : g_comp[i].color;
        uint16_t clr = (c > 0) ? g_comp[i].color : 0x3186;

        /* 数量 (12号字左对齐) */
        snprintf(buf, sizeof(buf), "%d", c);
        lcd_show_string(3, y + 5, (const uint8_t *)buf, fg, bg, 16, 0);

        /* 元件名 */
        lcd_show_string(20, y + 5, (const uint8_t *)g_comp[i].name, clr, bg, 16, 0);

        /* 电路符号 (右对齐) */
        if (g_symbols[i])
            g_symbols[i](105, y + 6, clr);
    }

    /* 状态栏 */
    int sep_y = y0 + N_COMPS * row_h + 2;
    lcd_draw_line(0, sep_y, LCD_W, sep_y, 0x4208);
    lcd_fill(0, sep_y + 2, LCD_W, LCD_H, BLACK);

    /* 总计数 */
    snprintf(buf, sizeof(buf), "Total: %d", total);
    lcd_show_string(2, sep_y + 4, (const uint8_t *)buf, WHITE, BLACK, 12, 0);

    /* 缺损 */
    int st2_y = sep_y + 18;
    if (has_damaged) {
        int d_cnt = counts[5] + counts[6] + counts[10];
        snprintf(buf, sizeof(buf), "DAM:%d", d_cnt);
        lcd_show_string(2, st2_y, (const uint8_t *)buf, RED, BLACK, 12, 0);
    } else {
        lcd_show_string(2, st2_y, (const uint8_t *)"OK", GREEN, BLACK, 12, 0);
    }

    /* 过滤模式 */
    if (text_filter >= 0 && text_filter <= 2) {
        static const char *fn[] = {"RESISTOR","CAPACITOR","DIODE"};
        snprintf(buf, sizeof(buf), "FLT:%s", fn[text_filter]);
        lcd_show_string(50, st2_y, (const uint8_t *)buf, YELLOW, BLACK, 12, 0);
    }

    if (has_unknown > 0) {
        snprintf(buf, sizeof(buf), "UNK:%d", has_unknown);
        lcd_show_string(80, st2_y, (const uint8_t *)buf, YELLOW, BLACK, 12, 0);
    }
}
