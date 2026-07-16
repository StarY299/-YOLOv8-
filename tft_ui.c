/**
 * tft_ui.c — TFT中文显示 (128x160 ST7735S + 内置字库)
 * 8元件: 电阻/电容/二极管/LED/电位器/连接器/晶振/芯片
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lcd.h"
#include "tft_display.h"
#include "tft_ui.h"

/* GB2312 编码的中文字符串 */
static const uint8_t CN_RESISTOR[]  = {0xB5,0xE7,0xD7,0xE8,0x00};       /* 电阻 */
static const uint8_t CN_CAPACITOR[]= {0xB5,0xE7,0xC8,0xDD,0x00};       /* 电容 */
static const uint8_t CN_DIODE[]    = {0xB6,0xFE,0xBC,0xAB,0xB9,0xDC,0x00}; /* 二极管 */
static const uint8_t CN_LED[]      = {0x4C,0x45,0x44,0x00}; /* LED (ASCII) */
static const uint8_t CN_POT[]      = {0xB5,0xE7,0xCE,0xBB,0xC6,0xF7,0x00}; /* 电位器 */
static const uint8_t CN_CONNECTER[]= {0xC1,0xAC,0xBD,0xD3,0xC6,0xF7,0x00}; /* 连接器 */
static const uint8_t CN_XTAL[]     = {0xBE,0xA7,0xD5,0xF1,0x00};       /* 晶振 */
static const uint8_t CN_IC[]       = {0xD0,0xBE,0xC6,0xAC,0x00};       /* 芯片 */
static const uint8_t CN_TRANSISTOR[]={0xC8,0xFD,0xBC,0xAB,0xB9,0xDC,0x00}; /* 三极管 */
static const uint8_t CN_INDUCTOR[]  ={0xB5,0xE7,0xB8,0xD0,0x00}; /* 电感 */
static const uint8_t *cn_names[] = {CN_RESISTOR,CN_CAPACITOR,CN_DIODE,CN_TRANSISTOR,CN_LED,CN_CONNECTER,CN_XTAL,CN_INDUCTOR};
static const uint8_t *cn_filter[] = {CN_RESISTOR,CN_CAPACITOR,CN_DIODE,CN_TRANSISTOR,CN_LED,CN_CONNECTER,CN_XTAL,CN_INDUCTOR};

static const uint8_t CN_SYSNAME[] = {0xD4,0xAA,0xC6,0xF7,0xBC,0xFE,0xCA,0xB6,0xB1,0xF0,0xD7,0xB0,0xD6,0xC3,0x00}; /* 元器件识别装置 */
static const uint8_t CN_TITLE[]    = {0xD4,0xAA,0xC6,0xF7,0xBC,0xFE,0x41,0x49,0x00}; /* 元器件AI */
static const uint8_t CN_NORMAL[]   = {0xD5,0xFD,0xB3,0xA3,0x00};       /* 正常 */
static const uint8_t CN_DAMAGED[]  = {0xC8,0xB1,0xCB,0xF0,0x00};       /* 缺损 */
static const uint8_t CN_UNKNOWN[]  = {0xCE,0xB4,0xD6,0xAA,0x00};       /* 未知 */
static const uint8_t CN_FILTER[]   = {0xB9,0xFD,0xC2,0xCB,0x00};       /* 过滤 */

static const uint8_t CN_SPLASH[]   = {0xB5,0xE7,0xD7,0xD3,0xD4,0xAA,0xC6,0xF7,0xBC,0xFE,0xCA,0xB6,0xB1,0xF0,0x00}; /* 电子元器件识别 */
static const uint8_t CN_SPLASH2[]  = {0xB5,0xE7,0xD7,0xE8,0xB5,0xE7,0xC8,0xDD,0xB6,0xFE,0xBC,0xAB,0xB9,0xDC,0x4C,0x45,0x44,0x00}; /* 电阻电容二极管LED */
static const uint8_t CN_SPLASH3[]  = {0xB5,0xE7,0xCE,0xBB,0xC6,0xF7,0xC1,0xAC,0xBD,0xD3,0xC6,0xF7,0xBE,0xA7,0xD5,0xF1,0xD0,0xBE,0xC6,0xAC,0x00}; /* 电位器连接器晶振芯片 */
static const uint8_t CN_LOADING[]  = {0xD5,0xFD,0xD4,0xDA,0xBC,0xD3,0xD4,0xD8,0xC4,0xA3,0xD0,0xCD,0x00}; /* 正在加载模型 */

