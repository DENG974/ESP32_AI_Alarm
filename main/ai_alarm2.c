#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "alarm_manager.h"
#include "wifi_connect.h"
#include "baidu_asr.h"
#include "audio_recorder.h"
#include "esp_http_client.h"
#include "http_server.h"
#include "esp_netif.h"

// 引脚定义
#define MIC_WS_PIN  25
#define MIC_SCK_PIN 33
#define MIC_SD_PIN  32
#define SPK_BCLK_PIN 26
#define SPK_LRC_PIN  27
#define SPK_DIN_PIN  22

#define SAMPLE_RATE 16000
#define DMA_BUFFER_SIZE 512
#define DMA_BUFFER_COUNT 4  
#define RECORD_SEC 1.2f

static const char *TAG = "ALARM";
static i2s_chan_handle_t mic = NULL, spk = NULL;

// ========== I2S 初始化 ==========
static esp_err_t i2s_init_rx(i2s_chan_handle_t *handle, int bclk, int ws, int din) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,          // 建议值 8
        .dma_frame_num = 256,       // 建议值 256
        .auto_clear = true,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, handle);
    if (err != ESP_OK) return err;

    // 1. 配置时钟，启用 APLL
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    clk_cfg.clk_src = I2S_CLK_SRC_APLL;  // 启用 APLL

    // 2. 配置槽位，使用 32-bit 采样
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    
    // 3. 配置 GPIO
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = bclk,
        .ws = ws,
        .dout = I2S_GPIO_UNUSED,
        .din = din,
    };

    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    err = i2s_channel_init_std_mode(*handle, &std_cfg);
    if (err != ESP_OK) return err;
    
    return i2s_channel_enable(*handle);
}

static esp_err_t i2s_init_tx(i2s_chan_handle_t *handle, int bclk, int ws, int dout) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = DMA_BUFFER_COUNT,
        .dma_frame_num = DMA_BUFFER_SIZE,
        .auto_clear = true,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, handle, NULL);
    if (err != ESP_OK) return err;
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
        },
    };
    
    err = i2s_channel_init_std_mode(*handle, &std_cfg);
    if (err != ESP_OK) return err;
    
    return i2s_channel_enable(*handle);
}

