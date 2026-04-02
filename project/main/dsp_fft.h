// dsp_fft.h

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef DSP_FFT_H
#define DSP_FFT_H

extern TaskHandle_t fft_task_handle;

#define FS 48000
#define N_SAMPLES 4096
#define AVG_COUNT 4   // try 2–8
#define MAX_BANDS 32

extern float x1[N_SAMPLES];
extern float logbands[MAX_BANDS];

extern QueueHandle_t display_queue;

typedef struct {
    float bands[MAX_BANDS];
} spectrum_frame_t;

extern volatile bool fft_task_running;
extern volatile bool fft_enabled;

void init_fft();
void apply_fft();
void compute_log_bands_from_spectrum(float *x1, int num_bands, float *logbands);
float fast_log2f(float x);
void fft_task(void *arg);
void start_fft_task(void);
void stop_fft_task(void);

#endif