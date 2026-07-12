/**
 * tft_display.c — ST7735 1.8寸 TFT SPI 驱动 + LVGL 对接
 *
 * 用户态直驱: /dev/spidev + /sys/class/gpio, 不需要内核驱动/设备树.
 * 像素格式: RGB565 (16bpp)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <lvgl/lvgl.h>
#include "tft_display.h"

/* ============================================================
 *  TFT 硬件参数
 * ============================================================ */
#define TFT_WIDTH   128
#define TFT_HEIGHT  160
#define TFT_BPP     16          /* RGB565 */

/* ST7735 命令 */
#define ST7735_SWRESET  0x01
#define ST7735_SLPOUT   0x11
#define ST7735_NORON    0x13
#define ST7735_INVOFF   0x20
#define ST7735_DISPON   0x29
#define ST7735_CASET    0x2A
#define ST7735_RASET    0x2B
#define ST7735_RAMWR    0x2C
#define ST7735_MADCTL   0x36
#define ST7735_COLMOD   0x3A
#define ST7735_FRMCTR1  0xB1
#define ST7735_FRMCTR2  0xB2
#define ST7735_FRMCTR3  0xB3
#define ST7735_INVCTR   0xB4
#define ST7735_PWCTR1   0xC0
#define ST7735_PWCTR2   0xC1
#define ST7735_PWCTR3   0xC2
#define ST7735_PWCTR4   0xC3
#define ST7735_PWCTR5   0xC4
#define ST7735_VMCTR1   0xC5
#define ST7735_GAMCTRP1 0xE0
#define ST7735_GAMCTRN1 0xE1

/* LVGL 缓冲: 1/10 屏幕 = ~4KB, 平衡内存与刷新性能 */
#define LVGL_BUF_SIZE  (TFT_WIDTH * TFT_HEIGHT / 10)

/* ============================================================
 *  全局状态
 * ============================================================ */
static int            g_spi_fd    = -1;
static int            g_gpio_dc  = -1;
static int            g_gpio_rst = -1;
static char           g_dc_path[64];
static lv_color_t    *g_lvgl_buf1 = NULL;
static lv_color_t    *g_lvgl_buf2 = NULL;
static lv_disp_drv_t  g_disp_drv;
static lv_disp_draw_buf_t g_draw_buf;
static lv_disp_t     *g_disp = NULL;

/* ============================================================
 *  GPIO 操作 (sysfs)
 * ============================================================ */
static int gpio_export(int pin)
{
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, len);
    close(fd);
    return 0;
}

static int gpio_set_direction(int pin, const char *dir)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static int gpio_open(int pin)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    return open(path, O_WRONLY);
}

static void gpio_write(int fd, int val)
{
    char c = val ? '1' : '0';
    write(fd, &c, 1);
}

/* ============================================================
 *  SPI 操作
 * ============================================================ */
static void spi_begin(void) { /* CS 由 spidev 自动管理 */ }
static void spi_end(void)   { /* CS 由 spidev 自动管理 */ }

/* 发送命令 (DC=0) */
static void tft_send_cmd(uint8_t cmd)
{
    gpio_write(g_gpio_dc, 0);
    write(g_spi_fd, &cmd, 1);
}

/* 发送数据 (DC=1) */
static void tft_send_data(uint8_t data)
{
    gpio_write(g_gpio_dc, 1);
    write(g_spi_fd, &data, 1);
}

/* 批量发送数据 */
static void tft_send_data_bulk(const uint8_t *data, int len)
{
    gpio_write(g_gpio_dc, 1);
    write(g_spi_fd, data, len);
}

/* 设置窗口 (列范围 + 行范围) */
static void tft_set_addr_window(int x1, int y1, int x2, int y2)
{
    tft_send_cmd(ST7735_CASET);
    tft_send_data(0x00);          /* 列地址高字节 = 0 */
    tft_send_data(x1);            /* 起始列 */
    tft_send_data(0x00);
    tft_send_data(x2);            /* 结束列 */

    tft_send_cmd(ST7735_RASET);
    tft_send_data(0x00);          /* 行地址高字节 = 0 */
    tft_send_data(y1);            /* 起始行 */
    tft_send_data(0x00);
    tft_send_data(y2);            /* 结束行 */

    tft_send_cmd(ST7735_RAMWR);   /* 准备写像素 */
}

