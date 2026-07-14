/**
 * button.c — TM1650 4x4 矩阵按键 (I2C-4, 0x24)
 *
 * 去抖状态机:
 *   IDLE → (检测到按键, 启动30ms计时) → DEBOUNCE
 *        → (30ms内持续稳定) → PRESSED → 上报事件
 *        → (按键释放, 启动50ms计时) → RELEASING
 *        → (50ms内持续无按键) → IDLE (允许下次触发)
 *
 * I2C 读取失败时自动重试 3 次.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/i2c-dev.h>
#include "button.h"

#define TM1650_ADDR  0x24
#define I2C_DEV      "/dev/i2c-4"
#define DEBOUNCE_DOWN_MS  30   /* 按下消抖时间 */
#define DEBOUNCE_UP_MS    50   /* 释放消抖时间 */
#define I2C_RETRY         3    /* I2C 读失败重试次数 */

/* 去抖状态 */
typedef enum {
    STATE_IDLE,       /* 无按键, 等待按下 */
    STATE_DEBOUNCE,   /* 检测到按下, 消抖中 */
    STATE_PRESSED,    /* 确认按下, 已上报事件, 等待释放 */
    STATE_RELEASING,  /* 检测到释放, 消抖中 */
} debounce_state_t;

static int g_fd = -1;
static debounce_state_t g_state = STATE_IDLE;
static int  g_pending_key = -1;   /* 消抖中的候选键值 */
static int  g_event_key   = -1;   /* 已确认的按键事件 (待消费) */
static struct timespec g_debounce_start;  /* 消抖计时起点 */

/* TM1650 键码 → 逻辑键号 (0-15) */
static int code_to_idx(uint8_t code)
{
    if (code == 0) return -1;
    static const uint8_t m[16] = {
        0x44,0x45,0x46,0x47, 0x4C,0x4D,0x4E,0x4F,
        0x54,0x55,0x56,0x57, 0x5C,0x5D,0x5E,0x5F,
    };
    for (int i = 0; i < 16; i++) if (m[i] == code) return i;
    return -1;
}

/* 获取自 debounce_start 以来的毫秒数 */
static int64_t debounce_elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - g_debounce_start.tv_sec) * 1000LL
         + (now.tv_nsec - g_debounce_start.tv_nsec) / 1000000LL;
}

/* 读 TM1650 原始键码, 失败自动重试, 返回 -1 表示读不到 */
static int read_raw_key(void)
{
    for (int retry = 0; retry < I2C_RETRY; retry++) {
        uint8_t rcmd = 0x49;
        if (write(g_fd, &rcmd, 1) != 1) {
            usleep(2000);
            continue;
        }
        uint8_t key;
        if (read(g_fd, &key, 1) != 1) {
            usleep(2000);
            continue;
        }
        return code_to_idx(key);  /* -1 = 无按键或无效码 */
    }
    return -1;  /* 全部重试失败 */
}

int button_init(int gpio_pin)
{
    (void)gpio_pin;
    g_fd = open(I2C_DEV, O_RDWR);
    if (g_fd < 0) { perror("[BTN] i2c"); return -1; }
    if (ioctl(g_fd, I2C_SLAVE, TM1650_ADDR) < 0) {
        perror("[BTN] addr"); close(g_fd); g_fd = -1; return -1;
    }
    uint8_t cmd = 0x01;
    write(g_fd, &cmd, 1);
    g_state = STATE_IDLE;
    g_pending_key = -1;
    g_event_key   = -1;
    printf("[BTN] TM1650 ready (I2C-4, 0x%02X, debounce=%d/%dms)\n",
           TM1650_ADDR, DEBOUNCE_DOWN_MS, DEBOUNCE_UP_MS);
    return 0;
}

int button_read(void)
{
    if (g_fd < 0) return BTN_NONE;

    int raw = read_raw_key();

    switch (g_state) {

    case STATE_IDLE:
        if (raw >= 0) {
            /* 检测到按键, 开始消抖 */
            g_state = STATE_DEBOUNCE;
            g_pending_key = raw;
            clock_gettime(CLOCK_MONOTONIC, &g_debounce_start);
        }
        return BTN_NONE;

    case STATE_DEBOUNCE:
        if (raw == g_pending_key) {
            /* 键值持续稳定, 等待消抖期满 */
            if (debounce_elapsed_ms() >= DEBOUNCE_DOWN_MS) {
                g_state = STATE_PRESSED;
                g_event_key = g_pending_key;
                return BTN_SHORT;  /* 上报按键事件 */
            }
        } else if (raw < 0) {
            /* 键值消失, 抖动, 回 IDLE */
            g_state = STATE_IDLE;
            g_pending_key = -1;
        } else {
            /* 键值变了, 重新计时 */
            g_pending_key = raw;
            clock_gettime(CLOCK_MONOTONIC, &g_debounce_start);
        }
        return BTN_NONE;

    case STATE_PRESSED:
        if (raw < 0) {
            /* 检测到释放, 开始释放消抖 */
            g_state = STATE_RELEASING;
            clock_gettime(CLOCK_MONOTONIC, &g_debounce_start);
        }
        /* 如果还是同一个键, 继续等待释放 (防连发) */
        return BTN_NONE;

    case STATE_RELEASING:
        if (raw == g_pending_key) {
            /* 又检测到同一键, 回退到 PRESSED */
            g_state = STATE_PRESSED;
        } else if (raw < 0) {
            /* 持续无按键, 等待消抖期满 */
            if (debounce_elapsed_ms() >= DEBOUNCE_UP_MS) {
                g_state = STATE_IDLE;
                g_pending_key = -1;
                g_event_key   = -1;
            }
        } else {
            /* 出现了不同的键, 当作新按键, 重新消抖 */
            g_state = STATE_DEBOUNCE;
            g_pending_key = raw;
            g_event_key   = -1;
            clock_gettime(CLOCK_MONOTONIC, &g_debounce_start);
        }
        return BTN_NONE;
    }

    return BTN_NONE;
}

int button_key(void)
{
    return g_event_key;
}

void button_reset(void)
{
    g_state = STATE_IDLE;
    g_pending_key = -1;
    g_event_key   = -1;
}

void button_deinit(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    printf("[BTN] deinit\n");
}
