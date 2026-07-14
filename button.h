/**
 * button.h — TM1650 4x4 矩阵按键
 */
#ifndef BUTTON_H
#define BUTTON_H

#define BTN_NONE     0
#define BTN_SHORT    1   /* 短按 */
#define BTN_LONG     2   /* 长按 */

int  button_init(int gpio_pin);
int  button_read(void);           /* BTN_NONE/SHORT/LONG */
int  button_key(void);            /* 最后按下的键号 0-15 */
void button_reset(void);          /* 强制重置状态机到 IDLE */
void button_deinit(void);

#endif
