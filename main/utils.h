#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include "driver/i2s_std.h"

void utils_set_speaker(i2s_chan_handle_t spk);
void utils_set_sample_rate(int rate);
void beep(int freq, int ms, int amp);
void play_audio(int16_t *audio, int samples);
void say_hello(void);
void say_sorry(void);

#endif