//中文数字转整数
int chinese_to_int(const char *str) {
    if (!str) return 0;
    
    // 查找"十"
    const char *shi = strstr(str, "十");
    if (shi) {
        int num = 10;
        // "十"前面有数字（三十 => 30）
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
        // "十"后面有数字（十五 => 15）
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
    
    // 个位数
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

// 播放提示音
void beep(int freq, int ms, int amp) {
    int samples = (SAMPLE_RATE * ms) / 1000;
    int16_t *buf = malloc(samples * 2 * sizeof(int16_t));
    if (!buf) return;
    
    for (int i = 0; i < samples; i++) {
        int16_t s = (int16_t)(amp * sinf(2 * 3.14159f * freq * i / SAMPLE_RATE));
        buf[i*2] = s;
        buf[i*2+1] = s;
    }
    
    size_t written;
    i2s_channel_write(spk, buf, samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    free(buf);
}

// 回放函数
void play_audio(int16_t *audio, int samples) {
    if (!audio || samples <= 0) return;
    
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

// 解析命令
void parse(const char *text) {
    char cmd[64];
    strcpy(cmd, text);
    int len = strlen(cmd);
    if (len > 0 && cmd[len-1] == '。') {
        cmd[len-1] = '\0';
    }
    
    // ========== 1. 询问时间 ==========
    if (strstr(cmd, "几点") || strstr(cmd, "什么时间") || strstr(cmd, "几点了")) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[64];
        if (tm_info->tm_hour >= 12) {
            sprintf(time_str, "下午%d点%d分", tm_info->tm_hour - 12, tm_info->tm_min);
        } else {
            sprintf(time_str, "上午%d点%d分", tm_info->tm_hour, tm_info->tm_min);
        }
        ESP_LOGI(TAG, "当前时间: %s", time_str);
        return;
    }
    
    // ========== 2. 绝对时间闹钟（先处理，避免被分钟匹配）==========
    // 匹配格式：16点40分提醒我、下午3点提醒我、3点提醒我
    if ((strstr(cmd, "点") || strstr(cmd, "时")) && (strstr(cmd, "提醒") || strstr(cmd, "闹钟"))) {
        int hour = 0, minute = 0;
        int is_pm = 0;
        
        // 检查上午/下午
        if (strstr(cmd, "下午") || strstr(cmd, "晚上")) {
            is_pm = 1;
        }
        if (strstr(cmd, "上午") || strstr(cmd, "早上")) {
            is_pm = 0;
        }
        
        // 提取小时
        for (const char *p = cmd; *p; p++) {
            if (*p >= '0' && *p <= '9') {
                hour = atoi(p);
                // 跳过数字
                while (*p >= '0' && *p <= '9') p++;
                // 找分钟
                if (*p == '点' || *p == '时') {
                    p++;
                    while (*p && (*p < '0' || *p > '9')) p++;
                    if (*p >= '0' && *p <= '9') {
                        minute = atoi(p);
                    }
                }
                break;
            }
        }
        
        // 处理下午时间
        if (is_pm && hour > 0 && hour < 12) {
            hour += 12;
        }
        
        // 提取提醒内容
        char message[64] = "闹钟";
        const char *msg_start = strstr(cmd, "提醒");
        if (msg_start) {
            msg_start += 6; // 跳过"提醒"
            int msg_len = 0;
            while (msg_start[msg_len] && msg_start[msg_len] != '。') {
                message[msg_len] = msg_start[msg_len];
                msg_len++;
            }
            message[msg_len] = '\0';
            if (strlen(message) == 0) strcpy(message, "闹钟");
        }
        
        if (hour >= 1 && hour <= 24) {
            int alarm_id = alarm_manager_add_absolute(hour, minute, message);
            if (alarm_id > 0) {
                ESP_LOGI(TAG, "闹钟已设置: %d点%d分 - %s", hour, minute, message);
            }
        } else {
            ESP_LOGW(TAG, "无法解析时间: %s", cmd);
        }
        return;
    }
    
    // ========== 3. 相对时间闹钟（X分钟后/小时后/秒后）==========
    int num = 0;
    for (const char *p = cmd; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            num = atoi(p);
            break;
        }
    }
    if (num == 0) num = chinese_to_int(cmd);
    if (num == 0) num = 3;
    
    if (strstr(cmd, "分钟") || strstr(cmd, "分")) {
        alarm_manager_add_relative(num, TIME_UNIT_MINUTES, "闹钟");
        ESP_LOGI(TAG, "闹钟: %d分钟后", num);
    }
    else if (strstr(cmd, "小时") || strstr(cmd, "时")) {
        alarm_manager_add_relative(num, TIME_UNIT_HOURS, "闹钟");
        ESP_LOGI(TAG, "闹钟: %d小时后", num);
    }
    else if (strstr(cmd, "秒")) {
        alarm_manager_add_relative(num, TIME_UNIT_SECONDS, "闹钟");
        ESP_LOGI(TAG, "闹钟: %d秒后", num);
    }
    else {
        ESP_LOGW(TAG, "无法识别: %s", text);
    }
}
// 计算音量
uint32_t volume(const int16_t *buf, int len) {
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
                ESP_LOGI(TAG, "检测到语音，开始录音...");
                is_recording = true;
                
                beep(880, 100, 8000);
                vTaskDelay(pdMS_TO_TICKS(50));
                beep(880, 100, 8000);
                
                int16_t *audio = NULL;
                int cnt = 0;
                esp_err_t ret = recorder_start_to_memory(&audio, &cnt, RECORD_SEC);
                
                if (ret == ESP_OK && audio && cnt > 0) {
                    beep(440, 80, 8000);
                    
                    play_audio(audio, cnt);
                    
                    ESP_LOGI(TAG, "开始识别...");
                    char *result = NULL;
                    esp_err_t asr_ret = baidu_asr_recognize(audio, cnt, &result);
                    
                    if (asr_ret == ESP_OK && result) {
                        beep(880, 80, 8000);
                        beep(1320, 80, 8000);
                        ESP_LOGI(TAG, "识别结果: %s", result);
                        set_latest_recognition(result);
                        parse(result);
                        free(result);
                    } else {
                        beep(220, 150, 8000);
                        ESP_LOGW(TAG, "识别失败: %s", baidu_asr_last_error());
                    }
                    
                    free(audio);
                } else {
                    beep(220, 200, 8000);
                    ESP_LOGE(TAG, "录音失败");
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

void alarm_task(void *pv) {
    while (1) {
        alarm_t *a = alarm_manager_check_expired();
        if (a) {
            for (int i = 0; i < 5; i++) {
                beep(880, 200, 10000);
                vTaskDelay(pdMS_TO_TICKS(200));
                beep(1320, 200, 10000);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            alarm_manager_remove(a->id);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "AI闹钟启动");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化组件
    alarm_manager_init();
    
    // 初始化I2S
    ESP_ERROR_CHECK(i2s_init_rx(&mic, MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN));
    ESP_ERROR_CHECK(i2s_init_tx(&spk, SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DIN_PIN));
    
    // 开机音乐
    beep(523, 150, 6000);
    vTaskDelay(pdMS_TO_TICKS(150));
    beep(587, 150, 6000);
    vTaskDelay(pdMS_TO_TICKS(150));
    beep(659, 300, 6000);
    vTaskDelay(pdMS_TO_TICKS(200));
    beep(880, 400, 8000);
    
    // 连接WiFi
    wifi_init_sta();
    esp_err_t wifi_ret = wifi_wait_connected(pdMS_TO_TICKS(10000));
    
    if (wifi_ret == ESP_OK) {
        start_http_server();
        
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif, &ip_info);
            ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&ip_info.ip));
        }
        
        // 初始化百度ASR
        baidu_asr_init();
        ESP_LOGI(TAG, "WiFi已连接，语音识别就绪");
    } else {
        ESP_LOGW(TAG, "WiFi连接失败，语音识别不可用");
    }
    
    // 等待时间同步
    vTaskDelay(pdMS_TO_TICKS(2000));
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    ESP_LOGI(TAG, "当前时间: %02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    // 创建任务
    xTaskCreate(voice_task, "voice", 8192, NULL, 5, NULL);
    xTaskCreate(alarm_task, "alarm", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "就绪");
}