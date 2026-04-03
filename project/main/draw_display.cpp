#include "draw_display.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
#include "fonts.h"
#include "esp_log.h"
#include "math.h"
#include "esp_random.h"
#include "dsp_fft.h"

// HUB 75 Matrix Panel
// JXI5020GP 16-Channel Constant Current LED Sink Driver
// ICN74HC245TS octal bus transceiver with three-state outputs
// ICN74HC138 high-performance CMOS 3-to-8 line decoder/demultiplexer
// 4953 - 18CD - 1522


// GPIO pin definitions for HUB75 matrix panel
#define R1_PIN 4
#define G1_PIN 5
#define B1_PIN 18
#define R2_PIN 19
#define G2_PIN 21
#define B2_PIN 22
#define A_PIN 23
#define B_PIN 33
#define C_PIN 15
#define D_PIN -1
#define E_PIN -1 
#define LAT_PIN 25
#define OE_PIN 32
#define CLK_PIN 26  

static const char *TAG = "draw_display";

TaskHandle_t render_task_handle = NULL;

render_func_t display_renderer = nullptr;

float smoothedWaveform[DISPLAY_WIDTH] = {0};

static MatrixPanel_I2S_DMA *dma_display = nullptr;

bar_t bars[MAX_BARS];
int num_bars = 32;
int brightness = BRIGHTNESS_DEFAULT;

bool wait_for_render_notification = false;


bool init_display(void) {
    if (dma_display) {
        ESP_LOGW(TAG, "Display already initialized!");
        return true;
    }

    HUB75_I2S_CFG::i2s_pins _pins={R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
    HUB75_I2S_CFG mxconfig(
        DISPLAY_WIDTH,   // module width
        DISPLAY_HEIGHT,   // module height
        DISPLAY_CHAIN,    // Chain length
        _pins // pin mapping
    );

    mxconfig.latch_blanking = 2;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;
    mxconfig.setPixelColorDepthBits(6); // 6
    mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    //If your visualization has slightly non-black backgrounds (very dark blue, etc.) ghosting becomes far less visible.
    //If ghosting appears only on adjacent rows and is faint, it's typical driver settling ghosting.
    mxconfig.min_refresh_rate = 30;
    mxconfig.double_buff = DOUBLE_BUFF;  // ✅ enable double buffering

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display->begin()) {
        ESP_LOGE(TAG, "Failed to initialize display!");
        delete dma_display;
        dma_display = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Display initialized");

    dma_display->setBrightness(brightness); 

    start_render_task();

    //render_display(nullptr);
    return true;
}

/////////////////////////////////////
//// Low Level Drawing Functions ////
/////////////////////////////////////

void clear_screen(MatrixPanel_I2S_DMA *display)
{
    display->fillScreen(dma_display->color565(5, 5, 5));// avoid true black to mask ghosting
}

void drawLine(MatrixPanel_I2S_DMA *display, int x0, int y0, int x1, int y1, uint16_t color) {
    // Bresenham’s algorithm for line drawing
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
        display->drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/**
 * Render a frame to the LED matrix.
 *
 * This function clears the display buffer, calls the provided renderer
 * function to draw the frame contents, and then updates the display.
 * If double buffering is enabled, the back buffer is flipped to the
 * front buffer after drawing.
 *
 * Usage:
 *   render_display(draw_fft);
 *   render_display(draw_text);
 *
 * The renderer function must have the signature:
 *   void renderer(MatrixPanel_I2S_DMA *display);
 */
static void render_task(void* args)
{
    if (!dma_display) {
        vTaskDelete(NULL);
    }

    ESP_LOGW(TAG, "Notification required: %s", wait_for_render_notification ? "YES" : "NO");

    while (1)
    {
        // wait for trigger
        if (wait_for_render_notification) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10)); // small delay to avoid hogging CPU if no notification is needed
        }

        if (!display_renderer) continue;

        clear_screen(dma_display);

        display_renderer();

        if (DOUBLE_BUFF) {
            dma_display->flipDMABuffer();
        }
    }
}

void start_render_task() {
    if (render_task_handle) {
        ESP_LOGW(TAG, "Render task already running!");
        return;
    }
    xTaskCreate(render_task, "render_task", 4096, NULL, 5, &render_task_handle);
}

