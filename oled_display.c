/**
 * oled_display.c — OLED 元器件计数显示 (128x64)
 *
 * 页面0: 元器件计数 (主界面, 每2秒刷新)
 * 页面1: 过滤模式 (检测到文字时自动切换, 保持5秒)
 */
#include <stdio.h>
#include <string.h>
#include "oled.h"
#include "oled_display.h"

#define STATUS_HOLD_SEC  5   /* 过滤模式页面保持秒数 */

static int  g_show_filter = 0;
static int  g_filter_timer = 0;

int oled_display_init(void)
{
    OLED_Init();
    OLED_DisPlay_On();
    OLED_NewFrame();
    OLED_PrintASCIIString(0, 0, "ELF-RV1126B", &afont16x8, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 2, "Component AI", &afont16x8, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 4, "Starting...", &afont8x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
    printf("[OLED] Init OK\n");
    return 0;
}

void oled_display_update_components(const int counts[12], int text_filter,
                                     int has_damaged, int has_unknown)
{
    char line[32];

    /* 触发过滤模式页面: 检测到文字时自动切换 */
    if (text_filter >= 0 && text_filter <= 2) {
        g_show_filter = 1;
        g_filter_timer = STATUS_HOLD_SEC * 2;  /* 2次/秒更新, 保持5秒 */
    }

    OLED_NewFrame();

    if (g_show_filter) {
        /* ---- 页面1: 过滤模式 ---- */
        static const char *filter_names[] = {"Resistor", "Capacitor", "Diode"};
        const char *fname = (text_filter >= 0 && text_filter <= 2)
                            ? filter_names[text_filter] : "None";

        snprintf(line, sizeof(line), "Filter: %s", fname);
        OLED_PrintASCIIString(0, 0, line, &afont16x8, OLED_COLOR_REVERSED);

        /* 仅显示被过滤类型的数量 */
        int filtered_count = 0;
        if (text_filter == 0)      filtered_count = counts[0];  /* resistor */
        else if (text_filter == 1) filtered_count = counts[1];  /* capacitor */
        else if (text_filter == 2) filtered_count = counts[2];  /* diode */

        snprintf(line, sizeof(line), "Count: %d pcs", filtered_count);
        OLED_PrintASCIIString(0, 16, line, &afont16x8, OLED_COLOR_NORMAL);

        snprintf(line, sizeof(line), "Total: R%d C%d D%d",
                 counts[0], counts[1], counts[2]);
        OLED_PrintASCIIString(0, 32, line, &afont8x6, OLED_COLOR_NORMAL);

        snprintf(line, sizeof(line), "L%d LED%d IC%d",
                 counts[3], counts[4], counts[5]);
        OLED_PrintASCIIString(0, 44, line, &afont8x6, OLED_COLOR_NORMAL);

        if (has_damaged)
            OLED_PrintASCIIString(0, 56, "DAMAGED!", &afont8x6, OLED_COLOR_REVERSED);
        else if (has_unknown)
            OLED_PrintASCIIString(0, 56, "UNKNOWN!", &afont8x6, OLED_COLOR_REVERSED);

        if (--g_filter_timer <= 0) g_show_filter = 0;
    } else {
        /* ---- 页面0: 元器件计数主界面 ---- */
        OLED_PrintASCIIString(0, 0, "COMPONENT COUNT", &afont16x8, OLED_COLOR_NORMAL);

        /* 第一行: 基本元件 R/C/D */
        snprintf(line, sizeof(line), "R:%-2d C:%-2d D:%-2d",
                 counts[0], counts[1], counts[2]);
        OLED_PrintASCIIString(0, 16, line, &afont16x8, OLED_COLOR_NORMAL);

        /* 第二行: 扩展元件 */
        snprintf(line, sizeof(line), "L:%-2d LED:%-2d IC:%-2d",
                 counts[3], counts[4], counts[5]);
        OLED_PrintASCIIString(0, 32, line, &afont16x8, OLED_COLOR_NORMAL);

        /* 第三行: 状态标志 */
        snprintf(line, sizeof(line), "%s%s",
                 has_damaged ? "DAM " : "",
                 has_unknown ? "UNK" : "");
        if (line[0] == '\0')
            snprintf(line, sizeof(line), "Status: Normal");
        OLED_PrintASCIIString(0, 48, line, &afont8x6, OLED_COLOR_NORMAL);

        /* 文字过滤指示 (如果有) */
        if (text_filter >= 0) {
            static const char *fn[] = {"R","C","D"};
            snprintf(line, sizeof(line), "Fltr:%s", fn[text_filter]);
            OLED_PrintASCIIString(100, 48, line, &afont8x6, OLED_COLOR_NORMAL);
        }
    }

    OLED_ShowFrame();
}

void oled_display_deinit(void)
{
    OLED_NewFrame();
    OLED_ShowFrame();
    OLED_DisPlay_Off();
    printf("[OLED] Deinit\n");
}
