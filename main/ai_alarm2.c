#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "config.h"
#include "alarm_manager.h"
#include "wifi_connect.h"
#include "baidu_asr.h"
#include "voice_task.h"
#include "alarm_task.h"
#include "utils.h"
#include "http_server.h"
#include "esp_netif.h" 

static const char *TAG = "MAIN";
static i2s_chan_handle_t mic = NULL, spk = NULL;

static esp_err_t i2s_init_rx(i2s_chan_handle_t *handle, int bclk, int ws, int din) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 256,
        .auto_clear = true,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, handle);
    if (err != ESP_OK) return err;

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
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
        .dma_desc_num = 4,
        .dma_frame_num = 512,
        .auto_clear = true,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, handle, NULL);
    if (err != ESP_OK) return err;
    
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = bclk,
        .ws = ws,
        .dout = dout,
        .din = I2S_GPIO_UNUSED,
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

void app_main(void) {
    ESP_LOGI(TAG, "AI闹钟启动");
    
    nvs_flash_init();
    alarm_manager_init();
    alarm_manager_load();
    
    ESP_ERROR_CHECK(i2s_init_rx(&mic, MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN));
    ESP_ERROR_CHECK(i2s_init_tx(&spk, SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DIN_PIN));
    
    utils_set_speaker(spk);
    utils_set_sample_rate(SAMPLE_RATE);
    voice_task_set_mic(mic);
    voice_task_set_speaker(spk);
    alarm_task_set_speaker(spk);
    
    beep(880, 200, 8000);
    vTaskDelay(pdMS_TO_TICKS(300));
    
    wifi_init_sta();
    wifi_wait_connected(pdMS_TO_TICKS(10000));
    baidu_asr_init();
    
    start_http_server();
    
    // 打印 IP 地址
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
    ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&ip_info.ip));

    xTaskCreate(voice_task, "voice", 8192, NULL, 5, NULL);
    xTaskCreate(alarm_task, "alarm", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "就绪");
}