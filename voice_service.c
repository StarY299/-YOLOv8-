/**
 * voice_service.c — 语音播报服务实现
 *
 * 使用 aplay 播放预录制 WAV 文件实现语音播报.
 * 播报通过 system() 串行调用 aplay 实现 WAV 拼接.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "voice_service.h"

/* 类别对应的 WAV 文件名前缀 */
static const char *type_file[] = {
    "resistor",   // 0 电阻
    "capacitor",  // 1 电容
    "diode",      // 2 二极管
    "inductor",   // 3 电感
    "LED",        // 4 发光二极管
    "IC_chip",    // 5 芯片
};

/**
 * 播放单个 WAV 文件 (同步, 阻塞)
 */
int play_voice(const char *voice_file)
{
    char cmd[512];

    if (voice_file == NULL) {
        printf("[VOICE] voice_file is NULL\n");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "aplay -q %s 2>/dev/null", voice_file);
    printf("[VOICE] ▶ %s\n", voice_file);

    return system(cmd);
}

/**
 * 播报系统就绪
 */
int announce_system_ready(const char *voice_dir)
{
    char path[256];
    snprintf(path, sizeof(path), "%ssystem_ready.wav", voice_dir);
    return play_voice(path);
}

/**
 * 播报单个数字 (0-999)
 * 如 15 → "10.wav" + "5.wav",  100 → "100.wav"
 */
static int speak_number(int n, const char *voice_dir)
{
    char path[256];

    if (n < 0 || n > 999) return -1;

    /* 简化: 对于 20 以内的数字直接播放 */
    if (n <= 20) {
        snprintf(path, sizeof(path), "%s%d.wav", voice_dir, n);
        return play_voice(path);
    }

    /* 21-99: 十位 + 个位 */
    if (n < 100) {
        int tens = (n / 10) * 10;
        int ones = n % 10;
        snprintf(path, sizeof(path), "%s%d.wav", voice_dir, tens);
        play_voice(path);
        if (ones > 0) {
            snprintf(path, sizeof(path), "%s%d.wav", voice_dir, ones);
            play_voice(path);
        }
        return 0;
    }

    /* 100-999: 百位 + "百" + rest */
    int hundreds = n / 100;
    snprintf(path, sizeof(path), "%s%d.wav", voice_dir, hundreds);
    play_voice(path);
    snprintf(path, sizeof(path), "%s100.wav", voice_dir);  /* "百" 的录音文件命名为 100.wav */
    play_voice(path);
    int rest = n % 100;
    if (rest > 0) speak_number(rest, voice_dir);
    return 0;
}

/**
 * 播报元器件计数
 */
int announce_components(const int counts[12], int text_filter,
                        int has_damaged, int has_unknown,
                        const char *voice_dir)
{
    char path[256];
    int has_any = 0;

    /* ---- 1. "检测到" ---- */
    snprintf(path, sizeof(path), "%sdetect.wav", voice_dir);
    play_voice(path);

    /* ---- 2. 逐类播报 ---- */
    /* 文字过滤模式下, 仅播报对应类型 */
    int start = 0, end = 6;  /* 默认播报全部6种元件 */
    if (text_filter >= 0 && text_filter <= 2) {
        start = text_filter;
        end   = text_filter + 1;
    }

    for (int i = start; i < end; i++) {
        if (counts[i] <= 0) continue;
        has_any = 1;

        /* "电阻" / "电容" / ... */
        snprintf(path, sizeof(path), "%s%s.wav", voice_dir, type_file[i]);
        play_voice(path);

        /* 数量 */
        speak_number(counts[i], voice_dir);

        /* "个" */
        snprintf(path, sizeof(path), "%sge.wav", voice_dir);
        play_voice(path);

        usleep(100000);  /* 100ms 间隔 */
    }

    /* 如果没有任何元件 */
    if (!has_any) {
        snprintf(path, sizeof(path), "%sno_target.wav", voice_dir);
        play_voice(path);
    }

    /* ---- 3. 缺损告警 ---- */
    if (has_damaged) {
        usleep(200000);
        snprintf(path, sizeof(path), "%sdamaged.wav", voice_dir);
        play_voice(path);
    }

    /* ---- 4. 未知元件 ---- */
    if (has_unknown) {
        usleep(200000);
        snprintf(path, sizeof(path), "%sunknown.wav", voice_dir);
        play_voice(path);
        speak_number(counts[7], voice_dir);  /* CLS_UNKNOWN = 7 */
        snprintf(path, sizeof(path), "%sge.wav", voice_dir);
        play_voice(path);
    }

    return 0;
}
