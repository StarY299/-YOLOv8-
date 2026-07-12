/**
 * http_server.h — 内嵌 MJPEG 推流服务器
 *
 * 浏览器打开 http://<ip>:8080 即可查看实时带检测框画面.
 * 仅依赖 POSIX socket, 无第三方库依赖.
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动 HTTP 服务器 (后台线程)
 * @param port  监听端口, 默认 8080
 * @return 0 成功, -1 失败
 */
int http_server_start(int port);

/**
 * 停止 HTTP 服务器
 */
void http_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif
