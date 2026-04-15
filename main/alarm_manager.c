#include "alarm_manager.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "ALARM_MGR";

static alarm_t alarms[MAX_ALARMS];
static int alarm_count = 0;
static int next_id = 1;
static int pending_alarm_id = -1;
static SemaphoreHandle_t alarm_mutex = NULL;

#define NVS_NAMESPACE "alarm_data"

// 初始化闹钟管理器
void alarm_manager_init(void) {
    memset(alarms, 0, sizeof(alarms));
    alarm_count = 0;
    next_id = 1;
    pending_alarm_id = -1;
    
    if (alarm_mutex == NULL) {
        alarm_mutex = xSemaphoreCreateMutex();
    }
    
    ESP_LOGI(TAG, "Alarm manager initialized");
}

// 保存到NVS
void alarm_manager_save(void) {
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, "count", alarm_count);
        nvs_set_i32(handle, "next_id", next_id);
        nvs_set_blob(handle, "alarms", alarms, sizeof(alarms));
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGD(TAG, "Alarms saved: %d", alarm_count);
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
}

// 从NVS加载
void alarm_manager_load(void) {
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        int32_t count = 0, id = 0;
        size_t size = sizeof(alarms);
        
        nvs_get_i32(handle, "count", &count);
        nvs_get_i32(handle, "next_id", &id);
        
        if (count > 0 && count <= MAX_ALARMS) {
            nvs_get_blob(handle, "alarms", alarms, &size);
            alarm_count = count;
            next_id = id;
            ESP_LOGI(TAG, "Loaded %d alarms from NVS", alarm_count);
        }
        
        nvs_close(handle);
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
}

// 添加闹钟（相对时间）
int alarm_manager_add_relative(int value, time_unit_t unit, const char *message) {
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Max alarms reached");
        return -1;
    }
    
    time_t now = time(NULL);
    if (now == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
    }
    
    int seconds = 0;
    switch (unit) {
        case TIME_UNIT_SECONDS: seconds = value; break;
        case TIME_UNIT_MINUTES: seconds = value * 60; break;
        case TIME_UNIT_HOURS: seconds = value * 3600; break;
        default: seconds = value * 60;
    }
    
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    alarm_t *new_alarm = &alarms[alarm_count];
    new_alarm->id = next_id++;
    new_alarm->trigger_time = now + seconds;
    new_alarm->is_active = true;
    strncpy(new_alarm->message, message, MAX_MESSAGE_LEN - 1);
    new_alarm->message[MAX_MESSAGE_LEN - 1] = '\0';
    
    alarm_count++;
    pending_alarm_id = new_alarm->id;
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    
    alarm_manager_save();
    
    struct tm *timeinfo = localtime(&new_alarm->trigger_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%s", new_alarm->id, time_str);
    
    return new_alarm->id;
}

// 添加闹钟（绝对时间）
int alarm_manager_add_absolute(int hour, int minute, const char *message) {
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Max alarms reached");
        return -1;
    }
    
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    
    timeinfo->tm_hour = hour;
    timeinfo->tm_min = minute;
    timeinfo->tm_sec = 0;
    
    time_t target = mktime(timeinfo);
    if (target <= now) {
        target += 24 * 3600;
    }
    
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    alarm_t *new_alarm = &alarms[alarm_count];
    new_alarm->id = next_id++;
    new_alarm->trigger_time = target;
    new_alarm->is_active = true;
    strncpy(new_alarm->message, message, MAX_MESSAGE_LEN - 1);
    new_alarm->message[MAX_MESSAGE_LEN - 1] = '\0';
    
    alarm_count++;
    pending_alarm_id = new_alarm->id;
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    
    alarm_manager_save();
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&target));
    ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%s", new_alarm->id, time_str);
    
    return new_alarm->id;
}

// 设置闹钟名称
void alarm_set_name(int alarm_id, const char *name) {
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].id == alarm_id) {
            strncpy(alarms[i].message, name, MAX_MESSAGE_LEN - 1);
            alarms[i].message[MAX_MESSAGE_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Alarm %d name: %s", alarm_id, name);
            break;
        }
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    alarm_manager_save();
}

// 获取待命名的闹钟ID
int alarm_get_pending_id(void) {
    return pending_alarm_id;
}

// 清除待命名闹钟
void alarm_clear_pending(void) {
    pending_alarm_id = -1;
}

// 移除闹钟
bool alarm_manager_remove(int alarm_id) {
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    bool found = false;
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].id == alarm_id) {
            if (i < alarm_count - 1) {
                alarms[i] = alarms[alarm_count - 1];
            }
            alarm_count--;
            found = true;
            ESP_LOGI(TAG, "Alarm %d removed", alarm_id);
            break;
        }
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    
    if (found) alarm_manager_save();
    return found;
}

// 获取下一个要触发的闹钟
alarm_t* alarm_manager_get_next(void) {
    if (alarm_count == 0) return NULL;
    
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    alarm_t *next = &alarms[0];
    for (int i = 1; i < alarm_count; i++) {
        if (alarms[i].trigger_time < next->trigger_time) {
            next = &alarms[i];
        }
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    return next;
}

// 检查闹钟是否到期
alarm_t* alarm_manager_check_expired(void) {
    time_t now = time(NULL);
    if (now == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
    }
    
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    for (int i = 0; i < alarm_count; i++) {
        if (alarms[i].is_active && alarms[i].trigger_time <= now) {
            alarms[i].is_active = false;
            if (alarm_mutex) xSemaphoreGive(alarm_mutex);
            return &alarms[i];
        }
    }
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    return NULL;
}

// 获取所有闹钟
int alarm_manager_get_all(alarm_t *alarms_out, int max_count) {
    if (alarm_mutex) xSemaphoreTake(alarm_mutex, portMAX_DELAY);
    
    int count = (alarm_count < max_count) ? alarm_count : max_count;
    memcpy(alarms_out, alarms, count * sizeof(alarm_t));
    
    if (alarm_mutex) xSemaphoreGive(alarm_mutex);
    return count;
}

// 打印所有闹钟
void alarm_manager_print_all(void) {
    if (alarm_count == 0) {
        ESP_LOGI(TAG, "No active alarms");
        return;
    }
    
    ESP_LOGI(TAG, "=== Active Alarms (%d) ===", alarm_count);
    for (int i = 0; i < alarm_count; i++) {
        struct tm *timeinfo = localtime(&alarms[i].trigger_time);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
        ESP_LOGI(TAG, "ID=%d, Time=%s, Message='%s'", 
                 alarms[i].id, time_str, alarms[i].message);
    }
}