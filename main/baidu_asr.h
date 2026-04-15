#ifndef BAIDU_ASR_H
#define BAIDU_ASR_H

#include "esp_err.h"

esp_err_t baidu_asr_init(void);
esp_err_t baidu_asr_get_token(void);
esp_err_t baidu_asr_recognize(int16_t *audio_data, int audio_len, char **result);
const char* baidu_asr_last_error(void);

#endif