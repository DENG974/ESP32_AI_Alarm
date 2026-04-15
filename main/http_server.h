#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"

// 启动 HTTP 服务器
esp_err_t start_http_server(void);

void set_latest_recognition(const char *text);

#endif