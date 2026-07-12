/**
 * zk.c - 字库芯片驱动 (GT20L16S1Y / 类似 SPI Flash 字库)
 *
 * 字库芯片与 LCD 共用 SPI 总线 (MOSI, SCLK, CS)，
 * 通过全双工 SPI 读取字库数据 (MISO)。
 *
 * 基于正点原子 F103 1.8寸TFT液晶驱动移植
 */

#include "zk.h"
#include "lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

// 全局字库缓冲区
uint8_t FontBuf[130];

// 外部引用 LCD 的 SPI 文件描述符和 CS 控制
extern int lcd_get_spi_fd(void);

// ==================== 字库芯片 SPI 通信 ====================

/**
 * @brief 通过 SPI 向字库芯片发送一个字节 (DC 保持高，不影响 LCD 状态)
 */
void ZK_command(uint8_t dat)
{
    int fd = lcd_get_spi_fd();
    if (fd < 0) return;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)&dat,
        .rx_buf = 0,
        .len = 1,
        .speed_hz = 16000000,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

/**
 * @brief 从字库芯片读取一个字节 (全双工 SPI: 发送 0x00，读取 MISO)
 */
uint8_t ZK_read_byte(void)
{
    int fd = lcd_get_spi_fd();
    if (fd < 0) return 0;

    uint8_t tx = 0x00;
    uint8_t rx = 0x00;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)&tx,
        .rx_buf = (unsigned long)&rx,
        .len = 1,
        .speed_hz = 16000000,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    return rx;
}

/**
 * @brief 从字库芯片读取 N 个字节
 * @param AddrHigh  地址高字节 (24位地址)
 * @param AddrMid   地址中字节
 * @param AddrLow   地址低字节
 * @param pBuff     读取数据缓冲区
 * @param DataLen   读取数据长度
 *
 * 时序: CS低 -> 发送 0x03 (读命令) -> 发送 3字节地址 -> 读取数据 -> CS高
 */
void ZK_read_n_bytes(uint8_t AddrHigh, uint8_t AddrMid, uint8_t AddrLow,
                     uint8_t *pBuff, uint8_t DataLen)
{
    int fd = lcd_get_spi_fd();
    if (fd < 0) return;

    // 单次传输: 命令+地址+数据读取, CS保持低电平全程
    int total = 4 + DataLen;
    uint8_t tx[4 + 130];  // max DataLen=130
    uint8_t rx[4 + 130];
    memset(tx, 0, total);
    memset(rx, 0, total);

    tx[0] = 0x03;         // 读命令
    tx[1] = AddrHigh;     // 地址
    tx[2] = AddrMid;
    tx[3] = AddrLow;
    // tx[4..] 保持 0x00 (dummy bytes)

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = (uint32_t)total,
        .speed_hz = 8000000,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);

    // 数据从第3个字节开始 (GT20L16S1Y字库芯片协议: cmd 1B + addr 3B, 数据在第3字节)
    memcpy(pBuff, rx + 2, DataLen);
}

// ==================== GB2312 汉字显示 ====================

/**
 * @brief 显示单个 GB2312 汉字 (字库芯片数据已在 FontBuf 中)
 */