static const uint8_t CN_STT_TITLE[]={0xD3,0xEF,0xD2,0xF4,0xC3,0xFC,0xC1,0xEE,0x00}; /* 语音命令 */
static const uint8_t CN_LISTEN[]   = {0xD5,0xFD,0xD4,0xDA,0xF1,0xF6,0xCC,0xFD,0x00}; /* 正在聆听 */
static const uint8_t CN_HINT[]     = {0xC7,0xEB,0xCB,0xB5,0xB3,0xF6,0xB9,0xD8,0xBC,0xFC,0xB4,0xCA,0x00}; /* 请说出关键词 */
static const uint8_t CN_MATCHED[]  = {0xD2,0xD1,0xC6,0xA5,0xC5,0xE4,0x00}; /* 已匹配 */
static const uint8_t CN_OK[]       = {0xC8,0xB7,0xC8,0xCF,0x00};       /* 确认 */
static const uint8_t CN_NOMATCH[]  = {0xCE,0xDE,0xC6,0xA5,0xC5,0xE4,0x00}; /* 无匹配 */
static const uint8_t CN_RETRY[]    = {0xC7,0xEB,0xD6,0xD8,0xCA,0xD4,0x00}; /* 请重试 */
static const uint8_t CN_ALLUNK[]   = {0xC8,0xAB,0xB2,0xBF,0xCE,0xB4,0xD6,0xAA,0x00}; /* 全部未知 */

static const int g_comp_id[] = {3,0,1,2,4,12,13,15};
static const uint16_t g_colors[] = {GREEN,BLUE,0xFD20,YELLOW,MAGENTA,CYAN,WHITE,0xFC10};  /* R/C/D/T/LED/Con/Xtal/L */
#define N 8

/* 电路符号 */
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

/* 辅助: 显示中文或ASCII */
/* 显示GB2312中文, 未找到则用ASCII回退 */
static void show_cn(uint16_t x,uint16_t y,const uint8_t *s,uint16_t f,uint16_t b)
    { lcd_show_cn12_custom(x,y,s,f,b); }  /* 12x12 */
static void show_cn16(uint16_t x,uint16_t y,const uint8_t *s,uint16_t f,uint16_t b)
    { lcd_show_cn_custom(x,y,s,f,b); }   /* 16x16 */
static void show_asc(uint16_t x,uint16_t y,const uint8_t *s,uint16_t f,uint16_t b)
    { lcd_show_string(x,y,s,f,b,12,0); }

void tft_ui_splash(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,20,DARKBLUE);
    show_cn16(16,2,CN_SPLASH,WHITE,DARKBLUE);
    lcd_show_string(8,35,(const uint8_t*)"RV1126B YOLOv8",0x9CF3,BLACK,12,0);
    show_cn(8,55,CN_SPLASH2,0x7D7C,BLACK);
    show_cn(8,70,CN_SPLASH3,0x7D7C,BLACK);
    show_cn(8,100,CN_LOADING,YELLOW,BLACK);
    lcd_show_string(8,130,(const uint8_t*)"v2.0",0x3186,BLACK,12,0);
}

void tft_ui_init(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,15,DARKBLUE);
    show_cn16(4,0,CN_SYSNAME,WHITE,DARKBLUE);
    lcd_draw_line(0,15,LCD_W,15,0x4208);
}

