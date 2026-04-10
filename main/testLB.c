#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"

// 引脚定义
#define SPK_BCLK_PIN 26
#define SPK_LRC_PIN  27
#define SPK_DIN_PIN  22

#define SAMPLE_RATE 16000
#define DMA_BUFFER_SIZE 1024
#define DMA_BUFFER_COUNT 8

static const char *TAG = "SPEAKER_TEST";
static i2s_chan_handle_t spk = NULL;

// 初始化喇叭
static esp_err_t speaker_init(void) {
    ESP_LOGI(TAG, "初始化喇叭...");
    
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = DMA_BUFFER_COUNT,
        .dma_frame_num = DMA_BUFFER_SIZE,
        .auto_clear = true,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, &spk, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建通道失败: %d", err);
        return err;
    }
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK_PIN,
            .ws = SPK_LRC_PIN,
            .dout = SPK_DIN_PIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    
    err = i2s_channel_init_std_mode(spk, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "初始化标准模式失败: %d", err);
        return err;
    }
    
    err = i2s_channel_enable(spk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启用通道失败: %d", err);
        return err;
    }
    
    ESP_LOGI(TAG, "喇叭初始化成功");
    return ESP_OK;
}

// 播放正弦波
void play_tone(int freq, int duration_ms, int amplitude) {
    if (!spk) {
        ESP_LOGE(TAG, "喇叭未初始化");
        return;
    }
    
    int samples = (SAMPLE_RATE * duration_ms) / 1000;
    int16_t *buffer = malloc(samples * 2 * sizeof(int16_t));
    if (!buffer) {
        ESP_LOGE(TAG, "内存分配失败");
        return;
    }
    
    // 生成正弦波
    for (int i = 0; i < samples; i++) {
        int16_t sample = (int16_t)(amplitude * sinf(2 * 3.14159f * freq * i / SAMPLE_RATE));
        buffer[i*2] = sample;     // 左声道
        buffer[i*2+1] = sample;   // 右声道
    }
    
    // 播放
    size_t bytes_written;
    esp_err_t err = i2s_channel_write(spk, buffer, samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "播放失败: %d", err);
    }
    
    free(buffer);
}

// 播放测试序列
void speaker_test(void) {
    ESP_LOGI(TAG, "========== 喇叭测试开始 ==========");
    
    // 测试1: 短促的哔哔声
    ESP_LOGI(TAG, "测试1: 哔哔声");
    play_tone(880, 200, 8000);
    vTaskDelay(pdMS_TO_TICKS(300));
    play_tone(880, 200, 8000);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试2: 上升音阶
    ESP_LOGI(TAG, "测试2: 上升音阶");
    int freqs[] = {262, 294, 330, 349, 392, 440, 494, 523};
    for (int i = 0; i < 8; i++) {
        play_tone(freqs[i], 200, 8000);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试3: 下降音阶
    ESP_LOGI(TAG, "测试3: 下降音阶");
    for (int i = 7; i >= 0; i--) {
        play_tone(freqs[i], 200, 8000);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试4: 警报声
    ESP_LOGI(TAG, "测试4: 警报声");
    for (int i = 0; i < 5; i++) {
        play_tone(880, 100, 10000);
        vTaskDelay(pdMS_TO_TICKS(150));
        play_tone(1320, 100, 10000);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试5: 持续音
    ESP_LOGI(TAG, "测试5: 持续音");
    play_tone(440, 1000, 6000);
    
    ESP_LOGI(TAG, "========== 喇叭测试完成 ==========");
}

// 主函数 - 必须命名为 app_main
void app_main(void) {
    ESP_LOGI(TAG, "启动喇叭测试程序");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化喇叭
    ESP_ERROR_CHECK(speaker_init());
    
    // 等待1秒
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 运行测试
    speaker_test();
    
    // 循环播放测试
    while (1) {
        ESP_LOGI(TAG, "10秒后重复测试...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        speaker_test();
    }
}