/* ============================================================
 *  ST7735 初始化序列 (128x160, RGB565)
 * ============================================================ */
static int st7735_init(void)
{
    /* 硬件复位 */
    if (g_gpio_rst >= 0) {
        gpio_write(g_gpio_rst, 0);
        usleep(10000);
        gpio_write(g_gpio_rst, 1);
        usleep(120000);
    }

    /* 软件复位 */
    tft_send_cmd(ST7735_SWRESET);
    usleep(150000);

    /* 退出睡眠 */
    tft_send_cmd(ST7735_SLPOUT);
    usleep(120000);

    /* 帧速率控制 */
    {
        tft_send_cmd(ST7735_FRMCTR1);
        uint8_t frm1[] = {0x01, 0x2C, 0x2D};
        tft_send_data_bulk(frm1, 3);

        tft_send_cmd(ST7735_FRMCTR2);
        uint8_t frm2[] = {0x01, 0x2C, 0x2D};
        tft_send_data_bulk(frm2, 3);

        tft_send_cmd(ST7735_FRMCTR3);
        uint8_t frm3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
        tft_send_data_bulk(frm3, 6);
    }

    /* 反相控制 */
    tft_send_cmd(ST7735_INVCTR);
    tft_send_data(0x07);

    /* 电源控制 */
    {
        tft_send_cmd(ST7735_PWCTR1);
        uint8_t pw1[] = {0xA2, 0x02, 0x84};
        tft_send_data_bulk(pw1, 3);

        tft_send_cmd(ST7735_PWCTR2);
        tft_send_data(0xC5);

        tft_send_cmd(ST7735_PWCTR3);
        uint8_t pw3[] = {0x0A, 0x00};
        tft_send_data_bulk(pw3, 2);

        tft_send_cmd(ST7735_PWCTR4);
        uint8_t pw4[] = {0x8A, 0x2A};
        tft_send_data_bulk(pw4, 2);

        tft_send_cmd(ST7735_PWCTR5);
        uint8_t pw5[] = {0x8A, 0xEE};
        tft_send_data_bulk(pw5, 2);
    }

    tft_send_cmd(ST7735_VMCTR1);
    tft_send_data(0x0E);

    /* 反相关闭 */
    tft_send_cmd(ST7735_INVOFF);

    /* 内存访问控制: 横屏 (MY=0, MX=1, MV=1, BGR=1) */
    tft_send_cmd(ST7735_MADCTL);
    tft_send_data(0xC8);

    /* 像素格式: 16-bit RGB565 */
    tft_send_cmd(ST7735_COLMOD);
    tft_send_data(0x05);

    /* Gamma 校正 */
    {
        tft_send_cmd(ST7735_GAMCTRP1);
        uint8_t gp[] = {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                        0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10};
        tft_send_data_bulk(gp, 16);

        tft_send_cmd(ST7735_GAMCTRN1);
        uint8_t gn[] = {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                        0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10};
        tft_send_data_bulk(gn, 16);
    }

    /* 正常显示模式 */
    tft_send_cmd(ST7735_NORON);
    usleep(10000);

    /* 开显示 */
    tft_send_cmd(ST7735_DISPON);
    usleep(100000);

    return 0;
}

/* ============================================================
 *  LVGL flush_cb: 把渲染好的像素块推到 SPI
 * ============================================================ */
