#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include "driver/i2s_std.h"
#include "esp_err.h"

// 初始化录音
esp_err_t recorder_init(i2s_chan_handle_t *rx_handle);

// 录音到内存（推荐）
esp_err_t recorder_start_to_memory(int16_t **audio_out, int *sample_count, float max_seconds);

// 录音到Flash（备用）
esp_err_t recorder_start_to_flash(const char *filename, float max_seconds);

// 停止录音
void recorder_stop(void);

// 检查是否正在录音
bool recorder_is_recording(void);

// 获取录音采样数
int recorder_get_samples(void);

#endif