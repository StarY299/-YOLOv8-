/**
 * tft_ui.c — 元器件计数 LVGL UI 实现
 *
 * 128x160 屏幕布局:
 *   ┌──────────────────┐
 *   │  元器件识别系统    │  标题
 *   ├──────────────────┤
 *   │ ■ 电阻      5    │  6行元件计数
 *   │ ■ 电容      3    │
 *   │ ■ 二极管    2    │
 *   │ ■ 电感      1    │
 *   │ ■ LED       2    │
 *   │ ■ IC芯片    0    │
 *   ├──────────────────┤
 *   │ ⚠缺损 ?未知3     │  状态行
 *   │ [仅统计:电阻]    │  过滤提示
 *   └──────────────────┘
 */

#include <stdio.h>
#include <lvgl/lvgl.h>
#include "tft_ui.h"

#define COMP_COUNT  6

/* 元件名称 */
static const char *comp_names[COMP_COUNT] = {
    "电阻", "电容", "二极管", "电感", "LED", "IC"
};

/* 元件颜色 */
static const lv_color_t comp_colors[COMP_COUNT] = {
    LV_COLOR_MAKE(0x4F, 0xF0, 0x40),  /* 电阻 绿  */
    LV_COLOR_MAKE(0x40, 0x80, 0xFF),  /* 电容 蓝  */
    LV_COLOR_MAKE(0xFF, 0x40, 0x40),  /* 二极管 红*/
    LV_COLOR_MAKE(0xFF, 0xFF, 0x40),  /* 电感 黄  */
    LV_COLOR_MAKE(0xFF, 0x40, 0xFF),  /* LED 紫   */
    LV_COLOR_MAKE(0xFF, 0x80, 0x40),  /* IC  橙   */
};

/* UI 控件句柄 */
static lv_obj_t *g_title;
static lv_obj_t *g_dots[COMP_COUNT];
static lv_obj_t *g_labels[COMP_COUNT];
static lv_obj_t *g_status;
static lv_obj_t *g_filter;

/* ============================================================
 *  内部: 创建单个元件行
 * ============================================================ */
static void create_row(int idx, lv_obj_t *parent)
{
    int y = 22 + idx * 18;

    /* 色块 */
    g_dots[idx] = lv_obj_create(parent);
    lv_obj_set_size(g_dots[idx], 8, 8);
    lv_obj_set_pos(g_dots[idx], 4, y + 4);
    lv_obj_set_style_bg_color(g_dots[idx], comp_colors[idx], 0);
    lv_obj_set_style_bg_opa(g_dots[idx], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dots[idx], 0, 0);
    lv_obj_set_style_radius(g_dots[idx], 2, 0);

    /* 名称 + 数量标签 */
    g_labels[idx] = lv_label_create(parent);
    lv_obj_set_pos(g_labels[idx], 16, y);
    lv_label_set_text(g_labels[idx], "");
}

/* ============================================================
 *  公共 API
 * ============================================================ */
void tft_ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 背景: 深色 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x181830), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 标题 */
    g_title = lv_label_create(scr);
    lv_obj_set_pos(g_title, 0, 2);
    lv_obj_set_size(g_title, 128, 18);
    lv_label_set_text(g_title, "元器件识别");
    lv_obj_set_style_text_color(g_title, lv_color_hex(0xCCCCFF), 0);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);

    /* 6 行元件计数 */
    for (int i = 0; i < COMP_COUNT; i++) {
        create_row(i, scr);
    }

    /* 状态行 */
    g_status = lv_label_create(scr);
    lv_obj_set_pos(g_status, 2, 136);
    lv_obj_set_size(g_status, 124, 18);
    lv_label_set_text(g_status, "");
    lv_obj_set_style_text_color(g_status, lv_color_hex(0x888888), 0);

    /* 过滤提示 */
    g_filter = lv_label_create(scr);
    lv_obj_set_pos(g_filter, 2, 148);
    lv_obj_set_size(g_filter, 124, 12);
    lv_label_set_text(g_filter, "");
    lv_obj_set_style_text_color(g_filter, lv_color_hex(0x6688CC), 0);

    printf("[TFT-UI] init done\n");
}

void tft_ui_update(const int counts[12], int text_filter,
                   int has_damaged, int has_unknown)
{
    /* 更新每行: "名称  数量" */
    for (int i = 0; i < COMP_COUNT; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s  %d", comp_names[i], counts[i]);
        lv_label_set_text(g_labels[i], buf);

        /* 有计数时加亮色块 */
        lv_obj_set_style_bg_opa(g_dots[i],
            counts[i] > 0 ? LV_OPA_COVER : LV_OPA_30, 0);
    }

    /* 状态行 */
    {
        char status[64];
        int off = 0;

        if (has_damaged) {
            off += snprintf(status + off, sizeof(status) - off,
                            "!缺损 ");
            lv_obj_set_style_text_color(g_status, lv_color_hex(0xFF4040), 0);
        }
        if (has_unknown) {
            off += snprintf(status + off, sizeof(status) - off,
                            "?未知%d ", counts[7]);
            lv_obj_set_style_text_color(g_status, lv_color_hex(0xFFFF40), 0);
        }
        if (!has_damaged && !has_unknown) {
            snprintf(status, sizeof(status), "正常");
            lv_obj_set_style_text_color(g_status, lv_color_hex(0x40FF40), 0);
        }
        lv_label_set_text(g_status, status);
    }

    /* 过滤模式 */
    if (text_filter >= 0 && text_filter <= 2) {
        char buf[32];
        const char *fn[] = {"电阻", "电容", "二极管"};
        snprintf(buf, sizeof(buf), "仅统计: %s", fn[text_filter]);
        lv_label_set_text(g_filter, buf);
        lv_obj_clear_flag(g_filter, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_filter, "");
        lv_obj_add_flag(g_filter, LV_OBJ_FLAG_HIDDEN);
    }
}
