/**
 * tft_ui.c — TFT 8元件 (128x160 ST7735S)
 * R/C/D/LED/Pot/Connecter/Xtal/IC + 电路符号
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lcd.h"
#include "zk.h"
#include "tft_display.h"
#include "tft_ui.h"

static const struct { int mid; char *name; uint16_t color; } g_comp[] = {
    { 3,"Resistor ", GREEN   }, { 0,"Capacitor", BLUE    },
    { 1,"Diode    ", RED     }, { 4,"LED      ", MAGENTA },
    {11,"Pot      ", YELLOW  }, {12,"Connecter", 0x4BFF  },
    {13,"Xtal     ", CYAN    }, {14,"IC       ", WHITE   },
};
#define N 8

static void s_r(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+3,y+5,c); lcd_draw_line(x+3,y+5,x+5,y+1,c);
    lcd_draw_line(x+5,y+1,x+8,y+10,c); lcd_draw_line(x+8,y+10,x+11,y+1,c);
    lcd_draw_line(x+11,y+1,x+14,y+10,c); lcd_draw_line(x+14,y+10,x+17,y+5,c);
    lcd_draw_line(x+17,y+5,x+20,y+5,c);
}
static void s_c(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+6,y+5,c); lcd_draw_line(x+14,y+5,x+20,y+5,c);
    lcd_draw_line(x+6,y+1,x+6,y+10,c); lcd_draw_line(x+14,y+1,x+14,y+10,c);
}
static void s_d(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+5,y+5,c); lcd_draw_line(x+5,y+1,x+12,y+5,c);
    lcd_draw_line(x+5,y+10,x+12,y+5,c); lcd_draw_line(x+12,y+1,x+12,y+10,c);
    lcd_draw_line(x+12,y+5,x+20,y+5,c);
}
static void s_l(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+4,y+5,c); lcd_draw_line(x+4,y+1,x+10,y+5,c);
    lcd_draw_line(x+4,y+10,x+10,y+5,c); lcd_draw_line(x+10,y+1,x+10,y+10,c);
    lcd_draw_line(x+6,y+4,x+8,y+7,c); lcd_draw_line(x+6,y+6,x+8,y+9,c);
    lcd_draw_line(x+10,y+5,x+18,y+5,c);
}
static void s_p(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+3,y+5,c); lcd_draw_line(x+3,y+5,x+5,y+1,c);
    lcd_draw_line(x+5,y+1,x+8,y+10,c); lcd_draw_line(x+8,y+10,x+11,y+1,c);
    lcd_draw_line(x+11,y+1,x+13,y+5,c); lcd_draw_line(x+13,y+5,x+17,y+5,c);
    lcd_draw_line(x+10,y,x+13,y-3,c); lcd_draw_line(x+13,y-3,x+15,y,c);
    lcd_draw_line(x+13,y-3,x+13,y+3,c);
}
static void s_n(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_rectangle(x+6,y+1,x+14,y+10,c);
    lcd_draw_line(x,y+3,x+6,y+3,c); lcd_draw_line(x,y+8,x+6,y+8,c);
    lcd_draw_line(x+14,y+3,x+20,y+3,c); lcd_draw_line(x+14,y+8,x+20,y+8,c);
}
static void s_x(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_line(x,y+5,x+3,y+5,c); lcd_draw_rectangle(x+3,y+1,x+17,y+10,c);
    lcd_draw_line(x+17,y+5,x+20,y+5,c);
    lcd_draw_line(x+7,y+3,x+7,y+8,c); lcd_draw_line(x+13,y+3,x+13,y+8,c);
}
static void s_i(uint16_t x,uint16_t y,uint16_t c){
    lcd_draw_rectangle(x+3,y+1,x+17,y+10,c);
    lcd_draw_line(x,y+5,x+3,y+5,c); lcd_draw_line(x+17,y+5,x+20,y+5,c);
    lcd_draw_line(x+6,y,x+6,y+1,c); lcd_draw_line(x+10,y,x+10,y+1,c);
    lcd_draw_line(x+14,y,x+14,y+1,c);
    lcd_draw_line(x+6,y+10,x+6,y+11,c); lcd_draw_line(x+10,y+10,x+10,y+11,c);
    lcd_draw_line(x+14,y+10,x+14,y+11,c);
}
static void (*g_sym[])(uint16_t,uint16_t,uint16_t) = {s_r,s_c,s_d,s_l,s_p,s_n,s_x,s_i};

void tft_ui_splash(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,20,DARKBLUE);
    lcd_show_string(16,2,(const uint8_t*)"ELEC COMP AI",WHITE,DARKBLUE,16,0);
    lcd_show_string(8,40,(const uint8_t*)"RV1126B YOLOv8",0x9CF3,BLACK,12,0);
    lcd_show_string(8,60,(const uint8_t*)"R/C/D/LED/Pot/Con",0x7D7C,BLACK,12,0);
    lcd_show_string(8,75,(const uint8_t*)"Xtal/IC",0x7D7C,BLACK,12,0);
    lcd_show_string(8,100,(const uint8_t*)"Loading...",YELLOW,BLACK,12,0);
    lcd_show_string(8,130,(const uint8_t*)"v2.0",0x3186,BLACK,12,0);
}

void tft_ui_init(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,15,DARKBLUE);
    lcd_show_string(8,0,(const uint8_t*)"COMPONENT AI",WHITE,DARKBLUE,16,0);
    lcd_draw_line(0,15,LCD_W,15,0x4208);
}

void tft_ui_update(const int counts[15], int tf, int dam, int unk){
    char b[8]; int rh=14, y0=17;
    for(int i=0;i<N;i++){
        int y=y0+i*rh, cid=g_comp[i].mid, c=counts[cid];
        uint16_t bg=(i%2==0)?BLACK:0x1082, clr=c>0?g_comp[i].color:0x3186;
        uint16_t fg=(cid==tf)?YELLOW:clr;
        lcd_fill(0,y,LCD_W-1,y+rh-1,bg);
        if(i>0)lcd_draw_line(4,y,LCD_W-4,y,0x4208);
        snprintf(b,sizeof(b),"%d",c); lcd_show_string(2,y+1,(const uint8_t*)b,fg,bg,12,0);
        lcd_show_string(18,y+1,(const uint8_t*)g_comp[i].name,clr,bg,12,0);
        if(g_sym[i])g_sym[i](106,y+2,clr);
    }
    int sy=y0+N*rh+1; lcd_draw_line(0,sy,LCD_W,sy,0x4208);
    lcd_fill(0,sy+1,LCD_W,LCD_H,BLACK); int st=sy+3;
    if(dam){ int d=counts[5]+counts[6]+counts[10];
        snprintf(b,sizeof(b),"DAM:%d",d); lcd_show_string(2,st,(const uint8_t*)b,RED,BLACK,12,0); }
    else lcd_show_string(2,st,(const uint8_t*)"OK",GREEN,BLACK,12,0);
    if(unk>0){ snprintf(b,sizeof(b),"UNK:%d",unk); lcd_show_string(50,st,(const uint8_t*)b,YELLOW,BLACK,12,0); }
    if(tf>=0&&tf<=7){ static const char*fn[]={"R","C","D","LED","POT","CON","X","IC"};
        snprintf(b,sizeof(b),"FLT:%s",fn[tf]); lcd_show_string(2,st+13,(const uint8_t*)b,YELLOW,BLACK,12,0); }
}

void tft_ui_stt_listening(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,18,DARKBLUE);
    lcd_show_string(4,2,(const uint8_t*)"VOICE COMMAND",WHITE,DARKBLUE,16,0);
    lcd_draw_line(0,18,LCD_W,18,0x4208);
    lcd_show_string(10,45,(const uint8_t*)"Listening...",YELLOW,BLACK,16,0);
    lcd_show_string(10,70,(const uint8_t*)"Please speak",0x9CF3,BLACK,12,0);
    lcd_show_string(10,100,(const uint8_t*)"R/C/D/LED/Pot",0x7D7C,BLACK,12,0);
    lcd_show_string(10,115,(const uint8_t*)"Con/Xtal/IC",0x7D7C,BLACK,12,0);
    lcd_show_string(10,130,(const uint8_t*)"All/Unknown",0x7D7C,BLACK,12,0);
    lcd_fill(0,145,LCD_W,LCD_H,BLACK);
}

void tft_ui_stt_result(const char *text, const char *mode){
    (void)text; lcd_fill(0,40,LCD_W,LCD_H,BLACK);
    if(mode&&mode[0]){ lcd_show_string(10,50,(const uint8_t*)"Matched:",GREEN,BLACK,16,0);
        lcd_show_string(85,50,(const uint8_t*)mode,YELLOW,BLACK,16,0);
        lcd_show_string(10,80,(const uint8_t*)"[OK]",GREEN,BLACK,12,0); }
    else{ lcd_show_string(10,50,(const uint8_t*)"No match",RED,BLACK,16,0);
        lcd_show_string(10,80,(const uint8_t*)"Try again",0x3186,BLACK,12,0); }
}