void Display_GB2312(uint16_t x, uint16_t y, uint8_t zk_num, uint16_t fc, uint16_t bc)
{
    uint8_t i, k;
    switch (zk_num) {
    case 1:  // 12x12
        lcd_set_address(x, y, x + 15, y + 11);
        for (i = 0; i < 24; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;

    case 2:  // 16x16
        lcd_set_address(x, y, x + 15, y + 15);
        for (i = 0; i < 32; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;

    case 3:  // 24x24
        lcd_set_address(x, y, x + 23, y + 23);
        for (i = 0; i < 72; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;

    case 4:  // 32x32
        lcd_set_address(x, y, x + 31, y + 31);
        for (i = 0; i < 128; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    }
}

/**
 * @brief 显示 GB2312 中文字符串 (从字库芯片读取)
 */
void Display_GB2312_String(uint16_t x, uint16_t y, uint8_t zk_num, uint8_t text[],
                           uint16_t fc, uint16_t bc)
{
    uint8_t i = 0;
    uint8_t AddrHigh, AddrMid, AddrLow;
    uint32_t FontAddr = 0;
    uint32_t BaseAdd = 0;
    uint8_t n, d;

    switch (zk_num) {
    case 1:  BaseAdd = 0x00;     n = 24;  d = 12; break;  // 12x12
    case 2:  BaseAdd = 0x2C9D0;  n = 32;  d = 16; break;  // 15x16
    case 3:  BaseAdd = 0x68190;  n = 72;  d = 24; break;  // 24x24
    case 4:  BaseAdd = 0xEDF00;  n = 128; d = 32; break;  // 32x32
    default: return;
    }

    while ((text[i] > 0x00)) {
        if (((text[i] >= 0xA1) && (text[i] <= 0xA9)) && (text[i + 1] >= 0xA1)) {
            // GB2312 一级汉字
            FontAddr = (text[i] - 0xA1) * 94;
            FontAddr += (text[i + 1] - 0xA1);
            FontAddr = (unsigned long)((FontAddr * n) + BaseAdd);

            AddrHigh = (FontAddr & 0xFF0000) >> 16;
            AddrMid  = (FontAddr & 0xFF00) >> 8;
            AddrLow  = FontAddr & 0xFF;
            ZK_read_n_bytes(AddrHigh, AddrMid, AddrLow, FontBuf, n);
            Display_GB2312(x, y, zk_num, fc, bc);
        } else if (((text[i] >= 0xB0) && (text[i] <= 0xF7)) && (text[i + 1] >= 0xA1)) {
            // GB2312 二级汉字
            FontAddr = (text[i] - 0xB0) * 94;
            FontAddr += (text[i + 1] - 0xA1) + 846;
            FontAddr = (unsigned long)((FontAddr * n) + BaseAdd);

            AddrHigh = (FontAddr & 0xFF0000) >> 16;
            AddrMid  = (FontAddr & 0xFF00) >> 8;
            AddrLow  = FontAddr & 0xFF;
            ZK_read_n_bytes(AddrHigh, AddrMid, AddrLow, FontBuf, n);
            Display_GB2312(x, y, zk_num, fc, bc);
        }
        x += d;
        i += 2;
    }
}

// ==================== ASCII 显示 (字库芯片) ====================

/**
 * @brief 显示单个 ASCII 字符 (字库芯片数据已在 FontBuf 中)
 */
void Display_Asc(uint16_t x, uint16_t y, uint8_t zk_num, uint16_t fc, uint16_t bc)
{
    uint8_t i, k;
    switch (zk_num) {
    case 1:  // 5x7
        lcd_set_address(x, y, x + 7, y + 7);
        for (i = 0; i < 7; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 2:  // 7x8
        lcd_set_address(x, y, x + 7, y + 7);
        for (i = 0; i < 8; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 3:  // 6x12
        lcd_set_address(x, y, x + 7, y + 11);
        for (i = 0; i < 12; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 4:  // 8x16
        lcd_set_address(x, y, x + 7, y + 15);
        for (i = 0; i < 16; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 5:  // 12x24
        lcd_set_address(x, y, x + 15, y + 24);
        for (i = 0; i < 48; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 6:  // 16x32
        lcd_set_address(x, y, x + 15, y + 31);
        for (i = 0; i < 64; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    }
}

/**
 * @brief 显示 ASCII 字符串 (从字库芯片读取)
 */
void Display_Asc_String(uint16_t x, uint16_t y, uint16_t zk_num, uint8_t text[],
                        uint16_t fc, uint16_t bc)
{
    uint8_t i = 0;
    uint8_t AddrHigh, AddrMid, AddrLow;
    uint32_t FontAddr = 0;
    uint32_t BaseAdd = 0;
    uint8_t n, d;

    switch (zk_num) {
    case 1: BaseAdd = 0x1DDF80; n = 8;  d = 6;  break;  // 5x7
    case 2: BaseAdd = 0x1DE280; n = 8;  d = 8;  break;  // 7x8
    case 3: BaseAdd = 0x1DBE00; n = 12; d = 6;  break;  // 6x12
    case 4: BaseAdd = 0x1DD780; n = 16; d = 8;  break;  // 8x16
    case 5: BaseAdd = 0x1DFF00; n = 48; d = 12; break;  // 12x24
    case 6: BaseAdd = 0x1E5A50; n = 64; d = 16; break;  // 16x32
    default: return;
    }

    while ((text[i] > 0x00)) {
        if ((text[i] >= 0x20) && (text[i] <= 0x7E)) {
            FontAddr = text[i] - 0x20;
            FontAddr = (unsigned long)((FontAddr * n) + BaseAdd);

            AddrHigh = (FontAddr & 0xFF0000) >> 16;
            AddrMid  = (FontAddr & 0xFF00) >> 8;
            AddrLow  = FontAddr & 0xFF;
            ZK_read_n_bytes(AddrHigh, AddrMid, AddrLow, FontBuf, n);
            Display_Asc(x, y, zk_num, fc, bc);
        }
        i++;
        x += d;
    }
}

// ==================== Arial & Times New Roman ====================

/**
 * @brief 显示单个 Arial/TimesNewRoman 字符 (字库芯片数据已在 FontBuf 中)
 */
void Display_Arial_TimesNewRoman(uint16_t x, uint16_t y, uint8_t zk_num,
                                  uint16_t fc, uint16_t bc)
{
    uint8_t i, k;
    switch (zk_num) {
    case 1:  // 8x12
        lcd_set_address(x, y, x + 15, y + 12);
        for (i = 2; i < 26; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 2:  // 12x16
        lcd_set_address(x, y, x + 15, y + 17);
        for (i = 2; i < 34; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 3:  // 16x24
        lcd_set_address(x, y, x + 23, y + 23);
        for (i = 2; i < 74; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    case 4:  // 24x32
        lcd_set_address(x, y, x + 31, y + 31);
        for (i = 2; i < 130; i++) {
            for (k = 0; k < 8; k++) {
                if ((FontBuf[i] & (0x80 >> k)) != 0)
                    lcd_write_data16(fc);
                else
                    lcd_write_data16(bc);
            }
        }
        break;
    }
}

/**
 * @brief 显示 Arial 字体字符串 (从字库芯片读取)
 */
void Display_Arial_String(uint16_t x, uint16_t y, uint16_t zk_num, uint8_t text[],
                          uint16_t fc, uint16_t bc)
{
    uint8_t i = 0;
    uint8_t AddrHigh, AddrMid, AddrLow;
    uint32_t FontAddr = 0;
    uint32_t BaseAdd = 0;
    uint8_t n, d;

    switch (zk_num) {
    case 1: BaseAdd = 0x1DC400; n = 26;  d = 8;  break;  // 8x12 Arial
    case 2: BaseAdd = 0x1DE580; n = 34;  d = 12; break;  // 12x16 Arial
    case 3: BaseAdd = 0x1E22D0; n = 74;  d = 16; break;  // 16x24 Arial
    case 4: BaseAdd = 0x1E99D0; n = 130; d = 24; break;  // 24x32 Arial
    default: return;
    }

    while ((text[i] > 0x00)) {
        if ((text[i] >= 0x20) && (text[i] <= 0x7E)) {
            FontAddr = text[i] - 0x20;
            FontAddr = (unsigned long)((FontAddr * n) + BaseAdd);

            AddrHigh = (FontAddr & 0xFF0000) >> 16;
            AddrMid  = (FontAddr & 0xFF00) >> 8;
            AddrLow  = FontAddr & 0xFF;
            ZK_read_n_bytes(AddrHigh, AddrMid, AddrLow, FontBuf, n);
            Display_Arial_TimesNewRoman(x, y, zk_num, fc, bc);
        }
        i++;
        x += d;
    }
}

/**
 * @brief 显示 Times New Roman 字体字符串 (从字库芯片读取)
 */
void Display_TimesNewRoman_String(uint16_t x, uint16_t y, uint16_t zk_num,
                                  uint8_t text[], uint16_t fc, uint16_t bc)
{
    uint8_t i = 0;
    uint8_t AddrHigh, AddrMid, AddrLow;
    uint32_t FontAddr = 0;
    uint32_t BaseAdd = 0;
    uint8_t n, d;

    switch (zk_num) {
    case 1: BaseAdd = 0x1DCDC0; n = 26;  d = 8;  break;  // 8x12 TimesNewRoman
    case 2: BaseAdd = 0x1DF240; n = 34;  d = 12; break;  // 12x16 TimesNewRoman
    case 3: BaseAdd = 0x1E3E90; n = 74;  d = 16; break;  // 16x24 TimesNewRoman
    case 4: BaseAdd = 0x1ECA90; n = 130; d = 24; break;  // 24x32 TimesNewRoman
    default: return;
    }

    while ((text[i] > 0x00)) {
        if ((text[i] >= 0x20) && (text[i] <= 0x7E)) {
            FontAddr = text[i] - 0x20;
            FontAddr = (unsigned long)((FontAddr * n) + BaseAdd);

            AddrHigh = (FontAddr & 0xFF0000) >> 16;
            AddrMid  = (FontAddr & 0xFF00) >> 8;
            AddrLow  = FontAddr & 0xFF;
            ZK_read_n_bytes(AddrHigh, AddrMid, AddrLow, FontBuf, n);
            Display_Arial_TimesNewRoman(x, y, zk_num, fc, bc);
        }
        i++;
        x += d;
    }
}
