#ifndef __LCD_H
#define __LCD_H

#include <stdint.h>

// ==================== 显示方向配置 ====================
#define USE_HORIZONTAL 1  // 0/1=横屏 128x160  2/3=竖屏 160x128

#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
#define LCD_W 128
#define LCD_H 160
#else
#define LCD_W 160
#define LCD_H 128
#endif

// ==================== 引脚配置 (RV1126B ELF Board) ====================
#define PIN_MOSI 107   // SDI -> SPI1_MOSI (GPIO3_B3)
#define PIN_SCLK 106   // CLK -> SPI1_CLK  (GPIO3_B2)
#define PIN_CS   109   // CS  -> SPI1_CSN0 (GPIO3_B5) — 硬件CS
#define PIN_MISO 108   // SDO -> SPI1_MISO (GPIO3_B4)
#define PIN_DC   190   // D/C -> GPIO5_D6  (gpiochip5 offset 30)
#define PIN_BLK  191   // BLK -> GPIO5_D7  (gpiochip5 offset 31)
#define PIN_RST   -1   // RST -> 未接

// SPI 配置
#define SPI_DEV      "/dev/spidev1.0"
#define SPI_MODE_VAL SPI_MODE_3   // CPOL=1, CPHA=1
#define SPI_SPEED    8000000      // 8MHz

// ==================== 颜色定义 (RGB565) ====================
#define WHITE         0xFFFF
#define BLACK         0x0000
#define BLUE          0x001F
#define BRED          0xF81F
#define GRED          0xFFE0
#define GBLUE         0x07FF
#define RED           0xF800
#define MAGENTA       0xF81F
#define GREEN         0x07E0
#define CYAN          0x7FFF
#define YELLOW        0xFFE0
#define BROWN         0xBC40
#define BRRED         0xFC07
#define GRAY          0x8430
#define DARKBLUE      0x01CF
#define LIGHTBLUE     0x7D7C
#define GRAYBLUE      0x5458
#define LIGHTGREEN    0x841F
#define LGRAY         0xC618
#define LGRAYBLUE     0xA651
#define LBBLUE        0x2B12

// ==================== 函数声明 ====================

// 硬件初始化
int  lcd_hw_init(void);
void lcd_hw_deinit(void);

// LCD 底层通信
void lcd_write_cmd(uint8_t cmd);
void lcd_write_data8(uint8_t dat);
void lcd_write_data16(uint16_t dat);
void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

// LCD 初始化
void lcd_init(void);

// 绘图函数
void lcd_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color);
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);

// 文本显示 (内置字库 - 中文)
void lcd_show_builtin_cn(uint16_t x, uint16_t y, const uint8_t *s, uint16_t fc, uint16_t bc, uint8_t sizey);

// 文本显示 (内置字库)
void lcd_show_char(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void lcd_show_string(uint16_t x, uint16_t y, const uint8_t *p, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
void lcd_show_float_num(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t len, uint16_t wid, const uint8_t pic[]);

// 背光控制
void lcd_backlight_on(void);
void lcd_backlight_off(void);

// 获取 SPI fd (供字库芯片使用)
int lcd_get_spi_fd(void);

#endif
