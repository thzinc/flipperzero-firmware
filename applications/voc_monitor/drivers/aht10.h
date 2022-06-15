#pragma once

#include <furi_hal_i2c.h>

#define AHT10_I2C_ADDRESS (0x38 << 1)

/**
 * @brief Initialize Aosong ASAIR AHT10/AHT20 temperature and humidity sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @return      true if device is present, ready, and calibrated; false otherwise
 */
bool aht10_init(FuriHalI2cBusHandle* handle);

/**
 * @brief Get a temperature and relative humidity measurement from the sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @param       temp_c temperature in degrees Celsius (valid range from -40 to 85)
 * @param       rh_pct relative humidity in percentage (valid range 0.0 to 1.0)
 * @return      true if measurement was triggered and read successfully; false otherwise
 */
bool aht10_get_measurement(FuriHalI2cBusHandle* handle, float* temp_c, float* rh_pct);