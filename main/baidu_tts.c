#include "baidu_tts.h"
#include "baidu_asr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

static const char *TAG = "BAIDU_TTS";
static char g_tts_token[256] = {0};
static i2s_chan_handle_t s_spk = NULL;
static bool s_is_speaking = false;

// 播放PCM音频
void baidu_tts_play_pcm(int16_t *pcm, int samples) {
    if (!s_spk || !pcm || samples <= 0) return;
    
    int16_t *stereo = malloc(samples * 2 * sizeof(int16_t));
    if (!stereo) return;
    
    for (int i = 0; i < samples; i++) {
        stereo[i*2] = pcm[i];
        stereo[i*2+1] = pcm[i];
    }
    
    size_t written;
    i2s_channel_write(s_spk, stereo, samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    free(stereo);
}

// 生成简单的提示音（叮咚）
static void play_beep(void) {
    int samples = 8000;  // 0.5秒 @16kHz
    int16_t *pcm = malloc(samples * sizeof(int16_t));
    if (!pcm) return;
    
    for (int i = 0; i < samples; i++) {
        float t = i / 16000.0;
        float freq = (i < samples/2) ? 880 : 660;
        pcm[i] = (int16_t)(6000 * sinf(2 * 3.14159f * freq * t));
    }
    baidu_tts_play_pcm(pcm, samples);
    free(pcm);
}

// URL编码
static char* url_encode(const char *str) {
    if (!str) return NULL;
    
    int len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;
    
    int pos = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = str[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            encoded[pos++] = c;
        } else if (c == ' ') {
            encoded[pos++] = '+';
        } else {
            pos += sprintf(encoded + pos, "%%%02X", c);
        }
    }
    encoded[pos] = '\0';
    return encoded;
}

// 获取Access Token
static esp_err_t get_token(void) {
    if (strlen(g_tts_token) > 0) return ESP_OK;
    
    char url[256];
    snprintf(url, sizeof(url),
             "http://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    
    esp_err_t err = ESP_FAIL;
    esp_err_t http_err = esp_http_client_perform(client);
    
    if (http_err == ESP_OK) {
        char response[1024] = {0};
        int len = esp_http_client_read_response(client, response, 1023);
        if (len > 0) {
            cJSON *root = cJSON_Parse(response);
            if (root) {
                cJSON *token = cJSON_GetObjectItem(root, "access_token");
                if (token && token->valuestring) {
                    strncpy(g_tts_token, token->valuestring, sizeof(g_tts_token) - 1);
                    err = ESP_OK;
                    ESP_LOGI(TAG, "Token获取成功");
                }
                cJSON_Delete(root);
            }
        }
    }
    
    esp_http_client_cleanup(client);
    return err;
}

// 下载MP3数据
static uint8_t* download_mp3(const char *url, int *out_len) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    
    esp_err_t http_err = esp_http_client_perform(client);
    uint8_t *mp3_data = NULL;
    *out_len = 0;
    
    if (http_err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            int len = esp_http_client_get_content_length(client);
            if (len > 0 && len < 50000) {
                mp3_data = malloc(len);
                if (mp3_data) {
                    int total = 0;
                    while (total < len) {
                        int read = esp_http_client_read(client, (char*)(mp3_data + total), len - total);
                        if (read <= 0) break;
                        total += read;
                    }
                    *out_len = total;
                    if (total != len) {
                        free(mp3_data);
                        mp3_data = NULL;
                        *out_len = 0;
                    }
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    return mp3_data;
}

// 简单的MP3帧查找（找帧头0xFF 0xFB）
static uint8_t* find_mp3_frame(uint8_t *data, int len) {
    for (int i = 0; i < len - 4; i++) {
        if (data[i] == 0xFF && (data[i+1] & 0xE0) == 0xE0) {
            return data + i;
        }
    }
    return NULL;
}

// 模拟播放MP3（实际播放需要解码器，这里用提示音代替）
static void play_mp3_simulate(uint8_t *mp3, int len) {
    ESP_LOGI(TAG, "播放MP3: %d字节", len);
    
    // 查找MP3帧头
    uint8_t *frame = find_mp3_frame(mp3, len);
    if (frame) {
        ESP_LOGI(TAG, "找到MP3帧头");
    }
    
    // 播放提示音代替语音
    play_beep();
}

// 文字转语音并播放
esp_err_t baidu_tts_speak(const char *text) {
    if (!text || !s_spk) {
        ESP_LOGE(TAG, "参数无效或喇叭未初始化");
        return ESP_FAIL;
    }
    
    while (s_is_speaking) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_is_speaking = true;
    
    ESP_LOGI(TAG, "TTS: %s", text);
    
    if (get_token() != ESP_OK) {
        s_is_speaking = false;
        return ESP_FAIL;
    }
    
    char *encoded = url_encode(text);
    if (!encoded) {
        s_is_speaking = false;
        return ESP_FAIL;
    }
    
    // 使用 aue=6 返回 MP3 格式
    char url[512];
    snprintf(url, sizeof(url),
             "http://tsn.baidu.com/text2audio?tex=%s&lan=zh&cuid=esp32&ctp=1&tok=%s&spd=5&pit=5&vol=9&per=4&aue=6",
             encoded, g_tts_token);
    
    free(encoded);
    
    int mp3_len = 0;
    uint8_t *mp3_data = download_mp3(url, &mp3_len);
    
    if (mp3_data && mp3_len > 0) {
        play_mp3_simulate(mp3_data, mp3_len);
        free(mp3_data);
    } else {
        ESP_LOGE(TAG, "下载MP3失败");
        play_beep();  // 失败时播放提示音
    }
    
    s_is_speaking = false;
    return ESP_OK;
}

void baidu_tts_set_speaker(i2s_chan_handle_t spk) {
    s_spk = spk;
}

esp_err_t baidu_tts_init(void) {
    memset(g_tts_token, 0, sizeof(g_tts_token));
    return get_token();
}

bool baidu_tts_is_playing(void) {
    return s_is_speaking;
}