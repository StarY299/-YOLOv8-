/**
 * lv_conf.h — LVGL v8 最小化配置 (128x160 TFT)
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16         /* RGB565 */
#define LV_COLOR_16_SWAP        0
#define LV_HOR_RES_MAX          128
#define LV_VER_RES_MAX          160

#define LV_MEM_SIZE             (32 * 1024) /* 32KB 内部内存 */
#define LV_MEM_CUSTOM           0

#define LV_TICK_CUSTOM          0          /* 使用默认 tick, 主循环调 lv_tick_inc() */

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      0
#define LV_USE_ASSERT_MALLOC    0

/* 只启用需要的 Widget */
#define LV_USE_ARC              0
#define LV_USE_BAR              0
#define LV_USE_BTN              0
#define LV_USE_BTNMATRIX        0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMG              1          /* animimg 依赖, 必须开 */
#define LV_USE_LABEL            1
#define LV_USE_LINE             0
#define LV_USE_ROLLER           0
#define LV_USE_SLIDER           0
#define LV_USE_SWITCH           0
#define LV_USE_TEXTAREA         0
#define LV_USE_TABLE            0
#define LV_USE_ANIM             0
#define LV_USE_TABVIEW          0
#define LV_USE_WIN              0
#define LV_USE_MSGBOX           0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_KEYBOARD         0
#define LV_USE_LIST             0
#define LV_USE_LED              0
#define LV_USE_CHART            0
#define LV_USE_CALENDAR         0

/* 其他特性 */
#define LV_USE_GROUP            0
#define LV_USE_GPU              0
#define LV_USE_FS               0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_SNAPSHOT         0
#define LV_USE_MONKEY           0
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_BASIC      0
#define LV_USE_THEME_MONO       0

#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_12   1

#define LV_USE_FREETYPE         0

#endif
