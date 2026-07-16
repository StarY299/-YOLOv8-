/**
 * lcd.c - 1.8寸TFT液晶驱动 (ST7735S) for RV1126B Linux
 *
 * 硬件SPI (Mode 3, 每字节独立CS) + GPIO chardev (DC/BLK)
 * 已验证通过: ELF-RV1126B + 正点原子 1.8寸TFT
 */
#include "lcd.h"
#include "lcdfont.h"
#include "cn_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>

// ==================== 全局变量 ====================
static int spi_fd = -1;
static int dc_fd  = -1;
static int blk_fd = -1;
static int chip5_fd = -1;

// ==================== 内部辅助 ====================

static void delay_ms(unsigned int ms) { usleep(ms * 1000); }

static void dc_set(int val) {
    struct gpio_v2_line_values v = {.bits = val ? 1 : 0, .mask = 1};
    if (dc_fd >= 0) ioctl(dc_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

static void blk_set(int val) {
    struct gpio_v2_line_values v = {.bits = val ? 1 : 0, .mask = 1};
    if (blk_fd >= 0) ioctl(blk_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

// 每字节独立 SPI 传输 (CS 由硬件自动切换)
static void spi_w8(uint8_t dat) {
    struct spi_ioc_transfer t = {
        .tx_buf = (unsigned long)&dat,
        .rx_buf = 0,
        .len = 1,
        .speed_hz = SPI_SPEED,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &t);
}

// ==================== 硬件初始化 ====================

int lcd_hw_init(void) {
    // --- GPIO: DC 和 BLK (gpiochip5) ---
    chip5_fd = open("/dev/gpiochip5", O_RDWR);
    if (chip5_fd < 0) { perror("gpiochip5"); return -1; }

    // DC: offset 30 = GPIO5_D6
    struct gpio_v2_line_request req = {0};
    req.offsets[0] = 30;  req.num_lines = 1;
    snprintf(req.consumer, sizeof(req.consumer), "lcd_dc");
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    if (ioctl(chip5_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        perror("DC gpio"); return -1;
    }
    dc_fd = req.fd;

    // BLK: offset 31 = GPIO5_D7
    req.offsets[0] = 31;
    snprintf(req.consumer, sizeof(req.consumer), "lcd_blk");
    if (ioctl(chip5_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        perror("BLK gpio"); return -1;
    }
    blk_fd = req.fd;

    dc_set(1);   // DC 初始高
    blk_set(0);  // BLK 初始低

    // --- SPI ---
    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) { perror("SPI"); return -1; }

    uint8_t mode = SPI_MODE_VAL;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED;

    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    printf("LCD HW: SPI=%s Mode=3 DC=GPIO190 BLK=GPIO191\n", SPI_DEV);
    return 0;
}

void lcd_hw_deinit(void) {
    if (spi_fd  >= 0) { close(spi_fd);  spi_fd  = -1; }
    if (dc_fd   >= 0) { close(dc_fd);   dc_fd   = -1; }
    if (blk_fd  >= 0) { close(blk_fd);  blk_fd  = -1; }
    if (chip5_fd >= 0) { close(chip5_fd); chip5_fd = -1; }
}

int lcd_get_spi_fd(void) { return spi_fd; }

// ==================== LCD 底层通信 ====================

void lcd_write_cmd(uint8_t cmd) {
    dc_set(0);              // DC=0 命令模式
    usleep(50);
    spi_w8(cmd);
    usleep(50);
    dc_set(1);              // DC=1 数据模式
    usleep(50);
}

void lcd_write_data8(uint8_t dat) {
    spi_w8(dat);            // DC 已为高 (由上一条 lcd_write_cmd 设置)
}

void lcd_write_data16(uint16_t dat) {
    spi_w8(dat >> 8);       // 高字节在前 (每字节独立CS, 与STM32一致)
    spi_w8(dat & 0xFF);
}

void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    lcd_write_cmd(0x2A);    // CASET 列地址
    lcd_write_data16(x1);
    lcd_write_data16(x2);
    lcd_write_cmd(0x2B);    // RASET 行地址
    lcd_write_data16(y1);
    lcd_write_data16(y2);
    lcd_write_cmd(0x2C);    // RAMWR 内存写
}

// ==================== LCD 初始化 (ST7735S) ====================

void lcd_init(void) {
    // 等上电稳定
    usleep(100000);
    blk_set(1);  // 开背光

    // === ST7735S 初始化序列 ===
    lcd_write_cmd(0x11);    // Sleep out
    delay_ms(120);

    lcd_write_cmd(0xB1);
    lcd_write_data8(0x05); lcd_write_data8(0x3C); lcd_write_data8(0x3C);
    lcd_write_cmd(0xB2);
    lcd_write_data8(0x05); lcd_write_data8(0x3C); lcd_write_data8(0x3C);
    lcd_write_cmd(0xB3);
    lcd_write_data8(0x05); lcd_write_data8(0x3C); lcd_write_data8(0x3C);
    lcd_write_data8(0x05); lcd_write_data8(0x3C); lcd_write_data8(0x3C);

    lcd_write_cmd(0xB4);    // Dot inversion
    lcd_write_data8(0x03);

    lcd_write_cmd(0xC0);
    lcd_write_data8(0x28); lcd_write_data8(0x08); lcd_write_data8(0x04);
    lcd_write_cmd(0xC1);
    lcd_write_data8(0xC0);
    lcd_write_cmd(0xC2);
    lcd_write_data8(0x0D); lcd_write_data8(0x00);
    lcd_write_cmd(0xC3);
    lcd_write_data8(0x8D); lcd_write_data8(0x2A);
    lcd_write_cmd(0xC4);
    lcd_write_data8(0x8D); lcd_write_data8(0xEE);

    lcd_write_cmd(0xC5);    // VCOM
    lcd_write_data8(0x1A);

    lcd_write_cmd(0x36);    // MADCTL
    if (USE_HORIZONTAL == 0)      lcd_write_data8(0x00);
    else if (USE_HORIZONTAL == 1) lcd_write_data8(0xC0);
    else if (USE_HORIZONTAL == 2) lcd_write_data8(0x70);
    else                          lcd_write_data8(0xA0);

    lcd_write_cmd(0xE0);    // Gamma+
    lcd_write_data8(0x04); lcd_write_data8(0x22); lcd_write_data8(0x07);
    lcd_write_data8(0x0A); lcd_write_data8(0x2E); lcd_write_data8(0x30);
    lcd_write_data8(0x25); lcd_write_data8(0x2A); lcd_write_data8(0x28);
    lcd_write_data8(0x26); lcd_write_data8(0x2E); lcd_write_data8(0x3A);
    lcd_write_data8(0x00); lcd_write_data8(0x01); lcd_write_data8(0x03);
    lcd_write_data8(0x13);

    lcd_write_cmd(0xE1);    // Gamma-
    lcd_write_data8(0x04); lcd_write_data8(0x16); lcd_write_data8(0x06);
    lcd_write_data8(0x0D); lcd_write_data8(0x2D); lcd_write_data8(0x26);
    lcd_write_data8(0x23); lcd_write_data8(0x27); lcd_write_data8(0x27);
    lcd_write_data8(0x25); lcd_write_data8(0x2D); lcd_write_data8(0x3B);
    lcd_write_data8(0x00); lcd_write_data8(0x01); lcd_write_data8(0x04);
    lcd_write_data8(0x13);

    lcd_write_cmd(0x3A);    // 16-bit RGB565
    lcd_write_data8(0x05);

    lcd_write_cmd(0x29);    // Display on

    printf("LCD init done (ST7735S %dx%d Mode3)\n", LCD_W, LCD_H);
}

// ==================== 绘图函数 ====================

void lcd_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color) {
    lcd_set_address(xs, ys, xe - 1, ye - 1);
    for (uint16_t i = ys; i < ye; i++)
        for (uint16_t j = xs; j < xe; j++)
            lcd_write_data16(color);
}

void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color) {
    lcd_set_address(x, y, x, y);
    lcd_write_data16(color);
}

void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;

    delta_x = x2 - x1; delta_y = y2 - y1;
    uRow = x1; uCol = y1;

    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }

    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_y; }

    distance = (delta_x > delta_y) ? delta_x : delta_y;

    for (uint16_t t = 0; t <= (uint16_t)distance; t++) {
        lcd_draw_point(uRow, uCol, color);
        xerr += delta_x; yerr += delta_y;
        if (xerr > distance) { xerr -= distance; uRow += incx; }
        if (yerr > distance) { yerr -= distance; uCol += incy; }
    }
}

