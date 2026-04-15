#include "alarm_task.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "ALARM_TASK";
static i2s_chan_handle_t spk = NULL;

extern void beep(int freq, int ms, int amp);

void alarm_task(void *pv) {
    while (1) {
        alarm_t *a = alarm_manager_check_expired();
        if (a) {
            ESP_LOGI(TAG, "闹钟: %s", a->message);
            for (int i = 0; i < 5; i++) {
                beep(880, 200,VOLUME);
                vTaskDelay(pdMS_TO_TICKS(200));
                beep(1320, 200,VOLUME);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            alarm_manager_remove(a->id);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void alarm_task_set_speaker(i2s_chan_handle_t spk_handle) {
    spk = spk_handle;
}