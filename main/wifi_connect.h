#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "esp_err.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t wifi_init_sta(void);
esp_err_t wifi_wait_connected(TickType_t timeout);
bool wifi_is_connected(void);

#endif