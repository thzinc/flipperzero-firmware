#include "aht10.h"
#include <furi_hal.h>

#define AHT10_TAG "AHT10"
#define AHT10_I2C_TIMEOUT_TICKS 50
#define AHT10_CMD_INITIALIZE_TIMEOUT_MS 20
#define AHT10_CMD_STATUS_TIMEOUT_MS 10

#define AHT10_CMD_INITIALIZE 0b11100001
#define AHT10_CMD_TRIGGER_MEASUREMENT 0b10101100
#define AHT10_CMD_SOFT_RESET 0b10111010

#define AHT10_EMPTY_STATUS 0b00000000
#define AHT10_STATUS_MASK_BUSY 0b10000000
#define AHT10_STATUS_MASK_CALIBRATED 0b00001000

bool aht10_status(FuriHalI2cBusHandle* handle, uint8_t* status) {
    return furi_hal_i2c_rx(handle, AHT10_I2C_ADDRESS, status, 1, AHT10_I2C_TIMEOUT_TICKS);
}

bool aht10_initialize(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = {AHT10_CMD_INITIALIZE, 0x08, 0x00};
    return furi_hal_i2c_tx(handle, AHT10_I2C_ADDRESS, cmd, 3, AHT10_I2C_TIMEOUT_TICKS);
}

bool aht10_trigger(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = {AHT10_CMD_TRIGGER_MEASUREMENT, 0x33, 0x00};
    return furi_hal_i2c_tx(handle, AHT10_I2C_ADDRESS, cmd, 3, AHT10_I2C_TIMEOUT_TICKS);
}

bool aht10_read_measurement(FuriHalI2cBusHandle* handle, float* temp_c, float* rh_pct) {
    /*
     * buf index 0       1       2       3       4       5
     *           |-------|-------|-------|-------|-------|-------
     * category  SSSSSSSSHHHHHHHHHHHHHHHHHHHHTTTTTTTTTTTTTTTTTTTT
     *
     * Categories:
     * S: State (8 bits)
     * H: Humidity (20 bits)
     * T: Temperature (20 bits)
     */
    uint8_t buf[6] = {0, 0, 0, 0, 0, 0};
    bool success = furi_hal_i2c_rx(handle, AHT10_I2C_ADDRESS, buf, 6, AHT10_I2C_TIMEOUT_TICKS);

    uint32_t rawHumidityReading = buf[1] << 12 | buf[2] << 4 | buf[3] >> 4;
    *rh_pct = rawHumidityReading * 1.0 / 0x100000;

    uint32_t rawTemperatureReading = (buf[3] & 0xF) << 16 | buf[4] << 8 | buf[5];
    *temp_c = (rawTemperatureReading * 200.0 / 0x100000) - 50;

    return success;
}

bool aht10_reset(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = {AHT10_CMD_SOFT_RESET};
    return furi_hal_i2c_tx(handle, AHT10_I2C_ADDRESS, cmd, 1, AHT10_I2C_TIMEOUT_TICKS);
}

bool aht10_init(FuriHalI2cBusHandle* handle) {
    bool success = furi_hal_i2c_is_device_ready(handle, AHT10_I2C_ADDRESS, 2);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to find sensor at address %x (8-bit)", AHT10_I2C_ADDRESS);
        return false;
    }

    success = aht10_reset(handle);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to reset sensor");
        return false;
    }

    furi_hal_delay_ms(AHT10_CMD_INITIALIZE_TIMEOUT_MS);

    success = aht10_initialize(handle);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to initialize sensor");
        return false;
    }

    uint8_t status = AHT10_EMPTY_STATUS;
    bool is_busy = false;
    bool is_calibrated = false;
    do {
        if(is_busy) {
            furi_hal_delay_ms(AHT10_CMD_STATUS_TIMEOUT_MS);
        }

        success = aht10_status(handle, &status);
        if(!success) {
            FURI_LOG_E(AHT10_TAG, "failed while waiting for calibration");
            return false;
        }

        is_busy = (status & AHT10_STATUS_MASK_BUSY) > 0;
        is_calibrated = (status & AHT10_STATUS_MASK_CALIBRATED) > 0;
    } while(is_busy);

    if(!is_calibrated) {
        FURI_LOG_E(AHT10_TAG, "failed to calibrate sensor");
        return false;
    }

    return true;
}

bool aht10_get_measurement(FuriHalI2cBusHandle* handle, float* temp_c, float* rh_pct) {
    bool success = aht10_trigger(handle);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to trigger measurement");
        return false;
    }

    uint8_t status = AHT10_EMPTY_STATUS;
    bool is_busy = false;
    do {
        if(is_busy) {
            furi_hal_delay_ms(AHT10_CMD_STATUS_TIMEOUT_MS);
        }

        success = aht10_status(handle, &status);
        if(!success) {
            FURI_LOG_E(AHT10_TAG, "failed while waiting for measurement");
            return false;
        }

        is_busy = (status & AHT10_STATUS_MASK_BUSY) > 0;
    } while(is_busy);

    success = aht10_read_measurement(handle, temp_c, rh_pct);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to read measurement");
        return false;
    }

    return true;
}

bool aht10_deinit(FuriHalI2cBusHandle* handle) {
    bool success = aht10_reset(handle);
    if(!success) {
        FURI_LOG_E(AHT10_TAG, "failed to reset sensor");
        return false;
    }

    return true;
}