void set_renderer(render_func_t renderer) {
    display_renderer = renderer;
}


////////////////////////////
//// Renderer Functions ////
////////////////////////////

// simple test pattern renderer

void render_test_movingpixel()
{
    wait_for_render_notification = false; // this renderer doesn't need notification, it just runs continuously

    static int i = 0;
    static int j = 0;

    // Draw ONE pixel per call
    uint16_t color = dma_display->color565(255,255,255 );

    dma_display->drawPixel(j, i, color);

    // Move to next pixel
    j++;
    if (j >= DISPLAY_WIDTH) {
        j = 0;
        i++;
    }

    // Stop when done
    if (i >= DISPLAY_HEIGHT) {
        i = 0;
        j = 0;
        ESP_LOGW(TAG, "Finished render_test pattern");
    }


}

void render_test_horizontal_bars()
{
    wait_for_render_notification = false; // this renderer doesn't need notification, it just runs continuously

    uint16_t color = dma_display->color565(0, 255, 0);

    for (int x = 0; x < DISPLAY_WIDTH / 2; x+=2) {
        dma_display->drawPixel(x, 0, color);
    }
    for (int x = 1; x < DISPLAY_WIDTH; x+=2) {
        dma_display->drawPixel(x, 15, color);
    }

}

void render_test_spectrum()
{
    wait_for_render_notification = true; // this renderer will wait for a notification before running

    const int NUM_BANDS = 32;

    // --- persistent state ---
    static float raw[NUM_BANDS] = {0};
    static float smooth[NUM_BANDS] = {0};
    static float peak[NUM_BANDS] = {0};

    // tuning
    const float alpha = 0.5f;        // smoothing (higher = slower)
    const float decay = 0.2f;        // peak decay per frame (pixels/sec-ish)
    const float max_val = 1.0f;      // normalized max input

    // --- generate fake input ---
    for (int b = 0; b < NUM_BANDS; b++) {
        // semi-random but smooth-ish
        float noise = (float)(esp_random() % 1000) / 1000.0f;

        // add some structure (fake spectrum shape)
        float shape = 1.0f - ((float)b / NUM_BANDS); // higher on left
        raw[b] = 0.5f * noise + 0.5f * shape;
    }

    int bar_width = DISPLAY_WIDTH / NUM_BANDS;

    for (int b = 0; b < NUM_BANDS; b++) {

        // --- smoothing ---
        smooth[b] = alpha * smooth[b] + (1.0f - alpha) * raw[b];

        // --- convert to height ---
        int height = (int)(smooth[b] * DISPLAY_HEIGHT);
        if (height > DISPLAY_HEIGHT) height = DISPLAY_HEIGHT;
        if (height < 0) height = 0;

        // --- peak hold ---
        if (height > peak[b]) {
            peak[b] = height;
        } else {
            peak[b] -= decay;
            if (peak[b] < 0) peak[b] = 0;
        }

        // --- draw bar ---
        for (int x = b * bar_width; x < (b + 1) * bar_width; x++) {
            for (int y = 0; y < height; y++) {
                int draw_y = DISPLAY_HEIGHT - 1 - y;

                uint16_t color = dma_display->color565(0, 255, 0);
                dma_display->drawPixel(x, draw_y, color);
            }
        }

        // --- draw peak ---
        int peak_y = DISPLAY_HEIGHT - 1 - (int)peak[b];
        if (peak_y >= 0 && peak_y < DISPLAY_HEIGHT) {
            for (int x = b * bar_width; x < (b + 1) * bar_width; x++) {
                uint16_t color = dma_display->color565(255, 0, 0);
                dma_display->drawPixel(x, peak_y, color);
            }
        }
    }
}

