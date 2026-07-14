/**
 * tft_ui.c — 元器件计数 (128x160 ST7735S)
 *
 * 6种元件: R/C/D/LED/Pot/Connecter
 *
 * 布局:
 *   ┌──────────────────────┐ y=0
 *   │   COMPONENT  AI      │ 标题 DARKBLUE 16px
 *   ├──────────────────────┤ y=17
 *   │ 5 Resistor   ╱╲╱╲    │ 行高19px ×6=114px
 *   │ 8 Capacitor  ─┤├─    │ 数量+名称+符号
 *   │ 2 Diode      ─▷├─    │
 *   │ 3 LED        ─▷├→    │
 *   │ 1 Pot                │
 *   │ 2 Connecter          │
 *   ├──────────────────────┤ y=134
 *   │ OK  DAM:2  UNK:3     │ 状态栏 14px
 *   │ FLT:RESISTOR         │ 过滤(有过滤时)
 *   └──────────────────────┘ y=160
 */

#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "tft_display.h"
#include "tft_ui.h"

static const struct {
    int mid; char *name; uint16_t color;
} g_comp[] = {
    { 3,"Resistor ", GREEN   },
    { 0,"Capacitor", BLUE    },
    { 1,"Diode    ", RED     },
    { 4,"LED      ", MAGENTA },
    {11,"Pot      ", YELLOW  },
    {12,"Connecter", 0x7D7C  },
};
#define N 6

/* 电路符号 (前4个) */
static void s_r(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+6,x+3,y+6,c); lcd_draw_line(x+3,y+6,x+5,y+2,c);
    lcd_draw_line(x+5,y+2,x+8,y+11,c); lcd_draw_line(x+8,y+11,x+11,y+2,c);
    lcd_draw_line(x+11,y+2,x+14,y+11,c); lcd_draw_line(x+14,y+11,x+17,y+6,c);
    lcd_draw_line(x+17,y+6,x+20,y+6,c);
}
static void s_c(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+6,x+6,y+6,c); lcd_draw_line(x+14,y+6,x+20,y+6,c);
    lcd_draw_line(x+6,y+2,x+6,y+11,c); lcd_draw_line(x+14,y+2,x+14,y+11,c);
}
static void s_d(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+6,x+5,y+6,c); lcd_draw_line(x+5,y+2,x+12,y+6,c);
    lcd_draw_line(x+5,y+11,x+12,y+6,c); lcd_draw_line(x+12,y+2,x+12,y+11,c);
    lcd_draw_line(x+12,y+6,x+20,y+6,c);
}
static void s_l(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+6,x+4,y+6,c); lcd_draw_line(x+4,y+2,x+10,y+6,c);
    lcd_draw_line(x+4,y+11,x+10,y+6,c); lcd_draw_line(x+10,y+2,x+10,y+11,c);
    lcd_draw_line(x+6,y+5,x+8,y+8,c); lcd_draw_line(x+6,y+7,x+8,y+10,c);
    lcd_draw_line(x+10,y+6,x+18,y+6,c);
}
static void s_p(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+6,x+3,y+6,c); lcd_draw_line(x+3,y+6,x+5,y+2,c);
    lcd_draw_line(x+5,y+2,x+8,y+11,c); lcd_draw_line(x+8,y+11,x+11,y+2,c);
    lcd_draw_line(x+11,y+2,x+13,y+6,c); lcd_draw_line(x+13,y+6,x+17,y+6,c);
    /* 箭头 */
    lcd_draw_line(x+10,y+1,x+13,y-2,c); lcd_draw_line(x+13,y-2,x+15,y+1,c);
    lcd_draw_line(x+13,y-2,x+13,y+4,c);
}
static void s_n(uint16_t x,uint16_t y,uint16_t c){
    /* 长方形主体 */
    lcd_draw_rectangle(x+6,y+2,x+14,y+11,c);
    /* 左侧引脚 */
    lcd_draw_line(x,y+4,x+6,y+4,c);
    lcd_draw_line(x,y+9,x+6,y+9,c);
    /* 右侧引脚 */
    lcd_draw_line(x+14,y+4,x+20,y+4,c);
    lcd_draw_line(x+14,y+9,x+20,y+9,c);
}
static void (*g_sym[])(uint16_t,uint16_t,uint16_t) = {s_r,s_c,s_d,s_l,s_p,s_n};

