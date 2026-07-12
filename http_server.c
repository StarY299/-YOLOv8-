/**
 * http_server.c — 内嵌 MJPEG 推流服务器实现
 *
 * 浏览器访问 http://<board-ip>:8080 即可实时查看检测画面.
 * 仅使用 POSIX socket API, 无任何外部依赖.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include "http_server.h"
#include "ai_processor.h"

#define BACKLOG     4
#define RECV_BUF    2048
#define MJPEG_FPS   15
#define MJPEG_US    (1000000 / MJPEG_FPS)

static volatile int g_running = 0;
static pthread_t      g_thread;
static int            g_port = 8080;
static int            g_listen_fd = -1;

/* ---- 简单的 HTTP 响应构建 ---- */

/* 首页 HTML: 自动刷新显示 MJPEG 流 */
static const char *g_index_html =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<title>Component AI</title>\n"
    "<meta charset='utf-8'>\n"
    "<style>body{margin:0;background:#000;display:flex;"
    "justify-content:center;align-items:center;min-height:100vh;}"
    "img{max-width:100%;max-height:100vh;}</style>\n"
    "</head><body>\n"
    "<img src='/stream'>\n"
    "</body></html>\n";

/* ---- MJPEG 流 ---- */
static void send_mjpeg(int client_fd)
{
    char hdr[256];
    char boundary[] = "--ELFRV1126B\r\n";

    /* HTTP 响应头 */
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=ELFRV1126B\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", boundary);
    send(client_fd, hdr, strlen(hdr), MSG_NOSIGNAL);

    while (g_running) {
        size_t jpeg_size = 0;
        const uint8_t *jpeg = cv_branch_get_annotated_frame(&jpeg_size);

        if (jpeg && jpeg_size > 256) {
            /* multipart 帧头 */
            snprintf(hdr, sizeof(hdr),
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %zu\r\n"
                "\r\n", jpeg_size);
            send(client_fd, hdr, strlen(hdr), MSG_NOSIGNAL);
            send(client_fd, jpeg, jpeg_size, MSG_NOSIGNAL);
            send(client_fd, "\r\n", 2, MSG_NOSIGNAL);
            send(client_fd, boundary, strlen(boundary), MSG_NOSIGNAL);
        }

        usleep(MJPEG_US);
    }
}

/* ---- 单帧快照 ---- */
static void send_snapshot(int client_fd)
{
    size_t jpeg_size = 0;
    const uint8_t *jpeg = NULL;

    /* 等最多 500ms 拿一帧 */
    for (int retry = 0; retry < 10; retry++) {
        jpeg = cv_branch_get_annotated_frame(&jpeg_size);
        if (jpeg && jpeg_size > 256) break;
        usleep(50000);
    }

    if (jpeg && jpeg_size > 256) {
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n", jpeg_size);
        send(client_fd, hdr, strlen(hdr), MSG_NOSIGNAL);
        send(client_fd, jpeg, jpeg_size, MSG_NOSIGNAL);
    } else {
        const char *err = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
        send(client_fd, err, strlen(err), MSG_NOSIGNAL);
    }
}

/* ---- 请求路由 ---- */
static void handle_client(int client_fd)
{
    char buf[RECV_BUF];
    int n = (int)recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(client_fd); return; }
    buf[n] = '\0';

    /* 解析第一行: GET /path HTTP/1.1 */
    char method[8] = {0}, path[64] = {0};
    sscanf(buf, "%7s %63s", method, path);

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        send(client_fd, g_index_html, strlen(g_index_html), MSG_NOSIGNAL);
    } else if (strcmp(path, "/stream") == 0) {
        send_mjpeg(client_fd);
    } else if (strcmp(path, "/snapshot") == 0) {
        send_snapshot(client_fd);
    } else {
        const char *nf = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(client_fd, nf, strlen(nf), MSG_NOSIGNAL);
    }

    close(client_fd);
}

/* ---- 服务器主循环 ---- */
static void *server_thread(void *arg)
{
    (void)arg;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int opt = 1;

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("[HTTP] socket");
        return NULL;
    }

    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)g_port);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[HTTP] bind");
        close(g_listen_fd);
        return NULL;
    }

    if (listen(g_listen_fd, BACKLOG) < 0) {
        perror("[HTTP] listen");
        close(g_listen_fd);
        return NULL;
    }

    printf("[HTTP] server started on port %d\n", g_port);

    while (g_running) {
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* 设置超时避免客户端不关闭连接导致线程阻塞 */
        struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        /* 禁用 Nagle, MJPEG 需要低延迟 */
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        handle_client(client_fd);
        /* client_fd 在 handle_client 内 close */
    }

    close(g_listen_fd);
    g_listen_fd = -1;
    printf("[HTTP] server stopped\n");
    return NULL;
}

/* ---- 公共 API ---- */

int http_server_start(int port)
{
    if (g_running) return -1;

    g_port = port;
    g_running = 1;

    if (pthread_create(&g_thread, NULL, server_thread, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    return 0;
}

void http_server_stop(void)
{
    if (!g_running) return;

    g_running = 0;

    /* 触发 accept 返回 */
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    pthread_join(g_thread, NULL);
}