void tft_ui_update(const int counts[16], int tf, int dam, int unk){
    char b[8]; int rh=14, y0=17;
    /* 重绘标题栏(覆盖STT页面残留) */
    lcd_fill(0,0,LCD_W,15,DARKBLUE);
    show_cn16(4,0,CN_SYSNAME,WHITE,DARKBLUE);
    lcd_draw_line(0,15,LCD_W,15,0x4208);

    for(int i=0;i<N;i++){
        int y=y0+i*rh, cid=g_comp_id[i], c=counts[cid];
        uint16_t bg=(i%2==0)?BLACK:0x1082, clr=c>0?g_colors[i]:0x3186;
        uint16_t fg=(cid==tf)?YELLOW:clr;
        lcd_fill(0,y,LCD_W-1,y+rh-1,bg);
        if(i>0)lcd_draw_line(4,y,LCD_W-4,y,0x4208);
        snprintf(b,sizeof(b),"%d",c);
        show_asc(2,y+1,(uint8_t*)b,fg,bg);
        if(i==4) show_asc(20,y,(uint8_t*)"LED",clr,bg); else show_cn(16,y,cn_names[i],clr,bg);
        if(g_sym[i])g_sym[i](106,y+2,clr);
    }
    int sy=y0+N*rh+1; lcd_draw_line(0,sy,LCD_W,sy,0x4208);
    lcd_fill(0,sy+1,LCD_W,LCD_H,BLACK); int st=sy+3;
    if(dam){ int d=counts[5]+counts[6]+counts[10];
        snprintf(b,sizeof(b),":%d",d);
        show_cn(2,st,CN_DAMAGED,RED,BLACK); show_asc(30,st,(uint8_t*)b,RED,BLACK); }
    else show_cn(2,st,CN_NORMAL,GREEN,BLACK);
    if(unk>0){ snprintf(b,sizeof(b),":%d",unk);
        show_cn(45,st,CN_UNKNOWN,YELLOW,BLACK); show_asc(68,st,(uint8_t*)b,YELLOW,BLACK); }
    if(tf>=0&&tf<=7){
        show_cn(2,st+13,CN_FILTER,YELLOW,BLACK); show_cn(36,st+13,cn_filter[tf],YELLOW,BLACK); }
}

void tft_ui_stt_listening(void){
    lcd_fill(0,0,LCD_W,LCD_H,BLACK); lcd_fill(0,0,LCD_W,18,DARKBLUE);
    show_cn16(4,1,CN_STT_TITLE,WHITE,DARKBLUE);
    lcd_draw_line(0,18,LCD_W,18,0x4208);
    show_cn16(10,40,CN_LISTEN,YELLOW,BLACK);
    show_cn(10,65,CN_HINT,0x9CF3,BLACK);
    show_cn(10,95,CN_SPLASH2,0x7D7C,BLACK);
    show_cn(10,110,CN_SPLASH3,0x7D7C,BLACK);
    show_cn(10,125,CN_ALLUNK,0x7D7C,BLACK);
}

void tft_ui_stt_result(const char *text, const char *mode){
    (void)text; lcd_fill(0,40,LCD_W,LCD_H,BLACK);
    if(mode&&mode[0]){
        /* 将英文模式名映射为中文 */
        const uint8_t *cn_mode = NULL;
        if(strstr(mode,"Resistor")) cn_mode=CN_RESISTOR;
        else if(strstr(mode,"Capacitor")) cn_mode=CN_CAPACITOR;
        else if(strstr(mode,"Diode")) cn_mode=CN_DIODE;
        else if(strstr(mode,"LED")) cn_mode=CN_LED;
        else if(strstr(mode,"Pot")) cn_mode=CN_POT;
        else if(strstr(mode,"Connecter")) cn_mode=CN_CONNECTER;
        else if(strstr(mode,"Xtal")) cn_mode=CN_XTAL;
        else if(strstr(mode,"IC")) cn_mode=CN_IC;
        else if(strstr(mode,"All")) cn_mode=NULL;
        show_cn16(10,45,CN_MATCHED,GREEN,BLACK);
        if(cn_mode) show_cn16(80,45,cn_mode,YELLOW,BLACK);
        show_cn16(10,75,CN_OK,GREEN,BLACK);
    }else{
        show_cn16(10,45,CN_NOMATCH,RED,BLACK);
        show_cn16(10,75,CN_RETRY,0x3186,BLACK);
    }
}
