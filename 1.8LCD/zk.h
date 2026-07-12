#ifndef __ZK_H
#define __ZK_H

#include <stdint.h>

// 字库芯片接口函数
// 字库芯片通过共享 SPI 总线与 LCD 连接，共用 MOSI/SCLK/CS，
// 字库数据通过 MISO 读回。

void ZK_init(void);
void ZK_command(uint8_t dat);
uint8_t ZK_read_byte(void);
void ZK_read_n_bytes(uint8_t AddrHigh, uint8_t AddrMid, uint8_t AddrLow,
                     uint8_t *pBuff, uint8_t DataLen);

// GB2312 汉字显示 (从字库芯片)
void Display_GB2312(uint16_t x, uint16_t y, uint8_t zk_num, uint16_t fc, uint16_t bc);
void Display_GB2312_String(uint16_t x, uint16_t y, uint8_t zk_num, uint8_t text[],
                           uint16_t fc, uint16_t bc);

// ASCII 显示 (从字库芯片)
void Display_Asc(uint16_t x, uint16_t y, uint8_t zk_num, uint16_t fc, uint16_t bc);
void Display_Asc_String(uint16_t x, uint16_t y, uint16_t zk_num, uint8_t text[],
                        uint16_t fc, uint16_t bc);

// Arial & Times New Roman 字体显示 (从字库芯片)
void Display_Arial_TimesNewRoman(uint16_t x, uint16_t y, uint8_t zk_num,
                                  uint16_t fc, uint16_t bc);
void Display_Arial_String(uint16_t x, uint16_t y, uint16_t zk_num, uint8_t text[],
                          uint16_t fc, uint16_t bc);
void Display_TimesNewRoman_String(uint16_t x, uint16_t y, uint16_t zk_num,
                                  uint8_t text[], uint16_t fc, uint16_t bc);

// 字号参数:
//   GB2312:    zk_num=1:12x12, 2:15x16, 3:24x24, 4:32x32
//   ASCII:     zk_num=1:5x7,  2:5x8,  3:6x12,  4:8x16, 5:12x24, 6:16x32
//   Arial:     zk_num=1:8x12, 2:12x16, 3:16x24, 4:24x32
//   TimesNew:  zk_num=1:8x12, 2:12x16, 3:16x24, 4:24x32

// 全局字库缓冲区
extern uint8_t FontBuf[130];

#endif