void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color) {
    int a = 0, b = r;
    while (a <= b) {
        lcd_draw_point(x0 - b, y0 - a, color);
        lcd_draw_point(x0 + b, y0 - a, color);
        lcd_draw_point(x0 - a, y0 + b, color);
        lcd_draw_point(x0 - a, y0 - b, color);
        lcd_draw_point(x0 + b, y0 + a, color);
        lcd_draw_point(x0 + a, y0 - b, color);
        lcd_draw_point(x0 + a, y0 + b, color);
        lcd_draw_point(x0 - b, y0 + a, color);
        a++;
        if ((a * a + b * b) > (r * r)) b--;
    }
}

// ==================== 内置中文显示 (lcdfont.h 的 tfont 数组) ====================

// 显示内置中文字符串 (GB2312编码, 从tfont数组查找)
// sizey: 12/16/24/32

/* 自定义16x16中文显示 */

/* 自定义12x12中文显示 */
void lcd_show_cn12_custom(uint16_t x, uint16_t y, const uint8_t *s, uint16_t fc, uint16_t bc)
{
    while (*s) {
        int found = 0;
        for (int i = 0; i < 56; i++) {
            if (tfont12_custom[i].Index[0] == s[0] && tfont12_custom[i].Index[1] == s[1]) {
                lcd_set_address(x, y, x + 15, y + 11);
                for (int j = 0; j < 24; j++) {
                    uint8_t d = tfont12_custom[i].Msk[j];
                    for (int k = 0; k < 8; k++) {
                        if (d & 0x80) lcd_write_data16(fc); else lcd_write_data16(bc);
                        d <<= 1;
                    }
                }
                x += 12; s += 2; found = 1; break;
            }
        }
        if (!found) { s += 2; x += 12; }
    }
}

