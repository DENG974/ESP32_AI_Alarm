#ifndef CONFIG_H
#define CONFIG_H

// WiFi配置
#define WIFI_SSID ""
#define WIFI_PASS ""

// 百度API配置
#define BAIDU_API_KEY ""
#define BAIDU_SECRET_KEY ""

// 闹钟配置
#define MAX_ALARMS 10
#define MAX_MESSAGE_LEN 64

// 录音配置
#define SAMPLE_RATE 16000
#define RECORD_SEC 1.2f
#define VOLUME 1000

// 引脚配置
#define MIC_WS_PIN 25
#define MIC_SCK_PIN 33
#define MIC_SD_PIN 32
#define SPK_BCLK_PIN 26
#define SPK_LRC_PIN 27
#define SPK_DIN_PIN 22

#endif