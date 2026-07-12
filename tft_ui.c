/**
 * tft_ui.c — 元器件计数显示 (基于 lcd.c 驱动)
 *
 * 128x160 布局:
 *   ┌──────────────────┐
 *   │  COMPONENT AI    │  标题 16px
 *   │                  │
 *   │ ● R      5       │  色块 + 名称 + 数量
 *   │ ● C      3       │
 *   │ ● D      2       │
 *   │ ● L      1       │
 *   │ ● LED    2       │
 *   │ ● IC     0       │
 *   │                  │
 *   │ DAMAGED!  UNK:3  │  状态行
 *   │ Flt: Resistor    │  过滤模式
 *   └──────────────────┘
 */

#include <stdio.h>
#include "lcd.h"
#include "tft_ui.h"

static const char *names[] = {"R","C","D","L","LED","IC"};
static const uint16_t colors[] = {GREEN, BLUE, RED, YELLOW, MAGENTA, 0xFD20};

void tft_ui_init(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, 0x18E3);
    lcd_show_string(20, 2, (const uint8_t *)"COMPONENT AI",
                    WHITE, 0x18E3, 16, 0);
    printf("[TFT-UI] init\n");
}

void tft_ui_update(const int counts[12], int text_filter,
                   int has_damaged, int has_unknown)
{
    char buf[16];

    for (int i = 0; i < 6; i++) {
        int y = 22 + i * 18;

        /* 色块 */
        uint16_t c = counts[i] > 0 ? colors[i] : 0x4208;
        lcd_fill(4, y+2, 12, y+10, c);

        /* 名称 + 数量 */
        snprintf(buf, sizeof(buf), "%-3s %d", names[i], counts[i]);
        lcd_show_string(16, y, (const uint8_t *)buf, WHITE, 0x18E3, 16, 0);
    }

    /* 状态行 */
    char status[32];
    int off = 0;
    if (has_damaged) {
        off += snprintf(status + off, sizeof(status) - off, "DAM!");
        lcd_show_string(2, 136, (const uint8_t *)status, RED,   0x18E3, 12, 0);
    }
    if (has_unknown) {
        snprintf(status, sizeof(status), "UNK:%d", counts[7]);
        lcd_show_string(2, 148, (const uint8_t *)status, YELLOW, 0x18E3, 12, 0);
    }
    if (!has_damaged && !has_unknown) {
        lcd_show_string(2, 142, (const uint8_t *)"OK", GREEN, 0x18E3, 12, 0);
    } else {
        /* 清除 OK 区域 */
        lcd_fill(2, 142, 30, 152, 0x18E3);
    }

    /* 过滤模式 */
    static const char *fnames[] = {"Resistor","Capacitor","Diode"};
    if (text_filter >= 0 && text_filter <= 2) {
        snprintf(buf, sizeof(buf), "Flt:%s", fnames[text_filter]);
        lcd_show_string(80, 148, (const uint8_t *)buf, 0x6688, 0x18E3, 12, 0);
    } else {
        lcd_fill(80, 148, 128, 160, 0x18E3);
    }
}
