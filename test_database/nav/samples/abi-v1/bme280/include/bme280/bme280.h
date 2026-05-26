#pragma once

#include <hal.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t bme280_init(void);
hal_status_t bme280_read_temperature_c(int *temperature_c);

#ifdef __cplusplus
}
#endif
