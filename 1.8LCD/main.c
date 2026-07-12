/**
 * main.c - 1.8寸TFT液晶演示程序 for RV1126B
 * 展示: ASCII多字号 / 绘图 / 颜色填充 / 动态效果
 */
#include "lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>

static volatile int running = 1;
static void sig_handler(int sig) { (void)sig; running = 0; }

int main(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== 1.8 TFT LCD Demo ===\n");

    if (lcd_hw_init() < 0) {
        fprintf(stderr, "HW init failed!\n");
        return -1;
    }
    lcd_init();

    // ====== Demo 1: 基本信息 + 多字号 ======
    printf("[1] Basic info & font sizes\n");
    lcd_fill(0, 0, LCD_W, LCD_H, WHITE);
    lcd_show_string(4, 0,  (const uint8_t *)"LCD_W:", BLUE, WHITE, 16, 0);
    lcd_show_int_num(56, 0, LCD_W, 3, BLUE, WHITE, 16);
    lcd_show_string(4, 20, (const uint8_t *)"LCD_H:", BLUE, WHITE, 16, 0);
    lcd_show_int_num(56, 20, LCD_H, 3, BLUE, WHITE, 16);

    float t = 3.14;
    lcd_show_string(4, 40, (const uint8_t *)"Val:", RED, WHITE, 16, 0);
    lcd_show_float_num(40, 40, t, 4, RED, WHITE, 16);

    lcd_show_string(4, 65,  (const uint8_t *)"Size12:Hello", GREEN, WHITE, 12, 0);
    lcd_show_string(4, 80,  (const uint8_t *)"Size16:Hello", BLUE, WHITE, 16, 0);
    lcd_show_string(4, 100, (const uint8_t *)"Size24:Hi!", RED, WHITE, 24, 0);
    lcd_show_string(4, 128, (const uint8_t *)"Size32:WOW", MAGENTA, WHITE, 32, 0);
    sleep(3); if (!running) goto exit;

    // ====== Demo 2: 绘图函数 ======
    printf("[2] Drawing: rect/line/circle\n");
    lcd_fill(0, 0, LCD_W, LCD_H, WHITE);

    // 矩形
    lcd_draw_rectangle(5, 5, 55, 55, RED);
    lcd_draw_rectangle(10, 10, 50, 50, BLUE);

    // X形线
    lcd_draw_line(5, 5, 55, 55, GREEN);
    lcd_draw_line(55, 5, 5, 55, GREEN);

    // 同心圆
    lcd_draw_circle(90, 30, 25, RED);
    lcd_draw_circle(90, 30, 18, YELLOW);
    lcd_draw_circle(90, 30, 10, BLUE);

    // 彩色方块
    lcd_fill(70, 65, 90, 85, RED);
    lcd_fill(95, 65, 115, 85, GREEN);
    lcd_fill(70, 90, 90, 110, BLUE);
    lcd_fill(95, 90, 115, 110, YELLOW);

    // 点阵
    for (int i = 0; i < 50; i++) {
        lcd_draw_point(5 + i*2, 125, (i%2) ? RED : BLUE);
        lcd_draw_point(5 + i*2, 130, (i%3==0) ? GREEN : MAGENTA);
    }

    lcd_show_string(5, 140, (const uint8_t *)"Draw OK", DARKBLUE, WHITE, 16, 0);
    sleep(3); if (!running) goto exit;

    // ====== Demo 3: 颜色填充 ======
    printf("[3] Color fills\n");
    uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, BLACK, WHITE};
    const char *names[] = {"RED","GREEN","BLUE","YELLOW","CYAN","MAGENTA","BLACK","WHITE"};
    for (int c = 0; c < 8 && running; c++) {
        lcd_fill(0, 0, LCD_W, LCD_H, colors[c]);
        uint16_t tc = (colors[c]==BLACK||colors[c]==BLUE) ? WHITE : BLACK;
        lcd_show_string(30, 65, (const uint8_t *)names[c], tc, colors[c], 24, 0);
        usleep(800000);
    }

    // ====== Demo 4: 动态效果 ======
    printf("[4] Animation: moving circle\n");
    lcd_fill(0, 0, LCD_W, LCD_H, WHITE);
    for (int x = 20; x < 110 && running; x += 5) {
        lcd_fill(0, 0, LCD_W, LCD_H, WHITE);
        lcd_draw_circle(x, 40, 15, RED);
        lcd_show_string(4, 80, (const uint8_t *)"Moving...", DARKBLUE, WHITE, 16, 0);
        usleep(50000);
    }
    sleep(1); if (!running) goto exit;

    // ====== Demo 5: 全屏信息 ======
    printf("[5] Summary\n");
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);
    lcd_show_string(10, 10, (const uint8_t *)"1.8 TFT LCD", GREEN, BLACK, 16, 0);
    lcd_show_string(10, 35, (const uint8_t *)"ST7735S", CYAN, BLACK, 16, 0);
    lcd_show_string(10, 60, (const uint8_t *)"128 x 160", YELLOW, BLACK, 16, 0);
    lcd_show_string(10, 85, (const uint8_t *)"SPI Mode 3", MAGENTA, BLACK, 12, 0);
    lcd_show_string(10, 100, (const uint8_t *)"8MHz", MAGENTA, BLACK, 12, 0);
    lcd_show_string(10, 120, (const uint8_t *)"RV1126B OK", WHITE, BLACK, 16, 0);

    // 画个简单的电路符号
    lcd_draw_line(90, 140, 120, 140, RED);
    lcd_draw_line(120, 140, 120, 130, RED);
    lcd_draw_line(120, 130, 90, 130, RED);
    lcd_draw_line(90, 130, 90, 140, RED);
    lcd_show_string(90, 145, (const uint8_t *)"1.0", RED, BLACK, 12, 0);

    sleep(3);

exit:
    printf("Exiting...\n");
    lcd_fill(0, 0, LCD_W, LCD_H, BLACK);
    lcd_backlight_off();
    lcd_hw_deinit();
    return 0;
}
