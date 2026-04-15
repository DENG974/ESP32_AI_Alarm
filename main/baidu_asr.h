#ifndef BAIDU_ASR_H
#define BAIDU_ASR_H

#include "esp_err.h"

// 百度API配置
#define BAIDU_APP_ID     "7526759"
#define BAIDU_API_KEY    "ZwlqEH5L39IBuyH3HJURBlLQ" 
#define BAIDU_SECRET_KEY "lPJc0yQB7c5DWqQhSgx7JwLukTW5eQXm"

// 初始化百度ASR
esp_err_t baidu_asr_init(void);

// 获取访问令牌
esp_err_t baidu_asr_get_token(void);

// 语音识别：发送音频数据，返回识别文本
// audio_data: 16bit 16kHz 单声道 PCM数据
// audio_len: 采样点数
// result: 输出识别结果
esp_err_t baidu_asr_recognize(int16_t *audio_data, int audio_len, char **result);

// 获取最后错误信息
const char* baidu_asr_last_error(void);

#endif