float db_to_height(float db)
{
    const float min_db = -75.0f;

    db += 75.0f;   // shift range to 0..75

    if (db < 0) db = 0;
    if (db > 75) db = 75;

    float norm = db / 75.0f;

    // perceptual boost
    //norm = norm * norm;

    return (int)(norm * 15.0f);
}
void render_spectrum_simple()
{
    wait_for_render_notification = true; // this renderer will wait for a notification before running

    const int NUM_BANDS = num_bars;

    spectrum_frame_t frame;

    // get latest FFT frame
    xQueueReceive(display_queue, &frame, 0);

    int bar_width = DISPLAY_WIDTH / NUM_BANDS;

    //ESP_LOGI(TAG, "Band 0: input=%.2f dB, height=%f", frame.bands[0], (float)db_to_height(frame.bands[0]));

    for (int b = 0; b < NUM_BANDS; b++)
    {
        float input = frame.bands[b];

        int height = db_to_height(input);

        int x0 = b * bar_width;
        int x1 = x0 + bar_width;

        // draw bar
        for (int x = x0; x < x1; x++)
        {
            for (int y = 0; y < height; y++)
            {
                int draw_y = DISPLAY_HEIGHT - 1 - y;

                dma_display->drawPixel(
                    x,
                    draw_y,
                    dma_display->color565(0, 255, 0)
                );
            }
        }
    }
}

void render_spectrum()
{
    wait_for_render_notification = true; // this renderer will wait for a notification before running

    const int NUM_BANDS = num_bars;

    // --- persistent state ---
    static float smooth[32] = {0};   // use max possible or MAX_BANDS
    static float peak[32] = {0};

    spectrum_frame_t frame;

    // --- get latest FFT frame (non-blocking) ---
    xQueueReceive(display_queue, &frame, 0);

    // tuning
    const float alpha_p = 0.5f;   // smoothing
    const float decay = 0.3f;   // peak fall speed

    int bar_width = DISPLAY_WIDTH / NUM_BANDS;

    for (int b = 0; b < NUM_BANDS; b++)
    {
        // --- input from FFT (NO more fake noise) ---
        float input = frame.bands[b];

        // --- optional safety clamp ---
        if (input < 0) input = 0;
        if (input > 1.0f) input = 1.0f;

        // --- smoothing ---
        float alpha = alpha_p; //+ (b / (float)NUM_BANDS) * 0.2f;; // faster for higher f
        smooth[b] = alpha * smooth[b] + (1.0f - alpha) * input;

        // --- convert to height ---
        int height = (int)(smooth[b] * DISPLAY_HEIGHT);
        if (height > DISPLAY_HEIGHT) height = DISPLAY_HEIGHT;
        if (height < 0) height = 0;

        // --- peak hold ---
        if (height > peak[b]) {
            peak[b] = height;
        } else {
            peak[b] -= decay;
            if (peak[b] < 0) peak[b] = 0;
        }

        int x0 = b * bar_width;
        int x1 = x0 + bar_width;

        // --- draw bar ---
        for (int x = x0; x < x1; x++)
        {
            for (int y = 0; y < height; y++)
            {
                int draw_y = DISPLAY_HEIGHT - 1 - y;
                dma_display->drawPixel(
                    x,
                    draw_y,
                    dma_display->color565(0, 255, 0)
                );
            }
        }

        // --- draw peak ---
        int peak_y = DISPLAY_HEIGHT - 1 - (int)peak[b];

        if (peak_y >= 0 && peak_y < DISPLAY_HEIGHT)
        {
            for (int x = x0; x < x1; x++)
            {
                dma_display->drawPixel(
                    x,
                    peak_y,
                    dma_display->color565(255, 0, 0)
                );
            }
        }
    }
}


//void render_test() {
 //   dma_display->drawPixel(0, 0, dma_display->color565(200, 92, 255));
//    dma_display->drawPixel(31, 15, dma_display->color565(200, 92, 255));
 //   dma_display->drawPixel(30, 14, dma_display->color565(0, 92, 255));
//}

// renderer for FFT Bars

// renderer for waveform
void render_waveform()
{
    if (!dma_display) return;

    uint16_t color = dma_display->color565(0, 255, 0);

    for (int i = 1; i < DISPLAY_WIDTH; i++) {
        int y0 = (int)smoothedWaveform[i - 1];
        int y1 = (int)smoothedWaveform[i];

        drawLine(dma_display,i - 1, y0, i, y1, color);
    }
}

// renderer for text
void render_text()
{
    if (!dma_display) return;

    const char *message = "Hello, ESP32!";
    int x = 0;
    int y = 0;
    uint16_t color = dma_display->color565(255, 255, 0);

}

