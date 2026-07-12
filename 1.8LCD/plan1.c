/**
 * comps.c - 元器件识别结果显示
 * 电容/电阻/二极管/电感/LED，数量+英文名+电路符号
 */
#include "lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// ====== 电路符号 (20x14 画布, 右下角区域) ======

static void draw_cap(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x, y+7, x+6, y+7, c);
    lcd_draw_line(x+14, y+7, x+20, y+7, c);
    lcd_draw_line(x+6, y+2, x+6, y+12, c);
    lcd_draw_line(x+14, y+2, x+14, y+12, c);
}

static void draw_res(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x, y+7, x+3, y+7, c);
    lcd_draw_line(x+3, y+7, x+5, y+2, c);
    lcd_draw_line(x+5, y+2, x+8, y+12, c);
    lcd_draw_line(x+8, y+12, x+11, y+2, c);
    lcd_draw_line(x+11, y+2, x+14, y+12, c);
    lcd_draw_line(x+14, y+12, x+17, y+7, c);
    lcd_draw_line(x+17, y+7, x+20, y+7, c);
}

static void draw_diode(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x, y+7, x+5, y+7, c);
    lcd_draw_line(x+5, y+2, x+12, y+7, c);
    lcd_draw_line(x+5, y+12, x+12, y+7, c);
    lcd_draw_line(x+12, y+2, x+12, y+12, c);
    lcd_draw_line(x+12, y+7, x+20, y+7, c);
}

// 电感: 线圈锯齿 ~~~~
static void draw_ind(uint16_t x, uint16_t y, uint16_t c) {
    lcd_draw_line(x, y+7, x+3, y+7, c);
    // 4个锯齿弯
    lcd_draw_line(x+3, y+7, x+5, y+2, c);
    lcd_draw_line(x+5, y+2, x+7, y+12, c);
    lcd_draw_line(x+7, y+12, x+9, y+2, c);
    lcd_draw_line(x+9, y+2, x+11, y+12, c);
    lcd_draw_line(x+11, y+12, x+13, y+2, c);
    lcd_draw_line(x+13, y+2, x+15, y+12, c);
    lcd_draw_line(x+15, y+12, x+17, y+7, c);
    lcd_draw_line(x+17, y+7, x+20, y+7, c);
}

// LED: 二极管 + 发光箭头 (向右)
static void draw_led(uint16_t x, uint16_t y, uint16_t c) {
    // 二极管主体
    lcd_draw_line(x, y+7, x+4, y+7, c);
    lcd_draw_line(x+4, y+2, x+10, y+7, c);
    lcd_draw_line(x+4, y+12, x+10, y+7, c);
    lcd_draw_line(x+10, y+2, x+10, y+12, c);
    // 箭头: 两个小斜线表示发光
    lcd_draw_line(x+6, y+6, x+8, y+9, c);
    lcd_draw_line(x+6, y+8, x+8, y+11, c);
    lcd_draw_line(x+10, y+7, x+20, y+7, c);
}

typedef struct {
    const char *name;
    int count;
    uint16_t color;
    void (*icon)(uint16_t, uint16_t, uint16_t);
} Component;

int main(void) {
    if (lcd_hw_init() < 0) return -1;
    lcd_init();

    Component comps[] = {
        {"Capacitor", 12, BLUE,   draw_cap},
        {"Resistor",   5, GREEN,  draw_res},
        {"Diode",      3, RED,    draw_diode},
        {"Inductor",   2, CYAN,   draw_ind},
        {"LED",        8, YELLOW, draw_led},
    };
    int num = sizeof(comps)/sizeof(comps[0]);
    int total = 0;
    for (int i = 0; i < num; i++) total += comps[i].count;

    // ====== 布局: 128x160, 5行紧凑 ======
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);

    // 标题栏
    lcd_fill(0, 0, LCD_W, 18, DARKBLUE);
    lcd_show_string(4, 2, (const uint8_t *)"COMPONENTS", WHITE, DARKBLUE, 16, 0);
    lcd_draw_line(0, 18, LCD_W, 18, GRAY);

    int row_h = 24;    // 行高
    int y0 = 22;       // 第一行起始Y

    for (int i = 0; i < num; i++) {
        int y = y0 + i * row_h;

        uint16_t bg = (i % 2 == 0) ? BLACK : 0x1082;
        lcd_fill(0, y, LCD_W, y + row_h, bg);

        if (i > 0) lcd_draw_line(8, y, LCD_W - 8, y, GRAY);

        // 数量 (16号字)
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%d", comps[i].count);
        lcd_show_string(8, y + 4, (const uint8_t *)cnt, comps[i].color, bg, 16, 0);

        // 名称
        lcd_show_string(36, y + 4, (const uint8_t *)comps[i].name,
                        comps[i].color, bg, 16, 0);

        // 符号 (右下角)
        if (comps[i].icon) comps[i].icon(106, y + 10, comps[i].color);
    }

    // 底部
    int bot = y0 + num * row_h + 4;
    lcd_draw_line(0, bot, LCD_W, bot, GRAY);
    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %d", total);
    lcd_show_string(4, bot + 4, (const uint8_t *)buf, WHITE, BLACK, 12, 0);

    while (1) sleep(1);
    return 0;
}
