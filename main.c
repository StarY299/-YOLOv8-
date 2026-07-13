/**
 * main.c — RV1126B 电子元器件识别系统
 * 按键控制模式 + STT语音指令
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

#define CAP_WIDTH        1920
#define CAP_HEIGHT       1080
#define CAP_FPS          30
#define H264_BITRATE     4000000
#define QUEUE_SIZE       4
#define MODEL_PATH       "/userdata/best3-i8.rknn"

static volatile int running = 1;
static int filter_override = -1; /* -1=AI决定, 0=R,1=C,2=D */

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
static uint8_t *g_anno_buf=NULL;static size_t g_anno_len=0;
static pthread_mutex_t g_anno_lock=PTHREAD_MUTEX_INITIALIZER;
static void *ai_feed_thread(void *arg){(void)arg;
    while(running){queued_frame_t f;if(queue_get(&g_queue,&f)!=0)break;
        if(f.size<256){free(f.data);continue;}
        size_t ns=0;const uint8_t *nd=cv_branch_get_annotated_frame(&ns);
        if(nd&&ns>256){pthread_mutex_lock(&g_anno_lock);free(g_anno_buf);
            g_anno_buf=(uint8_t*)malloc(ns);if(g_anno_buf){memcpy(g_anno_buf,nd,ns);g_anno_len=ns;}else g_anno_len=0;
            pthread_mutex_unlock(&g_anno_lock);}
        int pushed=0;pthread_mutex_lock(&g_anno_lock);
        if(g_anno_buf&&g_anno_len>256){gst_pipeline_push_frame(g_anno_buf,g_anno_len,f.frame_id);pushed=1;}
        pthread_mutex_unlock(&g_anno_lock);if(!pushed)gst_pipeline_push_frame(f.data,f.size,f.frame_id);
        cv_frame_t cvf;cvf.data=f.data;cvf.size=f.size;cvf.width=CAP_WIDTH;cvf.height=CAP_HEIGHT;cvf.stride=0;
        cvf.format=CV_FMT_JPEG;cvf.timestamp_us=0;cvf.frame_id=f.frame_id;cv_branch_push_frame(&cvf);free(f.data);}
    return NULL;
}
static void on_ai_result(const cv_result_t *result, void *user_data){(void)user_data;
    if(!result||result->count==0)return;
    printf("[AI] frame %lld: %d detections\n",(long long)result->frame_id,result->count);
    for(int i=0;i<result->count&&i<8;i++){const cv_detection_t *d=&result->detections[i];
        printf("  [%d] %s: %.2f\n",d->class_id,d->label,d->confidence);}
}
static void sig_handler(int sig){(void)sig;running=0;}

int main(void){
    printf("=== RV1126B Component Recognition System ===\n");
    signal(SIGTERM,sig_handler);signal(SIGINT,sig_handler);

    /* 1. STT 加载 (麦关) + 按键 */
    int stt_ok=(stt_init()==0);
    if(stt_ok){voice_play("stt_ready");printf("[MAIN] STT ready\n");}
    button_init(0);

    /* 2. 摄像头 */
    char cap_dev[128];if(capture_find_device(cap_dev,sizeof(cap_dev))!=0){fprintf(stderr,"FATAL: no camera\n");return -1;}
    if(capture_init(cap_dev,CAP_WIDTH,CAP_HEIGHT,CAP_FPS)!=0)return -1;
    if(gst_pipeline_init(CAP_WIDTH,CAP_HEIGHT,CAP_FPS,H264_BITRATE)!=0){capture_deinit();return -1;}

    /* 3. AI + TFT */
    cv_branch_config_t cv_cfg={.max_queue_size=2,.on_result=on_ai_result,.model_path=MODEL_PATH};
    cv_branch_init(&cv_cfg);
    if(tft_display_init()!=0)fprintf(stderr,"WARN: TFT\n");else tft_ui_init();

    /* 4. 工作线程 */
    queue_init(&g_queue);pthread_t cap_tid,feed_tid;
    pthread_create(&cap_tid,NULL,capture_thread,NULL);
    pthread_create(&feed_tid,NULL,ai_feed_thread,NULL);
    usleep(500000);if(start_mediamtx()!=0)fprintf(stderr,"WARN: mediamtx\n");

    printf("\n=== System Ready ===\n");voice_ready();
    printf("Short press=announce  Long press=voice command\n");

    int64_t tick=0;
    while(running){sleep(1);tick++;

        int btn=button_read();
        if(btn==BTN_SHORT){ /* 短按: 直接播报 */
            int c[12],f,d;cv_branch_get_component_result(c,&f,&d,NULL);
            if(filter_override>=0)f=filter_override;
            voice_announce(c,f,d);
        }
        if(btn==BTN_LONG&&stt_ok){ /* 长按: 开麦听指令 */
            printf("[MAIN] listening...\n");
            stt_start_listening();sleep(5);stt_pause_listening();
            const char *t=stt_get_text();
            if(t){printf("[MAIN] heard: %s\n",t);
                if(strstr(t,"电阻"))filter_override=0;
                else if(strstr(t,"电容"))filter_override=1;
                else if(strstr(t,"二极管"))filter_override=2;
                else if(strstr(t,"全部")||strstr(t,"所有"))filter_override=-1;
            }
            int c[12],f,d;cv_branch_get_component_result(c,&f,&d,NULL);
            if(filter_override>=0)f=filter_override;
            voice_announce(c,f,d);
        }
        /* 清残留文本 */
        if(stt_ok)stt_get_text();

        /* TFT */
        if(tick%2==0){int c[12],f,d,u;cv_branch_get_component_result(c,&f,&d,&u);tft_ui_update(c,f,d,u);}
        /* 30s统计 */
        if(tick%30==0){int64_t in,out,drop;cv_branch_get_stats(&in,&out,&drop);
            printf("[STATS] cv(in=%lld out=%lld drop=%lld)\n",(long long)in,(long long)out,(long long)drop);}
    }

    printf("\n=== Shutting down ===\n");
    stt_deinit();stop_mediamtx();button_deinit();
    pthread_cond_broadcast(&g_queue.cond);pthread_join(cap_tid,NULL);pthread_join(feed_tid,NULL);
    cv_branch_deinit();tft_display_deinit();gst_pipeline_deinit();capture_deinit();
    free(g_anno_buf);printf("=== Done ===\n");return 0;
}
