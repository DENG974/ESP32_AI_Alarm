#include "alarm_manager.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "ALARM_MGR";

static alarm_t alarms[MAX_ALARMS];
static int alarm_count = 0;
static int next_id = 1;
static SemaphoreHandle_t s_alarm_mutex = NULL;

static void alarm_lock(void) {
    if (s_alarm_mutex) {
        xSemaphoreTake(s_alarm_mutex, portMAX_DELAY);
    }
}

static void alarm_unlock(void) {
    if (s_alarm_mutex) {
        xSemaphoreGive(s_alarm_mutex);
    }
}

// 初始化闹钟管理器
void alarm_manager_init(void) {
    memset(alarms, 0, sizeof(alarms));
    alarm_count = 0;
    next_id = 1;
    if (!s_alarm_mutex) {
        s_alarm_mutex = xSemaphoreCreateMutex();
    }
    
    // 初始化系统时间（如果需要）
    // 可以在这里设置NTP时间同步
    
    ESP_LOGI(TAG, "Alarm manager initialized");
}

// 添加闹钟（相对时间）
int alarm_manager_add_relative(int value, time_unit_t unit, const char *message) {
    alarm_lock();
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Max alarms reached");
        alarm_unlock();
        return -1;
    }
    
    // 计算触发时间
    time_t now = time(NULL);
    if (now == 0) {
        // 如果时间未设置，使用启动后的秒数
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
    }
    
    int seconds = 0;
    switch (unit) {
        case TIME_UNIT_SECONDS:
            seconds = value;
            break;
        case TIME_UNIT_MINUTES:
            seconds = value * 60;
            break;
        case TIME_UNIT_HOURS:
            seconds = value * 3600;
            break;
        default:
            seconds = value * 60;
    }
    
    alarm_t *new_alarm = &alarms[alarm_count];
    new_alarm->id = next_id++;
    new_alarm->trigger_time = now + seconds;
    new_alarm->is_active = true;
    strncpy(new_alarm->message, message, MAX_MESSAGE_LEN - 1);
    
    alarm_count++;
    
    // 格式化时间显示
    struct tm *timeinfo = localtime(&new_alarm->trigger_time);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    
    ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%s, Message='%s'", 
             new_alarm->id, time_str, message);
    
    alarm_unlock();
    return new_alarm->id;
}

// 添加闹钟（绝对时间）
int alarm_manager_add_absolute(int hour, int minute, const char *message) {
    alarm_lock();
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Max alarms reached");
        alarm_unlock();
        return -1;
    }
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    
    // 设置目标时间
    timeinfo->tm_hour = hour;
    timeinfo->tm_min = minute;
    timeinfo->tm_sec = 0;
    
    time_t target = mktime(timeinfo);
    
    // 如果时间已过，设置为明天
    if (target <= now) {
        target += 24 * 3600;
    }
    
    alarm_t *new_alarm = &alarms[alarm_count];
    new_alarm->id = next_id++;
    new_alarm->trigger_time = target;
    new_alarm->is_active = true;
    strncpy(new_alarm->message, message, MAX_MESSAGE_LEN - 1);
    
    alarm_count++;
    
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&target));
    
    ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%s, Message='%s'", 
             new_alarm->id, time_str, message);
    
    alarm_unlock();
    return new_alarm->id;
}

// 娣诲姞闂归挓锛堟寚瀹氳Е鍙戞椂闂存埅锛?
int alarm_manager_add_epoch(time_t trigger_time, const char *message) {
    alarm_lock();
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Max alarms reached");
        alarm_unlock();
        return -1;
    }

    alarm_t *new_alarm = &alarms[alarm_count];
    new_alarm->id = next_id++;
    new_alarm->trigger_time = trigger_time;
    new_alarm->is_active = true;
    strncpy(new_alarm->message, message, MAX_MESSAGE_LEN - 1);
    new_alarm->message[MAX_MESSAGE_LEN - 1] = '\0';

    alarm_count++;

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&trigger_time));
    ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%s, Message='%s'",
             new_alarm->id, time_str, message);

    alarm_unlock();
    return new_alarm->id;
}

// 移除闹钟
bool alarm_manager_remove(int alarm_id) {
    alarm_lock();
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].id == alarm_id) {
            // 用最后一个覆盖
            if (i < alarm_count - 1) {
                alarms[i] = alarms[alarm_count - 1];
            }
            alarm_count--;
            ESP_LOGI(TAG, "Alarm %d removed", alarm_id);
            alarm_unlock();
            return true;
        }
    }
    alarm_unlock();
    return false;
}

// 获取下一个要触发的闹钟
alarm_t* alarm_manager_get_next(void) {
    alarm_lock();
    if (alarm_count == 0) {
        alarm_unlock();
        return NULL;
    }
    
    alarm_t *next = &alarms[0];
    for (int i = 1; i < alarm_count; i++) {
        if (alarms[i].trigger_time < next->trigger_time) {
            next = &alarms[i];
        }
    }
    alarm_unlock();
    return next;
}

// 检查闹钟是否到期
alarm_t* alarm_manager_check_expired(void) {
    alarm_lock();
    time_t now = time(NULL);
    if (now == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
    }
    
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].is_active && alarms[i].trigger_time <= now) {
            alarms[i].is_active = false;  // 标记为已触发
            alarm_unlock();
            return &alarms[i];
        }
    }
    alarm_unlock();
    return NULL;
}

// 获取所有闹钟
int alarm_manager_get_all(alarm_t *alarms_out, int max_count) {
    alarm_lock();
    int count = (alarm_count < max_count) ? alarm_count : max_count;
    memcpy(alarms_out, alarms, count * sizeof(alarm_t));
    alarm_unlock();
    return count;
}

// 打印所有闹钟
void alarm_manager_print_all(void) {
    alarm_lock();
    if (alarm_count == 0) {
        ESP_LOGI(TAG, "No active alarms");
        alarm_unlock();
        return;
    }
    
    ESP_LOGI(TAG, "=== Active Alarms (%d) ===", alarm_count);
    for (int i = 0; i < alarm_count; i++) {
        struct tm *timeinfo = localtime(&alarms[i].trigger_time);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
        
        ESP_LOGI(TAG, "ID=%d, Time=%s, Message='%s'%s", 
                 alarms[i].id, time_str, alarms[i].message,
                 alarms[i].is_active ? "" : " (inactive)");
    }
    alarm_unlock();
}
