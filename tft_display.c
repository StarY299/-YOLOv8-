/**
 * tft_display.c — ST7735 TFT SPI 驱动 + LVGL 对接
 *
 * 用户态 SPI 直驱, LVGL flush_cb 写像素到 ST7735.
 * 双缓冲 ~8KB, RGB565.
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

/* ---- ST7735 命令 ---- */
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
#define ST7735_INVCTR   0xB4
#define ST7735_PWCTR1   0xC0
#define ST7735_GAMCTRP1 0xE0

#define LVGL_BUF_PX  (TFT_W * TFT_H / 10)  /* ~2KB 单缓冲 */

/* ---- 全局状态 ---- */
static int           g_spi = -1, g_dc = -1, g_rst = -1;
static lv_color_t   *g_buf1 = NULL, *g_buf2 = NULL;
static lv_disp_t    *g_disp = NULL;

/* ---- GPIO (sysfs) ---- */
static int gpio_export(int pin) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    char b[16]; int n = snprintf(b, sizeof(b), "%d", pin);
    write(fd, b, n); close(fd); return 0;
}
static int gpio_set_dir(int pin, const char *dir) {
    char p[64]; snprintf(p, sizeof(p), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(p, O_WRONLY); if (fd < 0) return -1;
    write(fd, dir, strlen(dir)); close(fd); return 0;
}
static int gpio_open(int pin) {
    char p[64]; snprintf(p, sizeof(p), "/sys/class/gpio/gpio%d/value", pin);
    return open(p, O_WRONLY);
}
static void gpio_wr(int fd, int v) { char c = v ? '1' : '0'; write(fd, &c, 1); }

/* ---- SPI ---- */
static void tft_cmd(uint8_t c)   { gpio_wr(g_dc, 0); write(g_spi, &c, 1); }
static void tft_data(uint8_t d)  { gpio_wr(g_dc, 1); write(g_spi, &d, 1); }
static void tft_bulk(const uint8_t *d, int n) { gpio_wr(g_dc, 1); write(g_spi, d, n); }
static void tft_window(int x1, int y1, int x2, int y2) {
    tft_cmd(ST7735_CASET);
    tft_data(0x00); tft_data(x1); tft_data(0x00); tft_data(x2);
    tft_cmd(ST7735_RASET);
    tft_data(0x00); tft_data(y1); tft_data(0x00); tft_data(y2);
    tft_cmd(ST7735_RAMWR);
}

/* ---- ST7735 初始化 ---- */
static int st7735_init(void) {
    if (g_rst >= 0) { gpio_wr(g_rst,0); usleep(10000); gpio_wr(g_rst,1); usleep(120000); }
    tft_cmd(ST7735_SWRESET); usleep(150000);
    tft_cmd(ST7735_SLPOUT);  usleep(120000);
    tft_cmd(ST7735_FRMCTR1); { uint8_t d[]={0x01,0x2C,0x2D}; tft_bulk(d,3); }
    tft_cmd(ST7735_INVCTR); tft_data(0x07);
    tft_cmd(ST7735_PWCTR1); { uint8_t d[]={0xA2,0x02,0x84}; tft_bulk(d,3); }
    tft_cmd(ST7735_INVOFF);
    tft_cmd(ST7735_MADCTL); tft_data(0xC8);
    tft_cmd(ST7735_COLMOD); tft_data(0x05);
    tft_cmd(ST7735_GAMCTRP1);
    { uint8_t d[]={0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                   0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}; tft_bulk(d,16); }
    tft_cmd(ST7735_NORON);  usleep(10000);
    tft_cmd(ST7735_DISPON); usleep(100000);
    return 0;
}

/* ---- LVGL flush_cb ---- */
static void tft_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p)
{
    (void)drv;
    int x1 = area->x1 < 0 ? 0 : area->x1;
    int y1 = area->y1 < 0 ? 0 : area->y1;
    int x2 = area->x2 >= TFT_W ? TFT_W-1 : area->x2;
    int y2 = area->y2 >= TFT_H ? TFT_H-1 : area->y2;

    tft_window(x1, y1, x2, y2);
    gpio_wr(g_dc, 1);
    write(g_spi, color_p, (x2-x1+1) * (y2-y1+1) * sizeof(lv_color_t));
    lv_disp_flush_ready(drv);
}

/* ---- 公共 API ---- */
int tft_display_init(const char *spi_dev, int gpio_dc, int gpio_rst)
{
    printf("[TFT] init: spi=%s dc=%d rst=%d\n", spi_dev, gpio_dc, gpio_rst);

    gpio_export(gpio_dc); gpio_set_dir(gpio_dc, "out");
    g_dc = gpio_open(gpio_dc);
    if (g_dc < 0) { perror("[TFT] DC"); return -1; }
    if (gpio_rst >= 0) { gpio_export(gpio_rst); gpio_set_dir(gpio_rst, "out");
                         g_rst = gpio_open(gpio_rst); }

    g_spi = open(spi_dev, O_RDWR);
    if (g_spi < 0) { perror("[TFT] SPI"); return -1; }
    uint8_t m=SPI_MODE_0, b=8; uint32_t s=32000000;
    ioctl(g_spi, SPI_IOC_WR_MODE, &m);
    ioctl(g_spi, SPI_IOC_WR_BITS_PER_WORD, &b);
    ioctl(g_spi, SPI_IOC_WR_MAX_SPEED_HZ, &s);

    st7735_init();

    /* LVGL */
    lv_init();
    g_buf1 = malloc(LVGL_BUF_PX * sizeof(lv_color_t));
    g_buf2 = malloc(LVGL_BUF_PX * sizeof(lv_color_t));
    if (!g_buf1 || !g_buf2) { fprintf(stderr,"[TFT] LVGL buf fail\n"); return -1; }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, g_buf1, g_buf2, LVGL_BUF_PX);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = TFT_W; drv.ver_res = TFT_H;
    drv.flush_cb = tft_flush_cb;
    drv.draw_buf = &draw_buf;
    g_disp = lv_disp_drv_register(&drv);

    printf("[TFT] LVGL ready (%dx%d)\n", TFT_W, TFT_H);
    return 0;
}

void tft_display_deinit(void)
{
    if (g_disp) { lv_disp_remove(g_disp); g_disp = NULL; }
    free(g_buf1); free(g_buf2);
    if (g_spi>=0) { close(g_spi); g_spi=-1; }
    if (g_dc>=0)  { close(g_dc);  g_dc=-1; }
    if (g_rst>=0) { close(g_rst); g_rst=-1; }
}
