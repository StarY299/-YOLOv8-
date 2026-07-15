/**
 * main.c — RV1126B 电子元器件识别系统
 * 状态机: WAIT→JUDGE→TEXT/DAMAGED/GENERAL→WAIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "capture.h"
#include "rtsp_stream.h"
#include "rtsp_service.h"
#include "ai_processor.h"
#include "tft_display.h"
#include "tft_ui.h"
#include "voice_service.h"
#include "stt_service.h"
#include "button.h"
#include <sys/wait.h>

#define CAP_WIDTH        1920
#define CAP_HEIGHT       1080
#define CAP_FPS          30
#define H264_BITRATE     4000000
#define QUEUE_SIZE       4
#define MODEL_PATH       "/userdata/best-final-i8.rknn"

static volatile int running = 1;
static int filter_override = -1; /* 语音命令设置的过滤 */
static volatile int g_first_valid_frame = 0;
static pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_start_cond = PTHREAD_COND_INITIALIZER;
static uint8_t *g_anno_buf=NULL;static size_t g_anno_len=0;
static pthread_mutex_t g_anno_lock=PTHREAD_MUTEX_INITIALIZER;

typedef struct { uint8_t *data; size_t size; int64_t frame_id; } queued_frame_t;
typedef struct {
    queued_frame_t frames[QUEUE_SIZE]; int head,tail,count;
    pthread_mutex_t lock; pthread_cond_t cond;
} frame_queue_t;
static frame_queue_t g_queue;

static void queue_init(frame_queue_t *q) {
    memset(q,0,sizeof(*q)); pthread_mutex_init(&q->lock,NULL); pthread_cond_init(&q->cond,NULL);
}
static void queue_put(frame_queue_t *q, const uint8_t *data, size_t sz, int64_t id) {
    pthread_mutex_lock(&q->lock);
    while(q->count>=QUEUE_SIZE&&running)pthread_cond_wait(&q->cond,&q->lock);
    if(!running){pthread_mutex_unlock(&q->lock);return;}
    queued_frame_t *f=&q->frames[q->tail];
    f->data=(uint8_t*)malloc(sz);f->size=sz;f->frame_id=id;memcpy(f->data,data,sz);
    q->tail=(q->tail+1)%QUEUE_SIZE;q->count++;
    pthread_cond_signal(&q->cond);pthread_mutex_unlock(&q->lock);
}
static int queue_get(frame_queue_t *q, queued_frame_t *out) {
    pthread_mutex_lock(&q->lock);
    while(q->count==0&&running)pthread_cond_wait(&q->cond,&q->lock);
    if(!running&&q->count==0){pthread_mutex_unlock(&q->lock);return -1;}
    *out=q->frames[q->head];q->head=(q->head+1)%QUEUE_SIZE;q->count--;
    pthread_cond_signal(&q->cond);pthread_mutex_unlock(&q->lock);return 0;
}
static void *capture_thread(void *arg){(void)arg;
    while(running){capture_frame_t cap;if(capture_get_frame(&cap)!=0){usleep(100000);continue;}queue_put(&g_queue,cap.data,cap.size,cap.frame_id);}
    return NULL;
}
static void *ai_feed_thread(void *arg){(void)arg;
    int empty_streak = 0;
    while(running){queued_frame_t f;if(queue_get(&g_queue,&f)!=0)break;
        /* 空帧过滤 */
        if(f.size<256){
            empty_streak++;
            if(empty_streak%100==1)printf("[PUSH] skip empty frame streak=%d\n",empty_streak);
            free(f.data);continue;
        }
        /* 空帧恢复 → 重启 mediamtx */
        if(empty_streak>90){
            printf("[PUSH] frame recovered after %d empty, restarting mediamtx\n",empty_streak);
            system("killall -9 mediamtx 2>/dev/null; sleep 1; "
                   "/userdata/mediamtx /userdata/mediamtx.yml &");
        }
        empty_streak = 0;


        /* 更新标注帧缓存 */
        {
            size_t ns = 0;
            const uint8_t *nd = cv_branch_get_annotated_frame(&ns);
            if (nd && ns > 256) {
                pthread_mutex_lock(&g_anno_lock);
                free(g_anno_buf);
                g_anno_buf = (uint8_t *)malloc(ns);
                if (g_anno_buf) { memcpy(g_anno_buf, nd, ns); g_anno_len = ns; }
                else g_anno_len = 0;
                pthread_mutex_unlock(&g_anno_lock);
            }
        }
        /* 推送标注帧, 无缓存时回退原始帧 */
        {
            int pushed = 0;
            pthread_mutex_lock(&g_anno_lock);
            if (g_anno_buf && g_anno_len > 256) {
                gst_pipeline_push_frame(g_anno_buf, g_anno_len, f.frame_id);
                pushed = 1;
            }
            pthread_mutex_unlock(&g_anno_lock);
            if (!pushed)
                gst_pipeline_push_frame(f.data, f.size, f.frame_id);
        }

        /* 首帧信号 */
        if(!g_first_valid_frame){
            pthread_mutex_lock(&g_start_lock);
            g_first_valid_frame = 1;
            pthread_cond_signal(&g_start_cond);
            pthread_mutex_unlock(&g_start_lock);
        }

        /* 喂 AI */
        cv_frame_t cvf;cvf.data=f.data;cvf.size=f.size;cvf.width=CAP_WIDTH;cvf.height=CAP_HEIGHT;cvf.stride=0;
        cvf.format=CV_FMT_JPEG;cvf.timestamp_us=0;cvf.frame_id=f.frame_id;cv_branch_push_frame(&cvf);free(f.data);}
    return NULL;
}
static void on_ai_result(const cv_result_t *result, void *user_data){(void)user_data;
    if(!result||result->count==0)return;
    printf("[AI] frame %lld: %d detections\n",(long long)result->frame_id,result->count);
}
static void sig_handler(int sig){(void)sig;running=0;}