void compute_waveform(int32_t *samples, int sample_count)
{
    int step = sample_count / DISPLAY_WIDTH;
    const float alpha = 0.4f;

    for (int i = 0; i < DISPLAY_WIDTH; i++) {
        int idx = i * step;

        if (idx < sample_count) {
            // optional clamp (safety)
            //float normalized = samples[idx];
            //if (normalized > 1.0f) normalized = 1.0f;
            //if (normalized < -1.0f) normalized = -1.0f;

            // center waveform (oscilloscope style)
            int mid = DISPLAY_HEIGHT / 2;
            int y = mid - (int)(samples[idx] * (DISPLAY_HEIGHT / 2));

            if (y < 0) y = 0;
            if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;

            // smoothing
            smoothedWaveform[i] =
                alpha * smoothedWaveform[i] +
                (1.0f - alpha) * y;
        }
    }
}



////// Helper Functions //////
/**
 * @brief Converts a color from HSV (Hue, Saturation, Value) to RGB.
 *
 * @param h Hue angle in degrees [0, 360). Represents the color type.
 * @param s Saturation [0, 255]. 0 = grayscale, 255 = fully saturated color.
 * @param v Value (brightness) [0, 255]. 0 = black, 255 = full brightness.
 * @param r Output red component [0, 255].
 * @param g Output green component [0, 255].
 * @param b Output blue component [0, 255].
 *
 * @note The function uses floating-point math internally and writes results
 *       into the provided reference parameters.
 */
void hsvToRgb(int h, int s, int v, int &r, int &g, int &b) {
    h = h % 360;

    float hf = h / 60.0f;
    int i = (int)hf;
    float f = hf - i;

    float sf = s / 255.0f;
    float vf = v;

    float p = vf * (1 - sf);
    float q = vf * (1 - f * sf);
    float t = vf * (1 - (1 - f) * sf);

    switch (i) {
        case 0: r = vf; g = t;  b = p;  break;
        case 1: r = q;  g = vf; b = p;  break;
        case 2: r = p;  g = vf; b = t;  break;
        case 3: r = p;  g = q;  b = vf; break;
        case 4: r = t;  g = p;  b = vf; break;
        case 5: r = vf; g = p;  b = q;  break;
        default: r = g = b = 0; break;
    }
}

//////////////////

// Draw rectangle (outline)
void drawRect(MatrixPanel_I2S_DMA *d, int x, int y, int w, int h, uint16_t color) {
    drawLine(d, x, y, x + w - 1, y, color);
    drawLine(d, x, y, x, y + h - 1, color);
    drawLine(d, x + w - 1, y, x + w - 1, y + h - 1, color);
    drawLine(d, x, y + h - 1, x + w - 1, y + h - 1, color);
}

// Draw filled rectangle
void fillRect(MatrixPanel_I2S_DMA *d, int x, int y, int w, int h, uint16_t color) {
    for (int i = 0; i < h; i++) {
        drawLine(d, x, y + i, x + w - 1, y + i, color);
    }
}

// Draw circle outline (Midpoint algorithm)
void drawCircle(MatrixPanel_I2S_DMA *d, int x0, int y0, int r, uint16_t color) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    d->drawPixel(x0, y0 + r, color);
    d->drawPixel(x0, y0 - r, color);
    d->drawPixel(x0 + r, y0, color);
    d->drawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        d->drawPixel(x0 + x, y0 + y, color);
        d->drawPixel(x0 - x, y0 + y, color);
        d->drawPixel(x0 + x, y0 - y, color);
        d->drawPixel(x0 - x, y0 - y, color);
        d->drawPixel(x0 + y, y0 + x, color);
        d->drawPixel(x0 - y, y0 + x, color);
        d->drawPixel(x0 + y, y0 - x, color);
        d->drawPixel(x0 - y, y0 - x, color);
    }
}


// Helper to access glyph row
inline uint8_t getRowChar(const Font& f, int index, int row) {
    return f.data[index * f.height + row];
}

