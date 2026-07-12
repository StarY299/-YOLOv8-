/**
 * tft_display.h — 1.8寸 TFT SPI 屏驱动 (ST7735, 128x160)
 *
 * 用户态直驱, 不依赖内核 DRM/fbdev, 无需修改设备树.
 * 使用 /dev/spidevX.X 发送像素数据, /sys/class/gpio 控制 DC/RST.
 */

#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 TFT 显示屏
 *
 * 1. 导出并配置 DC/RST GPIO
 * 2. 打开 SPI 设备
 * 3. 发送 ST7735 初始化序列
 * 4. 初始化 LVGL 并注册 flush_cb
 *
 * @param spi_dev    SPI 设备路径, 如 "/dev/spidev2.0"
 * @param gpio_dc    DC 引脚 GPIO 编号 (sysfs), 如 44
 * @param gpio_rst   RST 引脚 GPIO 编号, 如 43
 * @return 0 成功, -1 失败
 */
int tft_display_init(const char *spi_dev, int gpio_dc, int gpio_rst);

/**
 * 销毁 TFT 显示屏, 释放 LVGL 和 SPI 资源
 */
void tft_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
