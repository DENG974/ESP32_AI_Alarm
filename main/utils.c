#include "utils.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UTILS";
static i2s_chan_handle_t spk = NULL;
static int sample_rate = 16000;

extern void set_latest_recognition(const char *text);

void utils_set_speaker(i2s_chan_handle_t spk_handle) {
    spk = spk_handle;
}

void utils_set_sample_rate(int rate) {
    sample_rate = rate;
}

void beep(int freq, int ms, int amp) {
    if (!spk) {
        ESP_LOGE(TAG, "喇叭未初始化");
        return;
    }
    
    int samples = (sample_rate * ms) / 1000;
    int16_t *buf = malloc(samples * 2 * sizeof(int16_t));
    if (!buf) return;
    
    for (int i = 0; i < samples; i++) {
        int16_t s = (int16_t)(amp * sinf(2 * 3.14159f * freq * i / sample_rate));
        buf[i*2] = s;
        buf[i*2+1] = s;
    }
    
    size_t written;
    i2s_channel_write(spk, buf, samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    free(buf);
}

void play_audio(int16_t *audio, int samples) {
    if (!spk || !audio || samples <= 0) return;
    
    int16_t *stereo = malloc(samples * 2 * sizeof(int16_t));
    if (!stereo) return;
    
    for (int i = 0; i < samples; i++) {
        stereo[i*2] = audio[i];
        stereo[i*2+1] = audio[i];
    }
    
    size_t bytes_written;
    i2s_channel_write(spk, stereo, samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(stereo);
}

void say_hello(void) {
    ESP_LOGI(TAG, "你好！我是AI闹钟，有什么可以帮您？");
    beep(880, 100, 6000);
    vTaskDelay(pdMS_TO_TICKS(100));
    beep(660, 100, 6000);
}

void say_sorry(void) {
    ESP_LOGI(TAG, "抱歉，我没听清楚，请再说一遍");
    beep(440, 200, 6000);
}