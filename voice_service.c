/**
 * voice_service.c — WAV 拼接语音播报
 *
 * 预录制 WAV 文件放在 /userdata/tts/wavs/ 目录.
 * 播报时用 aplay 依次播放, 简单可靠.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice_service.h"

/* 元件 WAV 文件名 (索引 0-5) */
static const char *comp_wav[] = {
    "resistor",    /* 0 */
    "capacitor",   /* 1 */
    "diode",       /* 2 */
    "inductor",    /* 3 */
    "LED",         /* 4 — 暂无wav, 跳过 */
    "IC_chip",     /* 5 — 暂无wav, 跳过 */
};

/* 数字 WAV: 1.wav~10.wav (不存在则跳过) */

int voice_play(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "%s%s.wav", WAV_DIR, name);
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "aplay -q -D plughw:0,0 %s 2>/dev/null", path);
    return system(cmd);
}

/* 播放数字 N (1-99), 支持组合: 15="10.wav"+"5.wav" */
static void speak_num(int n)
{
    if (n <= 0) return;
    if (n <= 10) {
        char name[8];
        snprintf(name, sizeof(name), "%d", n);
        voice_play(name);
        return;
    }
    if (n < 20) {
        voice_play("10");
        if (n > 10) speak_num(n - 10);
        return;
    }
    if (n < 100) {
        int tens = n / 10;
        speak_num(tens);
        voice_play("10");
        int ones = n % 10;
        if (ones > 0) speak_num(ones);
        return;
    }
}

int voice_ready(void)
{
    voice_play("ready");
    return 0;
}

int voice_announce(const int counts[6], int text_filter,
                   int has_damaged, int has_unknown)
{
    int has_any = 0;

    /* 文字过滤: 仅播报指定类型 */
    int start = 0, end = 6;
    if (text_filter >= 0 && text_filter <= 2) {
        start = text_filter;
        end   = text_filter + 1;
    }

    /* "检测到" */
    voice_play("detect");

    for (int i = start; i < end; i++) {
        if (counts[i] <= 0) continue;
        has_any = 1;

        /* "电阻" / "电容" / ... */
        if (i < 3) voice_play(comp_wav[i]);  /* R/C/D 有wav */
        /* TODO: LED/IC_chip 暂无wav */

        /* 数量 */
        speak_num(counts[i]);

        /* "个" */
        voice_play("ge");
    }

    if (!has_any)
        return 0;  /* 不播 "未检测到", 需要时加 */

    /* 缺损告警 */
    if (has_damaged)
        voice_play("damaged");

    /* 未知元件 */
    if (has_unknown) {
        voice_play("unknown");
        speak_num(counts[7]);
        voice_play("ge");
    }

    return 0;
}