int main(void){
    printf("=== RV1126B Component Recognition System ===\n");
    signal(SIGTERM,sig_handler);signal(SIGINT,sig_handler);

    int stt_ok=(stt_init()==0);
    if(stt_ok){voice_play("stt_ready");printf("[MAIN] STT ready\n");}
    button_init(0);

    char cap_dev[128];if(capture_find_device(cap_dev,sizeof(cap_dev))!=0){fprintf(stderr,"FATAL: no camera\n");return -1;}
    if(capture_init(cap_dev,CAP_WIDTH,CAP_HEIGHT,CAP_FPS)!=0)return -1;
    if(gst_pipeline_init(CAP_WIDTH,CAP_HEIGHT,CAP_FPS,H264_BITRATE)!=0){capture_deinit();return -1;}

    cv_branch_config_t cv_cfg={.max_queue_size=2,.on_result=on_ai_result,.model_path=MODEL_PATH};
    cv_branch_init(&cv_cfg);
    if(tft_display_init()!=0)fprintf(stderr,"WARN: TFT\n");else tft_ui_init();

    queue_init(&g_queue);pthread_t cap_tid,feed_tid;
    pthread_create(&cap_tid,NULL,capture_thread,NULL);
    pthread_create(&feed_tid,NULL,ai_feed_thread,NULL);

    /* 等待首帧流入管线后启动 mediamtx */
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 15;
        pthread_mutex_lock(&g_start_lock);
        while (!g_first_valid_frame && running) {
            if (pthread_cond_timedwait(&g_start_cond, &g_start_lock, &ts) != 0)
                break;
        }
        pthread_mutex_unlock(&g_start_lock);
        printf("[MAIN] first frame %s, starting mediamtx\n",
               g_first_valid_frame ? "OK" : "TIMEOUT");
    }
    if(start_mediamtx()!=0)fprintf(stderr,"WARN: mediamtx\n");

    printf("\n=== System Ready (WAIT) ===\n");voice_ready();
    printf("Key 0=JUDGE  Key 2=voice command\n");

    int64_t tick=0;
    while(running){
        for(int i=0;i<50&&running;i++){usleep(20000);  /* 50Hz 按键轮询, 配合消抖状态机 */
            int btn=button_read(),key=button_key();
            if(btn==BTN_SHORT)printf("[BTN-EVT] key=%d\n",key);

            /* ---- JUDGE 播报 (按键13 或 语音命令自动触发) ---- */
            int do_judge = 0;

            /* 按键"0": 立即 JUDGE 播报 (key 13 = 0x5D = 第4行第2列) */
            if(btn==BTN_SHORT && key==13){ do_judge = 1; printf("[BTN] JUDGE trigger\n"); }

            /* 按键"2": 语音命令 → 自动 JUDGE (key 1 = 0x45 = 第1行第2列) */
            if(btn==BTN_SHORT && key==1 && stt_ok){
                printf("[MAIN] listening...\n");
                tft_ui_stt_listening();
                stt_start_listening();usleep(4000000);stt_pause_listening();
                usleep(200000);  /* 等待 ASR 处理完残留帧 */
                /* 语音监听期间按键状态冻结, 强制重置消抖状态机 */
                button_reset();
                /* 资源监控: 检测FD泄漏 */
                { int nz=0; FILE *fp=popen("ls /proc/$$/fd 2>/dev/null|wc -l","r");
                  if(fp){char zb[16]={0};fgets(zb,sizeof(zb),fp);pclose(fp);
                    nz=atoi(zb);}
                  fp=popen("ps aux|grep -c defunct","r");
                  int zc=0; if(fp){char zb[16]={0};fgets(zb,sizeof(zb),fp);pclose(fp);
                    zc=atoi(zb);}
                  fprintf(stderr,"[RES] FDs=%d zombies=%d\n",nz,zc); }
                const char *t=stt_get_text();
                if(t){
                    printf("[MAIN] heard: '%s'\n", t);
                    tft_ui_stt_result(t, NULL);
                    if(stt_fuzzy_match_text(t, "芯片")||stt_fuzzy_match_text(t, "集成电路"))
                        { filter_override=7; do_judge=1; printf("[MAIN] → 芯片模式\n"); tft_ui_stt_result(t,"IC"); }
                    else if(stt_fuzzy_match_text(t, "电阻")||stt_fuzzy_match_text(t, "店主")||stt_fuzzy_match_text(t, "电主"))
                        { filter_override=0; do_judge=1; printf("[MAIN] → 电阻模式\n"); tft_ui_stt_result(t,"Resistor"); }
                    else if(stt_fuzzy_match_text(t, "电容")||stt_fuzzy_match_text(t, "店容")||stt_fuzzy_match_text(t, "电荣"))
                        { filter_override=1; do_judge=1; printf("[MAIN] → 电容模式\n"); tft_ui_stt_result(t,"Capacitor"); }
                    else if(stt_fuzzy_match_text(t, "二极管")||stt_fuzzy_match_text(t, "二级管"))
                        { filter_override=2; do_judge=1; printf("[MAIN] → 二极管模式\n"); tft_ui_stt_result(t,"Diode"); }
                    else if(stt_fuzzy_match_text(t, "LED")||stt_fuzzy_match_text(t, "发光二极管"))
                        { filter_override=3; do_judge=1; printf("[MAIN] → LED模式\n"); tft_ui_stt_result(t,"LED"); }
                    else if(stt_fuzzy_match_text(t, "电位器")||stt_fuzzy_match_text(t, "点位器")||stt_fuzzy_match_text(t, "继电器")||stt_fuzzy_match_text(t, "定位器")||stt_fuzzy_match_text(t, "定位机"))
                        { filter_override=4; do_judge=1; printf("[MAIN] → 电位器模式\n"); tft_ui_stt_result(t,"Pot"); }
                    else if(stt_fuzzy_match_text(t, "连接器")||stt_fuzzy_match_text(t, "链接器"))
                        { filter_override=5; do_judge=1; printf("[MAIN] → 连接器模式\n"); tft_ui_stt_result(t,"Connecter"); }
                    else if(stt_fuzzy_match_text(t, "晶振")||stt_fuzzy_match_text(t, "晶体")||stt_fuzzy_match_text(t, "金正")||stt_fuzzy_match_text(t, "金证")||stt_fuzzy_match_text(t, "精证")||stt_fuzzy_match_text(t, "经正"))
                        { filter_override=6; do_judge=1; printf("[MAIN] → 晶振模式\n"); tft_ui_stt_result(t,"Xtal"); }

                    else if(stt_fuzzy_match_text(t, "全部")||stt_fuzzy_match_text(t, "全部统计"))
                        { filter_override=-1;do_judge=1; printf("[MAIN] → 全部模式\n"); tft_ui_stt_result(t,"All"); }
                    else { printf("[MAIN] no keyword matched\n"); tft_ui_stt_result(t,"No match"); }
                } else {
                    printf("[MAIN] heard nothing\n");
                    tft_ui_stt_result(NULL, NULL);
                }
            }

            /* 执行 JUDGE 播报: UNKNOWN > TEXT > DAMAGED > GENERAL */
            if(do_judge){
                int c[15],f,d,u;cv_branch_get_component_result(c,&f,&d,&u);
                int tf = (filter_override>=0) ? filter_override : f;

                if(tf >= 0 && tf <= 7){ /* 过滤模式 — 最高优先级 */
                    printf("[JUDGE] TEXT mode (filter=%d)\n", tf);
                    voice_text_mode(tf, c);
                } else if(u > 0){ /* 未知模式 */
                    printf("[JUDGE] UNKNOWN mode (count=%d)\n", u);
                    voice_unknown_mode(u);
                } else if(d){ /* 缺损模式 */
                    printf("[JUDGE] DAMAGED mode\n");
                    voice_damaged_mode(c);
                } else { /* 通用模式 */
                    printf("[JUDGE] GENERAL mode\n");
                    voice_general_mode(c);
                }

                /* 播报完成后回到 WAIT 等待模式, 清除语音过滤 */
                filter_override = -1;
                printf("[MAIN] → WAIT\n");
                /* 立即恢复计数页 */
                { int c[15],f,d,u;cv_branch_get_component_result(c,&f,&d,&u);tft_ui_update(c,f,d,u); }
            }
            if(stt_ok)stt_get_text();  /* 清除残留STT文本 */
            /* TFT 5Hz刷新 (每200ms) */
            static int tft_cnt=0;
            if(++tft_cnt%5==0){int c[15],f,d,u;cv_branch_get_component_result(c,&f,&d,&u);tft_ui_update(c,f,d,u);}
        }
        tick++;
        if(tick%5==0){
            pid_t pid = get_mediamtx_pid();
            if(pid > 0 && kill(pid, 0) != 0){
                printf("[MAIN] mediamtx dead (pid=%d), restarting...\n", pid);
                waitpid(pid, NULL, WNOHANG);
                start_mediamtx();
            }
        }
        if(tick%30==0){int64_t in,out,drop;cv_branch_get_stats(&in,&out,&drop);
            printf("[STATS] cv(in=%lld out=%lld drop=%lld)\n",(long long)in,(long long)out,(long long)drop);}

    }

    printf("\n=== Shutting down ===\n");
    stt_deinit();free(g_anno_buf);stop_mediamtx();button_deinit();
    pthread_cond_broadcast(&g_queue.cond);pthread_join(cap_tid,NULL);pthread_join(feed_tid,NULL);
    cv_branch_deinit();tft_display_deinit();gst_pipeline_deinit();capture_deinit();
    printf("=== Done ===\n");return 0;
}
