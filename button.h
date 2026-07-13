/**
 * button.h — GPIO 按键检测
 */
#ifndef BUTTON_H
#define BUTTON_H

/* 按键事件 */
#define BTN_NONE     0
#define BTN_SHORT    1   /* 短按 (<1秒) */
#define BTN_LONG     2   /* 长按 (>2秒) */

int  button_init(int gpio_pin);
int  button_read(void);     /* 返回 BTN_NONE/BTN_SHORT/BTN_LONG, 消费后清零 */
void button_deinit(void);

#endif
