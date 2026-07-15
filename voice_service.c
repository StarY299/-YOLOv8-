/**
 * voice_service.c — 五态语音播报
 * WAIT → JUDGE → TEXT/DAMAGED/GENERAL → WAIT
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

/* type_wav: 按模型 class ID 索引 */
static const char *type_wav[] = {
    "capacitor",  /* 0: 电容 */
    "diode",      /* 1: 二极管 */
    "Triode",     /* 2: 三极管 */
    "resistor",   /* 3: 电阻 */
    "LED",        /* 4: LED */
    "capacitor",  /* 5: C-dam */
    "resistor",   /* 6: R-dam */
    NULL, NULL, NULL,
    "diode",      /*10: D-dam */
    "Pot",        /*11: 电位器 */
    "Connecter",  /*12: 连接器 */
    "Xtal",       /*13: 晶振 */
    "IC",         /*14: 芯片 */
};

static void add_file(const char *name)
{
    if (!name || g_nfiles >= MAX_FILES) return;
    int off = (g_nfiles == 0) ? snprintf(g_cmd, CMD_LEN, "sox")
                               : strlen(g_cmd);
    snprintf(g_cmd + off, CMD_LEN - off, " %s%s.wav", WAV_DIR, name);
    g_nfiles++;
}

static void play_all(void)
{
    if (g_nfiles == 0) return;
    snprintf(g_cmd + strlen(g_cmd), CMD_LEN - strlen(g_cmd),
             " -t wav - | aplay");
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

void voice_unknown_mode(int unknown_count)
{
    if (unknown_count <= 0) return;
    g_cmd[0]=0; g_nfiles=0;
    add_file("unknown");
    add_num(unknown_count);
    add_file("ge");
    play_all();
}

/* ---- 文字模式: 仅播报一种元件 ---- */
void voice_text_mode(int text_filter, const int counts[13])
{
    static const int map[] = {3, 0, 1, 4, 11, 12, 13, 14}; /* 0=R 1=C 2=D 3=LED 4=Pot 5=Con 6=Xtal 7=IC */
    int m = map[text_filter];

    g_cmd[0]=0; g_nfiles=0;
    add_file("mode_text");
    add_file(type_wav[m]);
    play_all();

    if (counts[m] > 0) {
        g_cmd[0]=0; g_nfiles=0;
        add_file("detect");
        add_file(type_wav[m]);
        add_num(counts[m]);
        add_file("ge");
        play_all();
    }
}

/* ---- 缺损模式: 仅播报缺损元件 ---- */
void voice_damaged_mode(const int counts[13])
{
    /* 缺损类: 6=R-dam, 5=C-dam, 10=D-dam */
    int d_order[] = {6, 5, 10};
    int any = 0;

    g_cmd[0]=0; g_nfiles=0;
    add_file("damaged");

    for (int i = 0; i < 3; i++) {
        int m = d_order[i];
        fprintf(stderr, "[VOICE-GEN] cls=%d count=%d\n", m, counts[m]);
        if (counts[m] <= 0) continue;
        any = 1;
        if (type_wav[m]) add_file(type_wav[m]);
        add_num(counts[m]);
        add_file("ge");
    }
    if (any) play_all();
    else { g_cmd[0]=0; g_nfiles=0; }
}

/* ---- 通用模式: 播报全部正常元件 ---- */
void voice_general_mode(const int counts[13])
{
    int order[] = {3, 0, 1, 4, 11, 12, 13, 14};
    int any = 0;

    g_cmd[0]=0; g_nfiles=0;
    add_file("detect");

    for (int i = 0; i < 8; i++) {
        int m = order[i];
        fprintf(stderr, "[GEN] m=%d cnt=%d\n", m, counts[m]);
        if (counts[m] <= 0) continue;
        any = 1;
        if (type_wav[m]) add_file(type_wav[m]);
        add_num(counts[m]);
        add_file("ge");
    }
    fprintf(stderr, "[GEN] any=%d nfiles=%d cmd=%s\n", any, g_nfiles, g_cmd);
    if (any) play_all();
    else { g_cmd[0]=0; g_nfiles=0; }
}
