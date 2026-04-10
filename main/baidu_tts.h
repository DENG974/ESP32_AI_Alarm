#ifndef BAIDU_TTS_H
#define BAIDU_TTS_H

#include "esp_err.h"
#include "driver/i2s_std.h"
#include <stdbool.h>

// 初始化TTS
esp_err_t baidu_tts_init(void);

// 设置喇叭句柄
void baidu_tts_set_speaker(i2s_chan_handle_t spk);

// 文字转语音并播放（会播放提示音）
esp_err_t baidu_tts_speak(const char *text);

// 检查是否正在播放
bool baidu_tts_is_playing(void);

#endif