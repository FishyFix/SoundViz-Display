#include "i2s_reader.h"
#include "dsp_fft.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <stdlib.h>
#include "driver/gpio.h"
#include "dsp_fft.h"  // to access x1[]
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NORMFACTOR (1.0f / 8388608.0f)

#define I2S_BCLK  GPIO_NUM_14
#define I2S_WS    GPIO_NUM_27
#define I2S_DIN   GPIO_NUM_35

#define AUDIO_BUFF_SIZE (N_SAMPLES * sizeof(int32_t))  // MONO

static const char* TAG = "I2S";
i2s_chan_handle_t rx_handle;
TaskHandle_t i2s_task_handle;

volatile bool i2s_data_ready = false;  // ✅ Definition in one place
volatile bool i2s_task_running = false;
volatile bool i2s_enabled = false;

void i2s_setup() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
    .clk_cfg = {
        .sample_rate_hz = FS,
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },
    .slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .slot_mask = I2S_STD_SLOT_LEFT,   // IMPORTANT (your mic is LEFT)
        .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
        .ws_pol = false,
        .bit_shift = true,
        .msb_right = false,
    },
    .gpio_cfg = {
        .mclk = -1,
        .bclk = I2S_BCLK,
        .ws   = I2S_WS,
        .din  = I2S_DIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "I2S Setup Finished.");
}

// buffer in dsp_fft.c
extern float x1[N_SAMPLES];


static void i2s_read_task(void* args) {
    uint8_t* r_buf = (uint8_t*)calloc(1, AUDIO_BUFF_SIZE);
    assert(r_buf);
    size_t r_bytes = 0;

    while (i2s_task_running) {

        if (!i2s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(20)); // don't spin CPU
            continue;
        }

        if (i2s_channel_read(rx_handle, r_buf, AUDIO_BUFF_SIZE, &r_bytes, 1000) == ESP_OK) {
            //int sample_count = r_bytes / sizeof(int32_t);

            // Extract I2S audio
            int32_t* samples = (int32_t*)r_buf;

            for (int i = 0; i < N_SAMPLES; i++) {
                int32_t s = samples[i] >> 8;
                x1[i] = (float)s * NORMFACTOR;  // Normalize to [-1.0, 1.0]
            }
  
            // Notify FFT task that new data is ready
            if (fft_task_handle != NULL) {
                xTaskNotifyGive(fft_task_handle);
            }  
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // Low-latency // WHHY?
    }

    free(r_buf);
    vTaskDelete(NULL);
}

void start_i2s_read_task() {
    i2s_task_running = true;
    i2s_enabled = true;
    xTaskCreate(i2s_read_task, "i2s_read_task", N_SAMPLES, NULL, 5, &i2s_task_handle);
}

void stop_i2s_read_task() {
    i2s_task_running = false;
    i2s_enabled = false;
    xTaskNotifyGive(i2s_task_handle); // wake it up if blocked
}