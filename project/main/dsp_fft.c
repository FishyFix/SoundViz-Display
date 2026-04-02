#include "dsp_fft.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_dsp.h"
#include "esp_log.h"
#include "draw_display.h"

TaskHandle_t fft_task_handle = NULL;

volatile bool fft_task_running = false;
volatile bool fft_enabled = false;

// log bands in [start bin 1, start bin 2,..., end bin +1]
// given sampling rate of 48kHz and FFT Size of 4096
static const int fft32logbins[] = {3,4,5,6,7,9,10,12,15,18,22,27,33,40,49,59,72,88,107,130,158,193,235,287,350,426,520,633,772,942,1148,1400,1707};
static const int fft16logbins[] = {3,5,7,10,15,22,33,49,72,107,158,235,350,520,772,1148,1707};
static const int fft8logbins[]  = {3,7,15,33,72,158,350,772,1707};

const int *fftlogbins = NULL;

float logbands[MAX_BANDS];
float x1[N_SAMPLES];
float wind[N_SAMPLES];
int N = N_SAMPLES;
int num_bands = MAX_BANDS;

static float history_lin[AVG_COUNT][MAX_BANDS] = {0};
static float running_sum_lin[MAX_BANDS] = {0};
static int history_index = 0;
static int filled = 0;

QueueHandle_t display_queue;

static const char *TAG = "dsp_fft";

//// FUNCTIONS


float fast_log2f(float x) {
    union { float f; uint32_t i; } vx = { x };
    float y = (float)(vx.i >> 23) - 127;
    vx.i = (vx.i & 0x7FFFFF) | 0x3F800000;
    float f = vx.f - 1.0f;
    return y + f - f * f * 0.5f; // First-order Taylor
}

void compute_fftlogbins() {
    switch (num_bands)
    {
    case 32: fftlogbins = fft32logbins; break;
    case 16: fftlogbins = fft16logbins; break;
    case 8:  fftlogbins = fft8logbins;  break;
    default:
        printf("Unsupported number of bands!\n");
        return;
    }
}

void compute_log_bands_from_spectrum(float *x1, int num_bands, float *logbands)
{
    for (int b = 0; b < num_bands; b++) {
        float sum = 0.0f;
        int end = fftlogbins[b+1];
        int start = fftlogbins[b];
        int count = end - start;
        for (int i = start; i <= end; i++) {
            sum += x1[i];
        }
        logbands[b] = (count > 0) ? 3.0103f * fast_log2f(sum / count) : 0.0f;
    }
}


//// MAIN

void init_fft()
{
    esp_err_t ret;
    ESP_LOGI(TAG, "Start Example.");
    ret = dsps_fft2r_init_fc32(NULL, N >> 1);
    if (ret  != ESP_OK) {
        ESP_LOGE(TAG, "Not possible to initialize FFT2R. Error = %i", ret);
        return;
    }

    ret = dsps_fft4r_init_fc32(NULL, N >> 1);
    if (ret  != ESP_OK) {
        ESP_LOGE(TAG, "Not possible to initialize FFT4R. Error = %i", ret);
        return;
    }

    // Generate hann window
    dsps_wind_hann_f32(wind, N);

    display_queue = xQueueCreate(1, sizeof(spectrum_frame_t));
}

void apply_fft() {
    // Apply window
    dsps_mul_f32(x1, wind, x1, N, 1, 1, 1);

    // FFT Radix-2
    dsps_fft2r_fc32(x1, N >> 1);
    // Bit reverse
    dsps_bit_rev2r_fc32(x1, N >> 1);
    // Convert one complex vector with length N/2 to one real spectrum vector with length N/2
    dsps_cplx2real_fc32(x1, N >> 1);
    // power
    float inv_N = 1.0f / N;
    for (int i = 0; i < N / 2; i++) {
        float re = x1[i * 2];
        float im = x1[i * 2 + 1];
        x1[i] = (re * re + im * im + 1e-7f) * inv_N;
    }
}

void fft_task(void *arg)
{   
    compute_fftlogbins();

    spectrum_frame_t frame;

    while (fft_task_running)
    {   
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!fft_enabled) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        apply_fft();

        // 1️⃣ sliding window + log computation
        for (int b = 0; b < num_bands; b++) {

            float sum = 0.0f;
            int start = fftlogbins[b];
            int end   = fftlogbins[b+1];
            int count = end - start;

            for (int i = start; i <= end; i++) {
                sum += x1[i];
            }

            float lin_val = (count > 0) ? (sum / count) : 0.0f;

            running_sum_lin[b] -= history_lin[history_index][b];
            history_lin[history_index][b] = lin_val;
            running_sum_lin[b] += lin_val;

            float avg_lin = running_sum_lin[b] / filled;

            // store directly into frame
            frame.bands[b] = 3.0103f * fast_log2f(avg_lin + 1e-9f);
        }

        // 2️⃣ update ring buffer
        history_index = (history_index + 1) % AVG_COUNT;
        if (filled < AVG_COUNT) filled++;

        // 3️⃣ send to display (OVERWRITE = key for realtime audio)
        xQueueOverwrite(display_queue, &frame);

        if (render_task_handle != NULL) {
                xTaskNotifyGive(render_task_handle);
        } 
    }

    vTaskDelete(NULL);
}

void start_fft_task() {   
    fft_task_running = true;
    fft_enabled = true;
    xTaskCreate(fft_task,"fft_task",4096,NULL,5,&fft_task_handle);
}

void stop_fft_task() {
    fft_task_running = false;
    fft_enabled = false;
    xTaskNotifyGive(fft_task_handle); // wake it up if blocked
}