void tft_ui_init(void)
{
    lcd_fill(0,0,LCD_W,LCD_H,BLACK);
    lcd_fill(0,0,LCD_W,16,DARKBLUE);
    lcd_show_string(8,1,(const uint8_t*)"COMPONENT AI",WHITE,DARKBLUE,16,0);
    lcd_draw_line(0,16,LCD_W,16,0x4208);
}


void tft_ui_stt_listening(void)
{
    lcd_fill(0,0,LCD_W,LCD_H,BLACK);
    lcd_fill(0,0,LCD_W,18,DARKBLUE);
    lcd_show_string(4,2,(const uint8_t*)"VOICE COMMAND",WHITE,DARKBLUE,16,0);
    lcd_draw_line(0,18,LCD_W,18,0x4208);
    lcd_show_string(10,40,(const uint8_t*)"Listening...",YELLOW,BLACK,16,0);
    lcd_show_string(10,60,(const uint8_t*)"Please speak",0x9CF3,BLACK,12,0);
    lcd_show_string(10,100,(const uint8_t*)"Key words:",0x7D7C,BLACK,12,0);
    lcd_show_string(10,116,(const uint8_t*)"R/C/D/LED/Pot/Con",0x7D7C,BLACK,12,0);
    lcd_show_string(10,130,(const uint8_t*)"All/Unknown",0x7D7C,BLACK,12,0);
    lcd_fill(0,145,LCD_W,LCD_H,BLACK);
}

void tft_ui_stt_result(const char *text, const char *mode)
{
    (void)text;
    lcd_fill(0,40,LCD_W,LCD_H,BLACK);
    if(mode&&mode[0]){
        lcd_show_string(10,45,(const uint8_t*)"Matched:",GREEN,BLACK,16,0);
        lcd_show_string(10,65,(const uint8_t*)mode,YELLOW,BLACK,16,0);
        lcd_show_string(10,90,(const uint8_t*)"[OK]",GREEN,BLACK,12,0);
    }else{
        lcd_show_string(10,45,(const uint8_t*)"No match",RED,BLACK,16,0);
        lcd_show_string(10,70,(const uint8_t*)"Try again",0x3186,BLACK,12,0);
    }
}
void tft_ui_update(const int counts[13], int text_filter,
                   int has_damaged, int has_unknown)
{
    char b[16];
    int rh=19, y0=18;

    for(int i=0;i<N;i++){
        int y=y0+i*rh, cid=g_comp[i].mid, c=counts[cid];
        uint16_t bg=(i%2==0)?BLACK:0x1082;
        uint16_t clr=c>0?g_comp[i].color:0x3186;
        uint16_t fg=(cid==text_filter)?YELLOW:clr;

        lcd_fill(0,y,LCD_W-1,y+rh-1,bg);
        if(i>0)lcd_draw_line(8,y,LCD_W-8,y,0x4208);

        /* 数量 */
        snprintf(b,sizeof(b),"%d",c);
        lcd_show_string(2,y+3,(const uint8_t*)b,fg,bg,16,0);

        /* 名称 */
        lcd_show_string(22,y+3,(const uint8_t*)g_comp[i].name,clr,bg,16,0);

        /* 符号 */
        if(g_sym[i])g_sym[i](106,y+5,clr);
    }

    int sy=y0+N*rh+1;
    lcd_draw_line(0,sy,LCD_W,sy,0x4208);
    lcd_fill(0,sy+1,LCD_W,LCD_H,BLACK);

    /* 状态行 */
    int st=sy+3;
    if(has_damaged){
        int d=counts[5]+counts[6]+counts[10];
        snprintf(b,sizeof(b),"DAM:%d",d);
        lcd_show_string(2,st,(const uint8_t*)b,RED,BLACK,12,0);
    }else{
        lcd_show_string(2,st,(const uint8_t*)"OK",GREEN,BLACK,12,0);
    }

    if(has_unknown>0){
        snprintf(b,sizeof(b),"UNK:%d",has_unknown);
        lcd_show_string(45,st,(const uint8_t*)b,YELLOW,BLACK,12,0);
    }

    if(text_filter>=0&&text_filter<=5){
        static const char*fn[]={"RESISTOR","CAPACITOR","DIODE","LED","POT","CONNECTER"};
        snprintf(b,sizeof(b),"FLT:%s",fn[text_filter]);
        lcd_show_string(2,st+14,(const uint8_t*)b,YELLOW,BLACK,12,0);
    }
}
