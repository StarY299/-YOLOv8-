/**
 * button.c — TM1650 4x4 矩阵按键 (I2C-4, 0x24)
 * 按键按下立即返回, 不区分长短按
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "button.h"

#define TM1650_ADDR  0x24
#define I2C_DEV      "/dev/i2c-4"

static int g_fd = -1;
static int g_last_key = -1;

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
    printf("[BTN] TM1650 ready (I2C-4, 0x%02X)\n", TM1650_ADDR);
    return 0;
}

int button_read(void)
{
    if (g_fd < 0) return BTN_NONE;

    /* TM1650: 先写读键命令 0x49, 再读键值 */
    uint8_t rcmd = 0x49;
    if (write(g_fd, &rcmd, 1) != 1) return BTN_NONE;

    uint8_t key;
    if (read(g_fd, &key, 1) != 1) return BTN_NONE;

    int idx = code_to_idx(key);
    if (idx < 0) {
        /* 按键释放 */
        return BTN_NONE;
    }

    /* 去抖: 同一按键连续只报一次 */
    if (idx == g_last_key) return BTN_NONE;
    g_last_key = idx;
    printf("[BTN] key %d pressed\n", idx);
    return BTN_SHORT;
}

int button_key(void) { return g_last_key; }

void button_deinit(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}
