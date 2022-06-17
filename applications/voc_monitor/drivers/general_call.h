#pragma once

#include <furi_hal_i2c.h>

#define GENERAL_CALL_I2C_ADDRESS 0x00

/**
 * @brief Use the General Call to perform a reset on the I2C bus
 * 
 * All sensors on the bus that support this I2C specification will reset according to their individual implementations.
 * 
 * @param       handle pointer to FuriHalI2cBusHandle instance
 * @return      true if reset was communicated to the bus successfully; false otherwise
 */
bool general_call_reset(FuriHalI2cBusHandle* handle);