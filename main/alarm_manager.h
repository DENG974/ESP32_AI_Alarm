#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_ALARMS 10
#define MAX_MESSAGE_LEN 64

// 时间单位枚举
typedef enum {
    TIME_UNIT_SECONDS,
    TIME_UNIT_MINUTES, 
    TIME_UNIT_HOURS
} time_unit_t;

// 闹钟结构体
typedef struct {
    int id;                         // 闹钟ID
    time_t trigger_time;            // 触发时间戳
    char message[MAX_MESSAGE_LEN];   // 提醒消息
    bool is_active;                  // 是否激活
} alarm_t;

// 初始化闹钟管理器
void alarm_manager_init(void);

// 添加闹钟（相对时间）
int alarm_manager_add_relative(int value, time_unit_t unit, const char *message);

// 添加闹钟（绝对时间，格式：HH:MM）
int alarm_manager_add_absolute(int hour, int minute, const char *message);

// 添加闹钟（指定触发时间戳）
int alarm_manager_add_epoch(time_t trigger_time, const char *message);

// 移除闹钟
bool alarm_manager_remove(int alarm_id);

// 获取下一个要触发的闹钟
alarm_t* alarm_manager_get_next(void);

// 检查闹钟是否到期
alarm_t* alarm_manager_check_expired(void);

int alarm_manager_get_all(alarm_t *alarms, int max_count);
void alarm_manager_print_all(void);
void alarm_manager_save(void);
void alarm_manager_load(void);
void alarm_set_name(int alarm_id, const char *name);
int alarm_get_pending_id(void);
void alarm_set_pending(int alarm_id);
void alarm_clear_pending(void);

#endif
