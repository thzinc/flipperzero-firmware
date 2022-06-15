#pragma once

#include <furi_hal_i2c.h>
#include <furi_hal_rtc.h>

#define SGP30_I2C_ADDRESS (0x58 << 1)

/**
 * @brief Initialize Sensiron SGP30 volatile organic compound (VOC) sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @param       initializedAfterTicks tick counter after which the sensor is initialized (see furi_hal_get_tick for current tick counter)
 * @return      true if device is present and ready; false otherwise
 */
bool sgp30_init(FuriHalI2cBusHandle* handle, uint32_t* initializedAfterTicks);

/**
 * @brief Get a total VOC concentration and equivalent CO2 concentration measurement from the sensor
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @param       tvoc_ppb total volatile organic compound (VOC) concentration in parts per billion (µg/l)
 * @param       eco2_ppm equivalent carbon dioxide (CO2) concentration in parts per million (mg/l)
 * @return      true if measurement was triggered and read successfully; false otherwise
 */
bool sgp30_get_measurement(FuriHalI2cBusHandle* handle, uint16_t* tvoc_ppb, uint16_t* eco2_ppm);

/**
 * @brief Set the on-chip humidity compensation
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @param       ah_gm3 absolute humidity in grams per cubic meter (g/㎥)
 * @return      true if humidity was updated successfully; false otherwise
 */
bool sgp30_set_humidity(FuriHalI2cBusHandle* handle, float ah_gm3);