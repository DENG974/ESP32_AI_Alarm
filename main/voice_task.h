#ifndef VOICE_TASK_H
#define VOICE_TASK_H

#include "driver/i2s_std.h"

void voice_task(void *pv);
void voice_task_set_speaker(i2s_chan_handle_t spk);
void voice_task_set_mic(i2s_chan_handle_t mic);

#endif