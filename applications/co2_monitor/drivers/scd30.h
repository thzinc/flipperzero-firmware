#pragma once

#include <furi_hal_i2c.h>

#define SCD30_I2C_ADDRESS (0x61 << 1)

/**
 * @brief Initialize Sensiron SCD30 carbon dioxide (CO2) sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @return      true if device is present and ready; false otherwise
 */
bool scd30_init(FuriHalI2cBusHandle* handle);

/**
 * @brief Get a temperature, relative humidity, and CO2 concentration measurement from the sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @param       ready true if measurement is ready to use; false otherwise
 * @param       temp_c temperature in degrees Celsius (valid range from -40 to 85)
 * @param       rh_pct relative humidity in percentage (valid range 0.0 to 1.0)
 * @param       co2_ppm carbon dioxide (CO2) concentration in parts per million (mg/l) (valid range from 0 to 10000)
 * @return      true if measurement was triggered and read successfully; false otherwise
 */
bool scd30_get_measurement(
    FuriHalI2cBusHandle* handle,
    bool* ready,
    float* temp_c,
    float* rh_pct,
    float* co2_ppm);

/**
 * @brief Deinitialize the sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @return      true if sensor was deinitialized successfully; false otherwise
 */
bool scd30_deinit(FuriHalI2cBusHandle* handle);