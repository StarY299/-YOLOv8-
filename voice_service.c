/**
 * voice_service.c — WAV 拼接语音播报 (无声间隙版)
 *
 * 参照 ELF_RV1126B 方案: 所有段先拼接成一个 WAV, 再播放一次.
 * sox  + wave 拼接 → 单文件 → aplay, 彻底消除词间间隙.
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

static const char *comp_wav[6] = {
    "resistor", "capacitor", "diode",
    "inductor", "inductor", "inductor"
};

/* 添加 WAV 到拼接列表 (空格分隔路径) */
static void add_file(const char *name)
{
    if (g_nfiles >= MAX_FILES) return;
    int off = (g_nfiles == 0) ? snprintf(g_cmd, CMD_LEN, "python3 /userdata/projects/ai_elc_t/wav_join.py")
                               : strlen(g_cmd);
    snprintf(g_cmd + off, CMD_LEN - off, " %s%s.wav", WAV_DIR, name);
    g_nfiles++;
}

/* 拼接并播放 */
static void play_all(void)
{
    if (g_nfiles == 0) return;
    snprintf(g_cmd + strlen(g_cmd), CMD_LEN - strlen(g_cmd),
             " -t wav - 2>/dev/null | aplay 2>/dev/null");
    system(g_cmd);
    g_cmd[0] = '\0';
    g_nfiles = 0;
}

/* 数字 */
static void add_num(int n)
{
    if (n <= 0) return;
    if (n <= 10) { char name[8]; snprintf(name, sizeof(name), "%d", n); add_file(name); return; }
    if (n < 20) { add_file("10"); if (n > 10) add_num(n - 10); return; }
    if (n < 100) { add_num(n / 10); add_file("10"); if (n % 10) add_num(n % 10); return; }
}

int voice_play(const char *name) { g_cmd[0]=0; g_nfiles=0; add_file(name); play_all(); return 0; }
int voice_ready(void) { return voice_play("ready"); }

int voice_announce(int counts[6], int text_filter,
                   int has_damaged, int has_unknown)
{
    int start = 0, end = 6;
    if (text_filter >= 0 && text_filter <= 2) { start = text_filter; end = text_filter + 1; }

    /* 收集所有段 */
    g_cmd[0] = '\0'; g_nfiles = 0;

    add_file("detect");
    int has_any = 0;
    for (int i = start; i < end && i < 3; i++) {
        if (counts[i] <= 0) continue;
        has_any = 1;
        add_file(comp_wav[i]);
        add_num(counts[i]);
        add_file("ge");
    }

    if (!has_any) { g_cmd[0] = '\0'; g_nfiles = 0; return 0; }

    if (has_damaged) add_file("damaged");

    if (has_unknown) {
        add_file("unknown");
        add_num(counts[7]);
        add_file("ge");
    }

    /* 一次性拼接播放，无缝 */
    play_all();
    return 0;
}
