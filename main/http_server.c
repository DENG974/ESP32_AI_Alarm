#include "http_server.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <time.h>
#include <string.h>

static const char *TAG = "HTTP_SERVER";
static httpd_handle_t server = NULL;

// 存储最新识别结果
static char latest_recognition[256] = "等待语音指令...";

// 更新识别结果
void set_latest_recognition(const char *text) {
    if (text) {
        strncpy(latest_recognition, text, sizeof(latest_recognition) - 1);
        latest_recognition[sizeof(latest_recognition) - 1] = '\0';
        ESP_LOGI(TAG, "识别结果已更新: %s", latest_recognition);
    }
}

// 获取当前时间 API
static esp_err_t time_get_handler(httpd_req_t *req) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char response[64];
    snprintf(response, sizeof(response), 
             "{\"hour\":%d,\"minute\":%d,\"second\":%d}",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// 获取识别结果 API
static esp_err_t recognition_get_handler(httpd_req_t *req) {
    char response[512];
    snprintf(response, sizeof(response), "{\"text\":\"%s\"}", latest_recognition);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// 获取闹钟列表 API
static esp_err_t alarms_get_handler(httpd_req_t *req) {
    alarm_t list[MAX_ALARMS];
    int count = alarm_manager_get_all(list, MAX_ALARMS);
    
    char response[2048] = "[";
    int offset = 1;
    
    for (int i = 0; i < count && offset < 1800; i++) {
        struct tm *t = localtime(&list[i].trigger_time);
        offset += snprintf(response + offset, sizeof(response) - offset,
                           "%s{\"id\":%d,\"hour\":%d,\"minute\":%d,\"msg\":\"%s\"}",
                           (i == 0 ? "" : ","),
                           list[i].id, t->tm_hour, t->tm_min, list[i].message);
    }
    strcat(response, "]");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// 设置闹钟 API
static esp_err_t alarm_post_handler(httpd_req_t *req) {
    char content[128] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    int hour = 0, minute = 0;
    char msg[64] = "闹钟";
    
    char *h = strstr(content, "\"hour\":");
    if (h) hour = atoi(h + 7);
    char *m = strstr(content, "\"minute\":");
    if (m) minute = atoi(m + 8);
    char *msg_start = strstr(content, "\"msg\":\"");
    if (msg_start) {
        msg_start += 7;
        char *msg_end = strchr(msg_start, '"');
        if (msg_end) {
            int len = msg_end - msg_start;
            if (len > 0 && len < 63) {
                memcpy(msg, msg_start, len);
                msg[len] = '\0';
            }
        }
    }
    
    if (hour > 0 && hour <= 24) {
        int alarm_id = alarm_manager_add_absolute(hour, minute, msg);
        char response[64];
        snprintf(response, sizeof(response), "{\"success\":true,\"id\":%d}", alarm_id);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time");
    }
    return ESP_OK;
}

// 删除闹钟 API
static esp_err_t alarm_delete_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    const char *last_slash = strrchr(uri, '/');
    if (!last_slash) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ID");
        return ESP_FAIL;
    }
    
    int id = atoi(last_slash + 1);
    bool success = alarm_manager_remove(id);
    char response[64];
    snprintf(response, sizeof(response), "{\"success\":%s}", success ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// 启动 HTTP 服务器
esp_err_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "启动 HTTP 服务器失败");
        return ESP_FAIL;
    }
    
    // 注册接口
    httpd_uri_t time_uri = {
        .uri = "/api/time",
        .method = HTTP_GET,
        .handler = time_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &time_uri);
    
    httpd_uri_t recognition_uri = {
        .uri = "/api/recognition",
        .method = HTTP_GET,
        .handler = recognition_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &recognition_uri);
    
    httpd_uri_t alarms_uri = {
        .uri = "/api/alarms",
        .method = HTTP_GET,
        .handler = alarms_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &alarms_uri);
    
    httpd_uri_t alarm_post_uri = {
        .uri = "/api/alarm",
        .method = HTTP_POST,
        .handler = alarm_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &alarm_post_uri);
    
    httpd_uri_t alarm_delete_uri = {
        .uri = "/api/alarm/*",
        .method = HTTP_DELETE,
        .handler = alarm_delete_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &alarm_delete_uri);
    
    httpd_uri_t recognition_uri2 = {
        .uri = "/api/api/recognition",
        .method = HTTP_GET,
        .handler = recognition_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &recognition_uri2);

    ESP_LOGI(TAG, "HTTP 服务器已启动");
    return ESP_OK;
}