static void tft_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p)
{
    (void)drv;

    /* 限制在屏幕范围内 */
    int x1 = area->x1 < 0 ? 0 : area->x1;
    int y1 = area->y1 < 0 ? 0 : area->y1;
    int x2 = area->x2 >= TFT_WIDTH  ? TFT_WIDTH  - 1 : area->x2;
    int y2 = area->y2 >= TFT_HEIGHT ? TFT_HEIGHT - 1 : area->y2;

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;
    int n_pixels = w * h;

    tft_set_addr_window(x1, y1, x2, y2);

    /* 转换 lv_color_t → RGB565 并通过 SPI 发送
     * lv_color_t 在 LV_COLOR_DEPTH=16 时就是 uint16_t (RGB565) */
    gpio_write(g_gpio_dc, 1);
    write(g_spi_fd, color_p, n_pixels * sizeof(lv_color_t));

    /* 通知 LVGL 刷完 */
    lv_disp_flush_ready(drv);
}

/* ============================================================
 *  公共 API
 * ============================================================ */
int tft_display_init(const char *spi_dev, int gpio_dc, int gpio_rst)
{
    printf("[TFT] init: spi=%s dc=gpio%d rst=gpio%d\n", spi_dev, gpio_dc, gpio_rst);

    /* ---- 1. GPIO 配置 ---- */
    g_gpio_dc  = -1;
    g_gpio_rst = -1;

    gpio_export(gpio_dc);
    gpio_set_direction(gpio_dc, "out");
    g_gpio_dc = gpio_open(gpio_dc);
    if (g_gpio_dc < 0) { perror("[TFT] DC gpio"); return -1; }

    if (gpio_rst >= 0) {
        gpio_export(gpio_rst);
        gpio_set_direction(gpio_rst, "out");
        g_gpio_rst = gpio_open(gpio_rst);
    }

    /* ---- 2. SPI 设备 ---- */
    g_spi_fd = open(spi_dev, O_RDWR);
    if (g_spi_fd < 0) {
        perror("[TFT] open spi");
        return -1;
    }

    uint8_t  mode = SPI_MODE_0;
    uint8_t  bits = 8;
    uint32_t speed = 32000000;  /* 32 MHz, ST7735 最高支持 33MHz */
    ioctl(g_spi_fd, SPI_IOC_WR_MODE,        &mode);
    ioctl(g_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(g_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    printf("[TFT] SPI configured: mode=%d bits=%d speed=%d Hz\n", mode, bits, speed);

    /* ---- 3. ST7735 初始化 ---- */
    st7735_init();
    printf("[TFT] ST7735 init done\n");

    /* ---- 4. LVGL 初始化 ---- */
    lv_init();

    /* 分配 LVGL 绘制缓冲 (双缓冲, ~8KB) */
    g_lvgl_buf1 = (lv_color_t *)malloc(LVGL_BUF_SIZE * sizeof(lv_color_t));
    g_lvgl_buf2 = (lv_color_t *)malloc(LVGL_BUF_SIZE * sizeof(lv_color_t));
    if (!g_lvgl_buf1 || !g_lvgl_buf2) {
        fprintf(stderr, "[TFT] LVGL buffer alloc failed\n");
        return -1;
    }
    lv_disp_draw_buf_init(&g_draw_buf, g_lvgl_buf1, g_lvgl_buf2, LVGL_BUF_SIZE);

    /* 注册显示驱动 */
    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res  = TFT_WIDTH;
    g_disp_drv.ver_res  = TFT_HEIGHT;
    g_disp_drv.flush_cb = tft_flush_cb;
    g_disp_drv.draw_buf = &g_draw_buf;
    g_disp = lv_disp_drv_register(&g_disp_drv);

    printf("[TFT] LVGL ready (%dx%d)\n", TFT_WIDTH, TFT_HEIGHT);
    return 0;
}

void tft_display_deinit(void)
{
    printf("[TFT] deinit\n");

    if (g_disp) {
        lv_disp_remove(g_disp);
        g_disp = NULL;
    }

    free(g_lvgl_buf1); g_lvgl_buf1 = NULL;
    free(g_lvgl_buf2); g_lvgl_buf2 = NULL;

    if (g_spi_fd >= 0)  { close(g_spi_fd);  g_spi_fd  = -1; }
    if (g_gpio_dc >= 0) { close(g_gpio_dc); g_gpio_dc = -1; }
    if (g_gpio_rst >= 0){ close(g_gpio_rst);g_gpio_rst= -1; }

    printf("[TFT] deinit done\n");
}
