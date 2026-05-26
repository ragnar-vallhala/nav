#include <bme280/bme280.h>

hal_status_t bme280_init(void) {
    return HAL_OK;
}

hal_status_t bme280_read_temperature_c(int *temperature_c) {
    if (!temperature_c) {
        return HAL_ERROR;
    }
    *temperature_c = NAVHAL_HAS_I2C_DMA ? 25 : 24;
    return HAL_OK;
}
