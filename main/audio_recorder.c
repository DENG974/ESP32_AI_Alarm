#include "audio_recorder.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RECORDER";
static i2s_chan_handle_t *s_rx_handle = NULL;
static bool s_is_recording = false;
static int s_sample_count = 0;

#define SAMPLE_RATE 16000
#define DMA_BUFFER_SIZE 512
#define DMA_BUFFER_BYTES (DMA_BUFFER_SIZE * sizeof(int16_t))

esp_err_t recorder_init(i2s_chan_handle_t *rx_handle) {
    s_rx_handle = rx_handle;
    ESP_LOGI(TAG, "录音模块初始化完成");
    return ESP_OK;
}

// 音频预处理：降噪和增益
static void preprocess_audio(int16_t *audio, int samples) {
    if (!audio || samples < 100) return;
    
    // 去除直流偏移
    int32_t sum = 0;
    for (int i = 0; i < samples; i++) sum += audio[i];
    int16_t dc_offset = sum / samples;
    for (int i = 0; i < samples; i++) audio[i] -= dc_offset;
    
    // 动态增益（适中）
    int16_t max_val = 0;
    for (int i = 0; i < samples; i++) {
        int16_t val = audio[i] < 0 ? -audio[i] : audio[i];
        if (val > max_val) max_val = val;
    }
    
    float gain = 1.5f;
    if (max_val < 3000) gain = 2.5f;
    else if (max_val < 6000) gain = 1.8f;
    
    for (int i = 0; i < samples; i++) {
        int32_t val = (int32_t)(audio[i] * gain);
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        audio[i] = (int16_t)val;
    }
}

// 录音到内存
esp_err_t recorder_start_to_memory(int16_t **audio_out, int *sample_count, float max_seconds) {
    if (!s_rx_handle || !*s_rx_handle) {
        ESP_LOGE(TAG, "I2S未初始化");
        return ESP_FAIL;
    }
    
    if (!audio_out || !sample_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int max_samples = SAMPLE_RATE * max_seconds;
    ESP_LOGI(TAG, "开始录音: %.1f秒, 最大采样数: %d", max_seconds, max_samples);
    
    // 分配内存
    int16_t *buffer = malloc(max_samples * sizeof(int16_t));
    if (!buffer) {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_FAIL;
    }
    
    s_is_recording = true;
    int samples_recorded = 0;
    int16_t chunk[DMA_BUFFER_SIZE];
    int timeout_count = 0;
    
    // 清空I2S缓冲区
    size_t dummy_bytes;
    for (int i = 0; i < 5; i++) {
        i2s_channel_read(*s_rx_handle, chunk, sizeof(chunk), &dummy_bytes, pdMS_TO_TICKS(10));
    }
    
    while (s_is_recording && samples_recorded < max_samples) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(*s_rx_handle, chunk, 
                                         DMA_BUFFER_BYTES, 
                                         &bytes_read, pdMS_TO_TICKS(200));
        
        if (err == ESP_OK && bytes_read > 0) {
            int read_samples = bytes_read / sizeof(int16_t);
            if (samples_recorded + read_samples <= max_samples) {
                memcpy(buffer + samples_recorded, chunk, bytes_read);
                samples_recorded += read_samples;
                timeout_count = 0;
            } else {
                int remaining = max_samples - samples_recorded;
                memcpy(buffer + samples_recorded, chunk, remaining * sizeof(int16_t));
                samples_recorded = max_samples;
                break;
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            timeout_count++;
            if (timeout_count > 30) {
                ESP_LOGW(TAG, "录音超时，已采集: %d 采样", samples_recorded);
                break;
            }
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S读取错误: %d", err);
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    s_is_recording = false;
    
    if (samples_recorded == 0) {
        ESP_LOGE(TAG, "录音失败，未采集到数据");
        free(buffer);
        return ESP_FAIL;
    }
    
    *audio_out = buffer;
    *sample_count = samples_recorded;
    
    ESP_LOGI(TAG, "录音完成: %d 采样 (%.2f秒)", samples_recorded, (float)samples_recorded / SAMPLE_RATE);
    // 音频预处理
    preprocess_audio(buffer, samples_recorded);
    return ESP_OK;
}

void recorder_stop(void) {
    s_is_recording = false;
}

bool recorder_is_recording(void) {
    return s_is_recording;
}

int recorder_get_samples(void) {
    return s_sample_count;
}