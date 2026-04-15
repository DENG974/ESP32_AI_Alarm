#include "voice_task.h"
#include "audio_recorder.h"
#include "baidu_asr.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "command_parser.h"
#include "config.h"


static const char *TAG = "VOICE_TASK";
static i2s_chan_handle_t mic = NULL;
static i2s_chan_handle_t spk = NULL;

extern void beep(int freq, int ms, int amp);
extern void play_audio(int16_t *audio, int samples);

static uint32_t volume(const int16_t *buf, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += (buf[i] < 0 ? -buf[i] : buf[i]);
    }
    return sum / len;
}

void voice_task(void *pv) {
    recorder_init(&mic);
    int16_t chunk[128];
    int trig = 0;
    bool is_recording = false;
    
    while (1) {
        if (is_recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        size_t bytes;
        esp_err_t err = i2s_channel_read(mic, chunk, sizeof(chunk), &bytes, pdMS_TO_TICKS(100));
        
        if (err != ESP_OK || bytes == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        uint32_t vol = volume(chunk, bytes / 2);
        
        if (vol > 1500) {
            if (++trig > 2) {
                is_recording = true;
                beep(880, 100,VOLUME);
                
                int16_t *audio = NULL;
                int cnt = 0;
                esp_err_t ret = recorder_start_to_memory(&audio, &cnt,RECORD_SEC);
                
                if (ret == ESP_OK && audio && cnt > 0) {
                    beep(440, 80,VOLUME);
                    char *result = NULL;
                    esp_err_t asr_ret = baidu_asr_recognize(audio, cnt, &result);
                    
                    if (asr_ret == ESP_OK && result) {
                        beep(880, 80,VOLUME);
                        beep(1320, 80,VOLUME);
                        parse(result);
                        free(result);
                    } else {
                        beep(220, 150,VOLUME);
                    }
                    free(audio);
                } else {
                    beep(220, 200,VOLUME);
                }
                
                trig = 0;
                is_recording = false;
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        } else {
            trig = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void voice_task_set_speaker(i2s_chan_handle_t spk_handle) {
    spk = spk_handle;
}

void voice_task_set_mic(i2s_chan_handle_t mic_handle) {
    mic = mic_handle;
}