void lcd_show_cn_custom(uint16_t x, uint16_t y, const uint8_t *s,
                         uint16_t fc, uint16_t bc)
{
    while (*s) {
        int found = 0;
        for (int i = 0; i < 56; i++) {
            if (tfont16_custom[i].Index[0] == s[0] &&
                tfont16_custom[i].Index[1] == s[1]) {
                lcd_set_address(x, y, x + 15, y + 15);
                for (int j = 0; j < 32; j++) {
                    uint8_t d = tfont16_custom[i].Msk[j];
                    for (int k = 0; k < 8; k++) {
                        if (d & 0x80) lcd_write_data16(fc);
                        else lcd_write_data16(bc);
                        d <<= 1;
                    }
                }
                x += 16; s += 2; found = 1; break;
            }
        }
        if (!found) { s += 2; x += 16; }
    }
}

void lcd_show_builtin_cn(uint16_t x, uint16_t y, const uint8_t *s,
                         uint16_t fc, uint16_t bc, uint8_t sizey) {
    uint16_t x0 = x;
    while (*s && *(s+1)) {
        uint8_t h = *s, l = *(s+1);
        int found = 0;
        uint8_t *msk = NULL;
        uint16_t count, bytes;

        if (sizey == 12) {
            count = sizeof(tfont12) / sizeof(typFNT_GB12);
            for (uint16_t k = 0; k < count; k++) {
                if (tfont12[k].Index[0] == h && tfont12[k].Index[1] == l) {
                    msk = (uint8_t *)tfont12[k].Msk;
                    found = 1; break;
                }
            }
            bytes = 24;
        } else if (sizey == 16) {
            count = sizeof(tfont16) / sizeof(typFNT_GB16);
            for (uint16_t k = 0; k < count; k++) {
                if (tfont16[k].Index[0] == h && tfont16[k].Index[1] == l) {
                    msk = (uint8_t *)tfont16[k].Msk;
                    found = 1; break;
                }
            }
            bytes = 32;
        } else if (sizey == 24) {
            count = sizeof(tfont24) / sizeof(typFNT_GB24);
            for (uint16_t k = 0; k < count; k++) {
                if (tfont24[k].Index[0] == h && tfont24[k].Index[1] == l) {
                    msk = (uint8_t *)tfont24[k].Msk;
                    found = 1; break;
                }
            }
            bytes = 72;
        } else if (sizey == 32) {
            count = sizeof(tfont32) / sizeof(typFNT_GB32);
            for (uint16_t k = 0; k < count; k++) {
                if (tfont32[k].Index[0] == h && tfont32[k].Index[1] == l) {
                    msk = (uint8_t *)tfont32[k].Msk;
                    found = 1; break;
                }
            }
            bytes = 128;
        } else {
            s += 2; x += sizey; continue;
        }

        if (found && msk) {
            lcd_set_address(x, y, x + sizey - 1, y + sizey - 1);
            for (uint16_t i = 0; i < bytes; i++) {
                for (uint8_t j = 0; j < 8; j++) {
                    lcd_write_data16((msk[i] & (0x01 << j)) ? fc : bc);
                }
            }
        } else {
            // 字不在内置库, 画红色方块标记
            lcd_fill(x, y, x + sizey, y + sizey, RED);
        }
        s += 2;
        x += sizey;
    }
}

