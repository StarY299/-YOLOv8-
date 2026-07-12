/**
 * voice_service.c — WAV 拼接语音播报 (状态机版)
 *
 * 状态机:
 *   文字过滤模式 (text_filter>=0): 仅播报指定类型
 *   通用模式 (text_filter<0):  R→C→D→三极管 顺序播报全部
 *   缺损播报: 通用模式后, 如有缺损则播报缺损种类+数量
 *
 * 播报顺序 (通用): 电阻→电容→二极管→三极管
 * 播报顺序 (缺损): 缺损电阻→缺损电容→缺损二极管
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "voice_service.h"

#define CMD_LEN  2048
#define MAX_FILES 32

static char g_cmd[CMD_LEN];
static int  g_nfiles;

/* WAV 文件名: 按模型 class ID 索引
 * model: 0=C 1=D 2=T 3=R 4=LED 5=C-dam 6=R-dam 7=text_R 8=text_C 9=text_D 10=D-dam */
static const char *type_wav[] = {
    "capacitor",  /* 0: 电容 */
    "diode",      /* 1: 二极管 */
    "Triode",     /* 2: 三极管 */
    "resistor",   /* 3: 电阻 */
    "inductor",   /* 4: LED (暂无wav, 用inductor替代) */
    "capacitor",  /* 5: C-dam → 报"电容" */
    "resistor",   /* 6: R-dam → 报"电阻" */
    NULL,         /* 7: text_R (不播报) */
    NULL,         /* 8: text_C */
    NULL,         /* 9: text_D */
    "diode",      /*10: D-dam → 报"二极管" */
};

static void add_file(const char *name)
{
    if (g_nfiles >= MAX_FILES || !name) return;
    int off = (g_nfiles == 0) ? snprintf(g_cmd, CMD_LEN, "sox")
                               : strlen(g_cmd);
    snprintf(g_cmd + off, CMD_LEN - off, " %s%s.wav", WAV_DIR, name);
    g_nfiles++;
}

static void play_all(void)
{
    if (g_nfiles == 0) return;
    snprintf(g_cmd + strlen(g_cmd), CMD_LEN - strlen(g_cmd),
             " -t wav - 2>/dev/null | aplay 2>/dev/null");
    system(g_cmd);
    g_cmd[0] = '\0'; g_nfiles = 0;
}

static void add_num(int n)
{
    if (n <= 0) return;
    if (n <= 10) { char name[8]; snprintf(name, sizeof(name), "%d", n); add_file(name); return; }
    if (n < 20) { add_file("10"); if (n > 10) add_num(n - 10); return; }
    if (n < 100) { add_num(n / 10); add_file("10"); if (n % 10) add_num(n % 10); return; }
}

int voice_play(const char *name)
    { g_cmd[0]=0; g_nfiles=0; add_file(name); play_all(); return 0; }

int voice_ready(void) { return voice_play("ready"); }

/* ---- 通用播报: 按顺序逐类 ---- */
static void announce_types(const int counts[12], const int order[],
                           int n, int has_damaged_only)
{
    g_cmd[0] = '\0'; g_nfiles = 0;

    if (has_damaged_only) add_file("damaged");
    else add_file("detect");

    int any = 0;
    for (int i = 0; i < n; i++) {
        int m = order[i];     /* 模型 class ID */
        int c = counts[m];
        if (c <= 0) continue;
        any = 1;
        if (type_wav[m]) add_file(type_wav[m]);
        add_num(c);
        add_file("ge");
    }

    if (any) play_all();
    else { g_cmd[0] = '\0'; g_nfiles = 0; }
}

int voice_announce(const int counts[12], int text_filter, int has_damaged)
{
    fprintf(stderr, "[VOICE] f=%d dam=%d R=%d C=%d D=%d dR=%d dC=%d dD=%d\n",
           text_filter, has_damaged,
           counts[3], counts[0], counts[1],
           counts[6], counts[5], counts[10]);

    if (text_filter >= 0 && text_filter <= 2) {
        /* ===== 文字过滤模式 ===== */
        static const int map[] = {3, 0, 1};
        int m = map[text_filter];
        if (counts[m] > 0) {
            int order[] = {m};
            announce_types(counts, order, 1, 0);
        }
    } else {
        /* ===== 通用模式 ===== */
        int order[] = {3, 0, 1, 2};
        announce_types(counts, order, 4, 0);
    }

    /* ===== 缺损播报 ===== */
    if (has_damaged) {
        usleep(300000);
        int d_order[] = {6, 5, 10};  /* R-dam → C-dam → D-dam */
        announce_types(counts, d_order, 3, 1);
    }

    return 0;
}
