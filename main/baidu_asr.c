#include "baidu_asr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "config.h"

static const char *TAG = "BAIDU_ASR";

static char g_access_token[256] = {0};
static char g_last_error[256] = {0};
static char g_json_buffer[65536];  // 全局静态缓冲区，避免栈溢出

#define SAMPLE_RATE 16000

// Base64编码函数
static char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    static const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded_data[j++] = encoding_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0) & 0x3F];
    }
    
    for (size_t i = 0; i < (3 - input_length % 3) % 3; i++) {
        encoded_data[*output_length - 1 - i] = '=';
    }
    
    encoded_data[*output_length] = 0;
    return encoded_data;
}

// HTTP客户端事件处理
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char *response_buffer = NULL;
    static int response_len = 0;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!response_buffer) {
                response_buffer = malloc(evt->data_len + 1);
                if (response_buffer) {
                    memcpy(response_buffer, evt->data, evt->data_len);
                    response_len = evt->data_len;
                    response_buffer[response_len] = 0;
                }
            } else {
                char *new_buffer = realloc(response_buffer, response_len + evt->data_len + 1);
                if (new_buffer) {
                    memcpy(new_buffer + response_len, evt->data, evt->data_len);
                    response_len += evt->data_len;
                    response_buffer = new_buffer;
                    response_buffer[response_len] = 0;
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (response_buffer) {
                esp_http_client_set_user_data(evt->client, response_buffer);
            }
            response_buffer = NULL;
            response_len = 0;
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

// 获取百度访问令牌
esp_err_t baidu_asr_get_token(void) {
    char url[256];
    snprintf(url, sizeof(url),
             "http://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        snprintf(g_last_error, sizeof(g_last_error), "HTTP客户端初始化失败");
        return ESP_FAIL;
    }
    
    esp_err_t err = ESP_FAIL;
    esp_err_t http_err = esp_http_client_perform(client);
    
    if (http_err == ESP_OK) {
        char *response = NULL;
        esp_http_client_get_user_data(client, (void**)&response);
        
        if (response) {
            cJSON *root = cJSON_Parse(response);
            if (root) {
                cJSON *token = cJSON_GetObjectItem(root, "access_token");
                if (token && token->valuestring) {
                    strncpy(g_access_token, token->valuestring, sizeof(g_access_token) - 1);
                    g_access_token[sizeof(g_access_token) - 1] = '\0';
                    ESP_LOGI(TAG, "Token获取成功");
                    err = ESP_OK;
                } else {
                    cJSON *error = cJSON_GetObjectItem(root, "error_description");
                    snprintf(g_last_error, sizeof(g_last_error), "API错误: %s", 
                             error ? error->valuestring : "未知");
                }
                cJSON_Delete(root);
            }
            free(response);
        }
    } else {
        snprintf(g_last_error, sizeof(g_last_error), "HTTP错误: %s", esp_err_to_name(http_err));
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(http_err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

// 语音识别
esp_err_t baidu_asr_recognize(int16_t *audio_data, int audio_len, char **result) {
    if (!audio_data || audio_len <= 0 || !result) {
        snprintf(g_last_error, sizeof(g_last_error), "参数无效");
        return ESP_ERR_INVALID_ARG;
    }
    
    *result = NULL;
    
    int max_audio_len = SAMPLE_RATE * 2;
    if (audio_len > max_audio_len) {
        audio_len = max_audio_len;
    }
    
    if (strlen(g_access_token) == 0) {
        ESP_LOGI(TAG, "获取Token...");
        esp_err_t err = baidu_asr_get_token();
        if (err != ESP_OK) {
            return err;
        }
    }
    
    size_t base64_len;
    size_t data_bytes = audio_len * sizeof(int16_t);
    ESP_LOGI(TAG, "编码音频: %d 字节", data_bytes);
    
    char *base64_data = base64_encode((unsigned char*)audio_data, data_bytes, &base64_len);
    if (!base64_data) {
        snprintf(g_last_error, sizeof(g_last_error), "Base64编码失败");
        return ESP_FAIL;
    }
    
    // 使用全局静态缓冲区，避免栈溢出
    if (512 + base64_len >= sizeof(g_json_buffer)) {
        ESP_LOGE(TAG, "JSON太大: %d", 512 + base64_len);
        free(base64_data);
        return ESP_FAIL;
    }
    
    snprintf(g_json_buffer, sizeof(g_json_buffer),
             "{\"format\":\"pcm\",\"rate\":%d,\"channel\":1,\"token\":\"%s\",\"speech\":\"%s\",\"len\":%d,\"cuid\":\"esp32\"}",
             SAMPLE_RATE, g_access_token, base64_data, data_bytes);
    
    free(base64_data);
    
    ESP_LOGI(TAG, "发送ASR请求...");
    
    esp_http_client_config_t config = {
        .url = "http://vop.baidu.com/server_api",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .buffer_size = 4096,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        snprintf(g_last_error, sizeof(g_last_error), "HTTP客户端初始化失败");
        return ESP_FAIL;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, g_json_buffer, strlen(g_json_buffer));
    esp_http_client_set_user_data(client, NULL);
    
    esp_err_t http_err = esp_http_client_perform(client);
    esp_err_t err = ESP_FAIL;
    
    if (http_err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "ASR HTTP状态: %d", status_code);
        
        char *response = NULL;
        esp_http_client_get_user_data(client, (void**)&response);
        
        if (response && status_code == 200) {
            cJSON *resp_root = cJSON_Parse(response);
            if (resp_root) {
                cJSON *err_no = cJSON_GetObjectItem(resp_root, "err_no");
                if (err_no && err_no->valueint == 0) {
                    cJSON *result_arr = cJSON_GetObjectItem(resp_root, "result");
                    if (result_arr && cJSON_GetArraySize(result_arr) > 0) {
                        cJSON *first = cJSON_GetArrayItem(result_arr, 0);
                        if (first && first->valuestring) {
                            *result = strdup(first->valuestring);
                            if (*result) {
                                err = ESP_OK;
                                ESP_LOGI(TAG, "识别结果: %s", *result);
                            }
                        }
                    } else {
                        snprintf(g_last_error, sizeof(g_last_error), "响应中无识别结果");
                    }
                } else {
                    cJSON *err_msg = cJSON_GetObjectItem(resp_root, "err_msg");
                    snprintf(g_last_error, sizeof(g_last_error), "ASR错误: %s",
                             err_msg ? err_msg->valuestring : "未知");
                    ESP_LOGE(TAG, "%s", g_last_error);
                }
                cJSON_Delete(resp_root);
            }
            free(response);
        } else {
            snprintf(g_last_error, sizeof(g_last_error), "HTTP响应: %d", status_code);
        }
    } else {
        snprintf(g_last_error, sizeof(g_last_error), "HTTP错误: %s", esp_err_to_name(http_err));
        ESP_LOGE(TAG, "HTTP失败: %s", esp_err_to_name(http_err));
    }
    
    esp_http_client_cleanup(client);
    
    return err;
}

const char* baidu_asr_last_error(void) {
    return g_last_error;
}

esp_err_t baidu_asr_init(void) {
    memset(g_access_token, 0, sizeof(g_access_token));
    memset(g_last_error, 0, sizeof(g_last_error));
    
    ESP_LOGI(TAG, "初始化百度ASR...");
    
    esp_err_t err = baidu_asr_get_token();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ASR初始化成功");
    } else {
        ESP_LOGE(TAG, "ASR初始化失败: %s", g_last_error);
    }
    return err;
}