void drawChar(MatrixPanel_I2S_DMA *d, int x, int y, char c, const Font& f, uint16_t color) {
    if (c < 0x20 || c > 0x7F) return;

    int index = c - 0x20;

    for (int row = 0; row < f.height; ++row) {
        uint8_t bits = getRowChar(f, index, row);

        uint8_t mask = 1 << (f.width - 1);
        int runStart = -1;

        for (int col = 0; col < f.width; ++col) {
            bool on = bits & mask;

            if (on && runStart == -1) {
                runStart = col;
            }

            // End of run OR last pixel
            if ((!on || col == f.width - 1) && runStart != -1) {
                int end = on ? col : col - 1;

                d->drawFastHLine(
                    x + runStart,
                    y + row,
                    end - runStart + 1,
                    color
                );

                runStart = -1;
            }

            mask >>= 1;
        }
    }
}

/*

void drawChar(MatrixPanel_I2S_DMA *d, int x, int y, char c, char font_size, uint16_t color) {
    if (c < 0x20 || c > 0x7F) return;
    int index = c - 0x20;

    if (font_size == 'S') {                      // 4x6
        const uint8_t (*table)[6] = font4x6;
        for (int row = 0; row < 6; ++row) {
            uint8_t bits = table[index][row];
            for (int col = 0; col < 4; ++col) {
                if ((bits >> (2 - col)) & 1) {   // leftmost bit is MSB
                    d->drawPixel(x + col, y + row, color);
                }
            }
        }
    } else if (font_size == 'M') {               // 5x7
        const uint8_t (*table)[7] = font5x7;
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = table[index][row];
            for (int col = 0; col < 5; ++col) {
                if ((bits >> (4 - col)) & 1) {
                    d->drawPixel(x + col, y + row, color);
                }
            }
        }
    } else if (font_size == 'L') {               // 6x12
        const uint8_t (*table)[12] = font6x12;
        // NOTE: font6x12 might have 95 entries (ASCII 32..126) so check its size
        for (int row = 0; row < 12; ++row) {
            uint8_t bits = table[index][row];
            for (int col = 0; col < 6; ++col) {
                if ((bits >> (5 - col)) & 1) {
                    d->drawPixel(x + col, y + row, color);
                }
            }
        }
    } else if (font_size == 'X') {               // 6x12
        const uint8_t (*table)[16] = font8x16;
        // NOTE: font6x12 might have 95 entries (ASCII 32..126) so check its size
        for (int row = 0; row < 16; ++row) {
            uint8_t bits = table[index][row];
            for (int col = 0; col < 8; ++col) {
                if ((bits >> (7 - col)) & 1) {
                    d->drawPixel(x + col, y + row, color);
                }
            }
        }
    }
}



void drawText(MatrixPanel_I2S_DMA *d, int x, int y, const char *text, char font_size, uint16_t color) {
    while (*text) {
        drawChar(d, x, y, *text, font_size, color);
        if (font_size == 'S'){
            x += 4; // wide+spacing
        } else if (font_size == 'M'){
            x += 6; // wide+spacing
        } else if (font_size == 'L'){
            x += 6; // wide+spacing
        } else if (font_size == 'X'){
            x += 8; // wide+spacing
        }
        
        text++;
    }
}

void scrollText(MatrixPanel_I2S_DMA *d, int x, int y, const char *text, char font_size, uint16_t color, int delay_ms) {
    int textOffset = 0;
    int textPixels = 0;

    // determine text width in pixels
    if (font_size == 'S')
        textPixels = (4) * strlen(text);
    else if (font_size == 'M')
        textPixels = (5 + 1) * strlen(text);
    else if (font_size == 'L')
        textPixels = (6) * strlen(text);
    else if (font_size == 'X')
        textPixels = (8) * strlen(text);

    // initial draw
    d->clearScreen();
    drawText(d, x, y, text, font_size, color);
    d->flipDMABuffer();  // show initial frame
    vTaskDelay(pdMS_TO_TICKS(500));

    // scroll animation
    for (int i = 0; i < textPixels; i++) {
        d->clearScreen(); //  clear *back buffer* only
        drawText(d, textOffset + x, y, text, font_size, color);
        d->flipDMABuffer();                      // ✅ swap buffers cleanly
        textOffset--;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

*/
