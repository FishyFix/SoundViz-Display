// draw_display.h

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef DRAW_DISPLAY_H
#define DRAW_DISPLAY_H

#define DISPLAY_WIDTH 32
#define DISPLAY_HEIGHT 16
#define DISPLAY_CHAIN 1

#define MAX_BARS 32

#define BRIGHTNESS_MAX 120
#define BRIGHTNESS_MIN 10
#define BRIGHTNESS_STEP 10
#define BRIGHTNESS_DEFAULT 80

#define DOUBLE_BUFF true

extern float smoothedWaveform[DISPLAY_WIDTH];

extern TaskHandle_t render_task_handle;

typedef void (*render_func_t)();

typedef struct {
    int height;
    uint16_t color;
} bar_t;


// need to declare functions as extern c but also use C++ features 
// because ESP32-HUB75-MatrixPanel-I2S-DMA library is C++
#ifdef __cplusplus
extern "C" {
#endif

bool init_display(void);
void start_render_task();
// void draw_spectrum();
// void draw_text();
void compute_waveform(int32_t *samples, int sample_count);
void render_waveform();
void render_text();
void render_test_movingpixel();
void render_test_horizontal_bars();
void render_test_spectrum();
void render_spectrum_simple();
void render_spectrum();
void render_fft_bars();
void set_renderer(render_func_t renderer);
// void draw_visualization();
// void change_color_scheme();

#ifdef __cplusplus
}
#endif

#endif // DRAW_DISPLAY_H