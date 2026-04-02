#ifndef I2S_READER_H
#define I2S_READER_H
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t i2s_task_handle;

extern volatile bool i2s_data_ready;
extern volatile bool i2s_task_running;
extern volatile bool i2s_enabled;

void i2s_setup(void);
void start_i2s_read_task(void);
void stop_i2s_read_task(void);

#endif