#ifndef ALARM_TASK_H
#define ALARM_TASK_H

#include "driver/i2s_std.h"

void alarm_task(void *pv);
void alarm_task_set_speaker(i2s_chan_handle_t spk);

#endif