// ==================== 文本显示 ====================

static uint32_t mypow(uint8_t m, uint8_t n) {
    uint32_t r = 1;
    while (n--) r *= m;
    return r;
}

void lcd_show_char(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc,
                   uint8_t sizey, uint8_t mode) {
    uint8_t temp, sizex, t, m = 0;
    uint16_t i, TypefaceNum, x0 = x;

    sizex = sizey / 2;
    TypefaceNum = (sizex / 8 + ((sizex % 8) ? 1 : 0)) * sizey;
    num = num - ' ';

    lcd_set_address(x, y, x + sizex - 1, y + sizey - 1);

    for (i = 0; i < TypefaceNum; i++) {
        if (sizey == 12)      temp = ascii_1206[num][i];
        else if (sizey == 16) temp = ascii_1608[num][i];
        else if (sizey == 24) temp = ascii_2412[num][i];
        else if (sizey == 32) temp = ascii_3216[num][i];
        else return;

        for (t = 0; t < 8; t++) {
            if (!mode) {
                lcd_write_data16((temp & (0x01 << t)) ? fc : bc);
                m++;
                if (m % sizex == 0) { m = 0; break; }
            } else {
                if (temp & (0x01 << t)) lcd_draw_point(x, y, fc);
                x++;
                if ((x - x0) == sizex) { x = x0; y++; break; }
            }
        }
    }
}

void lcd_show_string(uint16_t x, uint16_t y, const uint8_t *p, uint16_t fc,
                     uint16_t bc, uint8_t sizey, uint8_t mode) {
    while (*p) {
        lcd_show_char(x, y, *p, fc, bc, sizey, mode);
        x += sizey / 2;
        p++;
    }
}

void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len,
                      uint16_t fc, uint16_t bc, uint8_t sizey) {
    uint8_t t, temp, enshow = 0, sizex = sizey / 2;
    for (t = 0; t < len; t++) {
        temp = (num / mypow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1)) {
            if (temp == 0) {
                lcd_show_char(x + t * sizex, y, ' ', fc, bc, sizey, 0);
                continue;
            } else enshow = 1;
        }
        lcd_show_char(x + t * sizex, y, temp + '0', fc, bc, sizey, 0);
    }
}

void lcd_show_float_num(uint16_t x, uint16_t y, float num, uint8_t len,
                        uint16_t fc, uint16_t bc, uint8_t sizey) {
    uint8_t t, temp, sizex = sizey / 2;
    uint16_t num1 = (uint16_t)(num * 100);
    for (t = 0; t < len; t++) {
        temp = (num1 / mypow(10, len - t - 1)) % 10;
        if (t == (len - 2)) {
            lcd_show_char(x + (len - 2) * sizex, y, '.', fc, bc, sizey, 0);
            t++; len++;
        }
        lcd_show_char(x + t * sizex, y, temp + '0', fc, bc, sizey, 0);
    }
}

void lcd_show_picture(uint16_t x, uint16_t y, uint16_t length, uint16_t width,
                      const uint8_t pic[]) {
    uint32_t k = 0;
    lcd_set_address(x, y, x + length - 1, y + width - 1);
    for (uint16_t i = 0; i < length; i++)
        for (uint16_t j = 0; j < width; j++) {
            lcd_write_data8(pic[k * 2]);
            lcd_write_data8(pic[k * 2 + 1]);
            k++;
        }
}

void lcd_backlight_on(void)  { blk_set(1); }
void lcd_backlight_off(void) { blk_set(0); }
