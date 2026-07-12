/**
 * tft_ui.c — 元器件计数 LVGL UI (内置字体版)
 *
 * 128x160 布局 (英文缩写):
 *   ┌──────────────┐
 *   │ COMPONENT AI  │
 *   │              │
 *   │ ■ R    5     │  Resistor
 *   │ ■ C    3     │  Capacitor
 *   │ ■ D    2     │  Diode
 *   │ ■ L    1     │  Inductor
 *   │ ■ LED  2     │
 *   │ ■ IC   0     │
 *   │              │
 *   │ DAMAGED!     │
 *   │ UNK:3        │
 *   │ Flt: R       │
 *   └──────────────┘
 */

#include <stdio.h>
#include <lvgl/lvgl.h>
#include "tft_ui.h"

#define COMP_COUNT  6

static const char *names[COMP_COUNT] = {"R","C","D","L","LED","IC"};

static const lv_color_t colors[COMP_COUNT] = {
    LV_COLOR_MAKE(0x4F,0xF0,0x40), LV_COLOR_MAKE(0x40,0x80,0xFF),
    LV_COLOR_MAKE(0xFF,0x40,0x40), LV_COLOR_MAKE(0xFF,0xFF,0x40),
    LV_COLOR_MAKE(0xFF,0x40,0xFF), LV_COLOR_MAKE(0xFF,0x80,0x40),
};

static const char *filter_names[] = {"Resistor","Capacitor","Diode"};

static lv_obj_t *g_title, *g_dots[COMP_COUNT], *g_labels[COMP_COUNT];
static lv_obj_t *g_status, *g_filter;

static void create_row(int i, lv_obj_t *p)
{
    int y = 20 + i * 20;
    g_dots[i] = lv_obj_create(p);
    lv_obj_set_size(g_dots[i], 10, 10);
    lv_obj_set_pos(g_dots[i], 4, y + 3);
    lv_obj_set_style_bg_color(g_dots[i], colors[i], 0);
    lv_obj_set_style_bg_opa(g_dots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dots[i], 0, 0);
    lv_obj_set_style_radius(g_dots[i], 2, 0);

    g_labels[i] = lv_label_create(p);
    lv_obj_set_pos(g_labels[i], 18, y);
    lv_label_set_text(g_labels[i], "");
}

void tft_ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181830), 0);

    g_title = lv_label_create(scr);
    lv_obj_set_pos(g_title, 0, 2); lv_obj_set_size(g_title, 128, 16);
    lv_label_set_text(g_title, "COMPONENT AI");
    lv_obj_set_style_text_color(g_title, lv_color_hex(0xCCCCFF), 0);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);

    for (int i = 0; i < COMP_COUNT; i++) create_row(i, scr);

    g_status = lv_label_create(scr);
    lv_obj_set_pos(g_status, 2, 136);
    lv_label_set_text(g_status, "OK");

    g_filter = lv_label_create(scr);
    lv_obj_set_pos(g_filter, 2, 150);
    lv_label_set_text(g_filter, "");
    lv_obj_set_style_text_color(g_filter, lv_color_hex(0x6688CC), 0);

    printf("[TFT-UI] init done\n");
}

void tft_ui_update(const int counts[12], int text_filter,
                   int has_damaged, int has_unknown)
{
    for (int i = 0; i < COMP_COUNT; i++) {
        char b[16];
        snprintf(b, sizeof(b), "%s  %d", names[i], counts[i]);
        lv_label_set_text(g_labels[i], b);
        lv_obj_set_style_bg_opa(g_dots[i], counts[i]>0?LV_OPA_COVER:LV_OPA_30, 0);
    }

    char s[64] = "OK";
    if (has_damaged) snprintf(s, sizeof(s), "DAMAGED!");
    if (has_unknown) snprintf(s, sizeof(s), "UNK:%d", counts[7]);
    if (has_damaged && has_unknown) snprintf(s, sizeof(s), "DAM! UNK:%d", counts[7]);
    lv_label_set_text(g_status, s);
    lv_obj_set_style_text_color(g_status,
        (has_damaged||has_unknown) ? lv_color_hex(0xFF4040) : lv_color_hex(0x40FF40), 0);

    if (text_filter >= 0 && text_filter <= 2) {
        char b[32];
        snprintf(b, sizeof(b), "Flt: %s", filter_names[text_filter]);
        lv_label_set_text(g_filter, b);
        lv_obj_clear_flag(g_filter, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_filter, LV_OBJ_FLAG_HIDDEN);
    }
}
