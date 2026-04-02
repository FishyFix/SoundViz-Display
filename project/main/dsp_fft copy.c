#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include <math.h>
#include "esp_timer.h"
#include "esp_dsp.h"

// log bands in [start bin 1, start bin 2,..., end bin +1]
// given sampling rate of 48kHz and FFT Size of 4096
static const int fft32logbins[] = {3,4,5,6,7,9,10,12,15,18,22,27,33,40,49,59,72,88,107,130,158,193,235,287,350,426,520,633,772,942,1148,1400,1707};
static const int fft16logbins[] = {3,5,7,10,15,22,33,49,72,107,158,235,350,520,772,1148,1707};
static const int fft8logbins[]  = {3,7,15,33,72,158,350,772,1707};

float logbands[32];

static const char *TAG = "main";

// This example shows how to use FFT from esp-dsp library
#define FS 48000
#define N_SAMPLES 4096 // Amount of real input samples
int N = N_SAMPLES;
// Input test array
float x1[N_SAMPLES];
// Window coefficients
float wind[N_SAMPLES];
// Pointer to result array
float *y1_cf = &x1[0];


//// FUNCTIONS


float fast_log2f(float x) {
    union { float f; uint32_t i; } vx = { x };
    float y = (float)(vx.i >> 23) - 127;
    vx.i = (vx.i & 0x7FFFFF) | 0x3F800000;
    float f = vx.f - 1.0f;
    return y + f - f * f * 0.5f; // First-order Taylor
}

void compute_log_bands_from_spectrum(float *x1, int num_bands, float *logbands)
{
    const int *fftlogbins = NULL;

    switch (num_bands)
    {
    case 32:
        fftlogbins = fft32logbins;
        break;
    case 16:
        fftlogbins = fft16logbins;
        break;
    case 8:
        fftlogbins = fft8logbins;
        break;
    default:
        printf("Unsupported number of bands!\n");
        break;
    }

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

void app_main()
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
    // Generate input signal for x1 A=1 , F=0.16
    float f_norm = 2300.0f/FS;
    printf("F: %.2f ",f_norm);
    printf("\n");
    dsps_tone_gen_f32(x1, N, 1.0, f_norm,  0);

    int64_t t0 = esp_timer_get_time();

    // Apply window
    dsps_mul_f32(x1, wind, x1, N, 1, 1, 1);

    int64_t t1 = esp_timer_get_time();

    // FFT Radix-2
    unsigned int start_r2 = dsp_get_cpu_cycle_count();
    dsps_fft2r_fc32(x1, N >> 1);
    // Bit reverse
    dsps_bit_rev2r_fc32(x1, N >> 1);
    // Convert one complex vector with length N/2 to one real spectrum vector with length N/2
    dsps_cplx2real_fc32(x1, N >> 1);
    unsigned int end_r2 = dsp_get_cpu_cycle_count();
    int64_t t2 = esp_timer_get_time();
    // power
    float inv_N = 1.0f / N;
    for (int i = 0; i < N / 2; i++) {
        float re = x1[i * 2];
        float im = x1[i * 2 + 1];
        x1[i] = (re * re + im * im + 1e-7f) * inv_N;
    }

    int bnum = 32;

    compute_log_bands_from_spectrum(x1, bnum, logbands);

    int64_t t3 = esp_timer_get_time();

    // Show power spectrum in 64x10 window from -100 to 0 dB from 0..N/4 samples
    ESP_LOGW(TAG, "Signal x1 windowed");
    dsps_view(x1, N / 2, 128, 10,  -30, 100, '|');
    ESP_LOGI(TAG, "FFT Radix 2 for %i complex points take %i cycles", N / 2, end_r2 - start_r2);
    ESP_LOGI(TAG, "End Example.");
    int64_t wintime = t1 - t0;
    int64_t ffttime = t2 - t1;
    int64_t posttime = t3 - t2;
    int64_t totaltime = wintime + ffttime + posttime;

    ESP_LOGI(TAG, "WIN TIME: %.2f ms    FFT TIME: %.2f ms    POST TIME: %.2f ms", wintime/1000.0, ffttime/1000.0, posttime/1000.0);
    ESP_LOGI(TAG, "TOTAL TIME: %.2f ms", totaltime/1000.0);
    dsps_view(logbands, bnum, bnum/2, 10, -110, 10, '|');

    for (int b = 0; b < bnum; b++) {
        ESP_LOGI(TAG, "B %i: %.2f", b+1,logbands[b]);
    }
}


