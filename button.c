/**
 * button.c — TM1650 4x4 矩阵按键 Linux 驱动
 * I2C-4, 地址 0x24
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

static uint8_t code_to_idx(uint8_t code);

#define TM1650_ADDR  0x24
#define I2C_DEV      "/dev/i2c-4"

static int g_fd = -1;

/* 按键码→索引映射 (4x4: 0x44-0x47, 0x4C-0x4F, 0x54-0x57, 0x5C-0x5F) */
static uint8_t code_to_idx(uint8_t code)
{
    if (code == 0) return 0xFF;
    static const uint8_t m[16] = {
        0x44,0x45,0x46,0x47, 0x4C,0x4D,0x4E,0x4F,
        0x54,0x55,0x56,0x57, 0x5C,0x5D,0x5E,0x5F,
    };
    for (int i = 0; i < 16; i++) if (m[i] == code) return i;
    return 0xFF;
}

int button_init(int gpio_pin)
{
    (void)gpio_pin; /* TM1650 I2C, 不使用 GPIO */
    g_fd = open(I2C_DEV, O_RDWR);
    if (g_fd < 0) { perror("[BTN] i2c open"); return -1; }
    if (ioctl(g_fd, I2C_SLAVE, TM1650_ADDR) < 0) {
        perror("[BTN] i2c addr"); close(g_fd); g_fd = -1; return -1;
    }

    /* 开启 TM1650 显示/扫描 */
    uint8_t cmd = 0x01;
    if (write(g_fd, &cmd, 1) != 1) {
        fprintf(stderr, "[BTN] init cmd failed\n");
    }

    printf("[BTN] TM1650 init OK (I2C-4, 0x%02X)\n", TM1650_ADDR);
    return 0;
}

int button_read(void)
{
    if (g_fd < 0) return BTN_NONE;

    uint8_t key;
    if (read(g_fd, &key, 1) != 1) return BTN_NONE;
    if (key == 0) return BTN_NONE;

    uint8_t idx = code_to_idx(key);
    if (idx >= 16) return BTN_NONE;

    printf("[BTN] key %d pressed (code 0x%02X)\n", idx, key);

    /* 简化: 直接返回短按, 长按由上层判断 */
    static int last_key = -1;
    static int hold_cnt = 0;

    if (idx == last_key) {
        hold_cnt++;
        if (hold_cnt > 60) { hold_cnt = 0; last_key = -1; return BTN_LONG; }
        return BTN_NONE;
    } else {
        int ret = (last_key >= 0) ? BTN_SHORT : BTN_NONE;
        last_key = idx;
        hold_cnt = 1;
        return ret;
    }
}

void button_deinit(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}
