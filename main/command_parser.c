#include "command_parser.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PARSER";

extern void beep(int freq, int ms, int amp);
extern void set_latest_recognition(const char *text);

static int chinese_to_int(const char *str) {
    if (!str) return 0;
    
    const char *shi = strstr(str, "十");
    if (shi) {
        int num = 10;
        if (shi > str) {
            char prev = *(shi - 1);
            if (prev >= '0' && prev <= '9') {
                num = (prev - '0') * 10;
            } else if (strchr("一二两", prev)) num = 10;
            else if (prev == '三') num = 30;
            else if (prev == '四') num = 40;
            else if (prev == '五') num = 50;
            else if (prev == '六') num = 60;
            else if (prev == '七') num = 70;
            else if (prev == '八') num = 80;
            else if (prev == '九') num = 90;
        }
        if (*(shi + 1) != '\0') {
            char next = *(shi + 1);
            if (next >= '0' && next <= '9') {
                num += (next - '0');
            } else if (next == '一') num += 1;
            else if (next == '二') num += 2;
            else if (next == '三') num += 3;
            else if (next == '四') num += 4;
            else if (next == '五') num += 5;
            else if (next == '六') num += 6;
            else if (next == '七') num += 7;
            else if (next == '八') num += 8;
            else if (next == '九') num += 9;
        }
        return num;
    }
    
    if (strstr(str, "一")) return 1;
    if (strstr(str, "二") || strstr(str, "两")) return 2;
    if (strstr(str, "三")) return 3;
    if (strstr(str, "四")) return 4;
    if (strstr(str, "五")) return 5;
    if (strstr(str, "六")) return 6;
    if (strstr(str, "七")) return 7;
    if (strstr(str, "八")) return 8;
    if (strstr(str, "九")) return 9;
    
    return 0;
}

void parse(const char *text) {
    char cmd[128];
    strcpy(cmd, text);
    int len = strlen(cmd);
    if (len > 0 && cmd[len-1] == '。') cmd[len-1] = '\0';
    
    // 显示用户说的话
    set_latest_recognition(cmd);
    
    // 打招呼
    if (strstr(cmd, "你好") || strstr(cmd, "嗨") || strstr(cmd, "您好")) {
        set_latest_recognition("你好！我是AI闹钟，有什么可以帮您？");
        beep(880, 100, 6000);
        vTaskDelay(pdMS_TO_TICKS(100));
        beep(660, 100, 6000);
        return;
    }
    
    // 查看闹钟列表
    if (strstr(cmd, "列表") || strstr(cmd, "查看")) {
        alarm_t list[MAX_ALARMS];
        int count = alarm_manager_get_all(list, MAX_ALARMS);
        if (count == 0) {
            set_latest_recognition("暂无闹钟");
            return;
        }
        char msg[256] = "闹钟列表：";
        for (int i = 0; i < count && i < 5; i++) {
            struct tm *t = localtime(&list[i].trigger_time);
            char item[80];
            snprintf(item, sizeof(item), " %d:%02d %s", t->tm_hour, t->tm_min, list[i].message);
            if (strlen(msg) + strlen(item) < sizeof(msg) - 1) {
                strcat(msg, item);
            }
        }
        set_latest_recognition(msg);
        return;
    }
    
    // 等待命名闹钟
    int pending_id = alarm_get_pending_id();
    if (pending_id != -1) {
        alarm_set_name(pending_id, cmd);
        alarm_clear_pending();
        char msg[160];
        snprintf(msg, sizeof(msg), "闹钟已设置：%s", cmd);
        set_latest_recognition(msg);
        beep(880, 100, 8000);
        return;
    }
    
    // 询问时间
    if (strstr(cmd, "几点") || strstr(cmd, "时间")) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char msg[80];
        snprintf(msg, sizeof(msg), "当前时间 %02d:%02d", t->tm_hour, t->tm_min);
        set_latest_recognition(msg);
        beep(880, 100, 6000);
        return;
    }
    
    // 提取数字
    int num = 0;
    for (const char *p = cmd; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            num = atoi(p);
            break;
        }
    }
    if (num == 0) num = chinese_to_int(cmd);
    if (num == 0) num = 3;
    
    // 相对时间闹钟
    if (strstr(cmd, "分钟") || strstr(cmd, "分")) {
        alarm_manager_add_relative(num, TIME_UNIT_MINUTES, "");
        set_latest_recognition("请问做什么？");
        return;
    }
    else if (strstr(cmd, "小时") || strstr(cmd, "时")) {
        alarm_manager_add_relative(num, TIME_UNIT_HOURS, "");
        set_latest_recognition("请问做什么？");
        return;
    }
    else if (strstr(cmd, "秒")) {
        alarm_manager_add_relative(num, TIME_UNIT_SECONDS, "");
        set_latest_recognition("请问做什么？");
        return;
    }
    else if (strstr(cmd, "点")) {
        int hour = 0, minute = 0;
        const char *p = cmd;
        while (*p) {
            if (*p >= '0' && *p <= '9') {
                hour = atoi(p);
                const char *dot = strstr(p, "点");
                if (dot) {
                    const char *min_start = dot + 1;
                    while (*min_start && (*min_start < '0' || *min_start > '9')) min_start++;
                    if (*min_start >= '0' && *min_start <= '9') minute = atoi(min_start);
                }
                break;
            }
            p++;
        }
        if (hour > 0) {
            alarm_manager_add_absolute(hour, minute, "");
            set_latest_recognition("请问做什么？");
            return;
        }
    }
    
    // 无法识别
    set_latest_recognition("抱歉，我没听清楚，请再说一遍");
    beep(440, 200, 6000);
}