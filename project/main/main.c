#include <stdio.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_dsp.h"
#include "dsp_fft.h" 
#include "i2s_reader.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "draw_display.h"

/* INFORMATION

MIC = INMP441
MIC_SENSITIVITY = -26 dBFS @ 94 dBspl
MIC_MAX_SPL = 120 dBspl
MIC_DYNAMIC_RANGE = 87 dB
MIC_NOISE_FLOOR_A = -87 dBFS or 120 - 87 = 33 dBspl
*/
const int MIC_SENS = -26; // dBFS @ 94 dBspl
const int MIC_MAX_SPL = 120; // dBspl
const int MIC_DNR = 87; // dB
const int MIC_NOISE_FLOOR = MIC_MAX_SPL - MIC_DNR; // dBsplA

const int MAX_SPL_DISPLAY = 100; // highest SPL to display in the spectrum
const int MIN_SPL_DISPLAY = 40; // lowest SPL to display in the spectrum

const int MAX_DBFS_DISPLAY = 0 - (MIC_MAX_SPL - MAX_SPL_DISPLAY);
const int MIN_DBFS_DISPLAY = 0 - (MIC_MAX_SPL - MIN_SPL_DISPLAY);

static const char *TAG = "main";

float x2[N_SAMPLES];

// configurable parameters
int bnum = 32; // number of spectrum bars (logarithmic)

// tasks: i2s, fft, draw, control 
            
//test function to run FFT and print results to console
void testtone_console_test() {

    ESP_LOGI(TAG, "Starting Testone FFT to console example");

    init_fft();

    while (1) {

        int64_t t0 = esp_timer_get_time();

        // Generate input signal
        float f_norm = 2300.0f / FS;
        dsps_tone_gen_f32(x1, N_SAMPLES, 0.1, f_norm, 0);  // 🔥 regenerate every time

        // 👉 ONLY HERE we do FFT manually
        apply_fft(); 
        compute_log_bands_from_spectrum(x1, bnum, logbands);

        int64_t t1 = esp_timer_get_time();

        // visualization 
        for (int i = 0; i < N_SAMPLES / 2; i++)
        {
            float val = x1[i];
            if (val < 1e-12f) val = 1e-12f;
            x2[i] = 10 * log10f(val);
        }

        int64_t t2 = esp_timer_get_time();

        dsps_view(x2, N_SAMPLES / 2, 128, 10, MIN_DBFS_DISPLAY, MAX_DBFS_DISPLAY, '|');

        int64_t ffttime = t1 - t0;
        int64_t posttime = t2 - t1;

        ESP_LOGI(TAG, " FFT TIME: %.2f ms    POST TIME: %.2f ms", ffttime / 1000.0, posttime / 1000.0);

        dsps_view(logbands, bnum, bnum, 10, MIN_DBFS_DISPLAY, MAX_DBFS_DISPLAY, '|');

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//test function to run FFT and print results to console
void i2sfft_console_test() {
    
    ESP_LOGI(TAG, "Starting I2S FFT to console example");

    init_fft();
    i2s_setup();

    start_fft_task(); // FFT task exists before I2S begins notifying
    start_i2s_read_task();

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        int64_t t1 = esp_timer_get_time();

        // visualization
        for (int i = 0; i < N_SAMPLES / 2; i++)
        {
            float val = x1[i];
            if (val < 1e-12f) val = 1e-12f;
            x2[i] = 10 * log10f(val);
        }

        int64_t t2 = esp_timer_get_time();

        dsps_view(x2, N_SAMPLES / 2, 128, 10, MIN_DBFS_DISPLAY, MAX_DBFS_DISPLAY, '|');

        int64_t posttime = t2 - t1;

        ESP_LOGI(TAG, "POST TIME: %.2f ms", posttime / 1000.0);

        dsps_view(logbands, bnum, bnum, 10, MIN_DBFS_DISPLAY, MAX_DBFS_DISPLAY, '|');

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Delete tasks and clean up
    stop_fft_task();
    stop_i2s_read_task();

    //To idle tasks instead of deleting
    //use fft_enabled and i2s_enabled flags
}

void display_test() {
    // Initialize display
    if (!init_display()) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    while (1) {
        //render_display(render_test_spectrum);
        // Control speed
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void display_spectrum() {

    ESP_LOGI(TAG, "Starting I2S FFT to console example");

    init_fft();
    i2s_setup();

    start_fft_task(); // FFT task exists before I2S begins notifying
    start_i2s_read_task();

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Initialize display
    if (!init_display()) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    set_renderer(render_spectrum_simple);
}

// The app_main function is called from the main task.  
void app_main() {
    //i2sfft_console_test();
    display_spectrum();
}