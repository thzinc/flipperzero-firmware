#include "scd30.h"
#include <furi.h>
#include <furi_hal.h>

#define SCD30_TAG "SCD30"
#define SCD30_I2C_TIMEOUT_TICKS 50

#define WORDS_TO_BYTES(words) (words * 3)
#define SCD30_GET_WORD(buf, word_index) \
    ((buf[WORDS_TO_BYTES(word_index) + 0] << 8) | buf[WORDS_TO_BYTES(word_index) + 1])
#define SCD30_GET_CRC(buf, word_index) buf[WORDS_TO_BYTES(word_index) + 2]
#define SCD30_GET_UINT32(buf, word_index) \
    ((SCD30_GET_WORD(buf, word_index) << 16) | SCD30_GET_WORD(buf, word_index + 1))

#define SCD30_CMD_TRIGGER_CONTINUOUS_MEASUREMENT \
    { 0x00, 0x10, 0x00, 0x00, 0x81 }
#define SCD30_CMD_STOP_CONTINUOUS_MEASUREMENT \
    { 0x01, 0x04 }
#define SCD30_CMD_SOFT_RESET \
    { 0xD3, 0x04 }
#define SCD30_CMD_SET_MEASUREMENT_INTERVAL_TO_MIN \
    { 0x46, 0x00, 0x00, 0x02, 0xE3 }
#define SCD30_CMD_GET_DATA_READY_STATUS \
    { 0x02, 0x02 }
#define SCD30_CMD_READ_MEASUREMENT \
    { 0x03, 0x00 }

bool scd30_trigger_continuous_measurement(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SCD30_CMD_TRIGGER_CONTINUOUS_MEASUREMENT;
    return furi_hal_i2c_tx(handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), SCD30_I2C_TIMEOUT_TICKS);
}

bool scd30_soft_reset(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SCD30_CMD_SOFT_RESET;
    return furi_hal_i2c_tx(handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), SCD30_I2C_TIMEOUT_TICKS);
}

bool scd30_stop_continuous_measurement(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SCD30_CMD_STOP_CONTINUOUS_MEASUREMENT;
    return furi_hal_i2c_tx(handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), SCD30_I2C_TIMEOUT_TICKS);
}

bool scd30_set_measurement_interval(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SCD30_CMD_SET_MEASUREMENT_INTERVAL_TO_MIN;
    return furi_hal_i2c_tx(handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), SCD30_I2C_TIMEOUT_TICKS);
}

bool scd30_get_data_ready_status(FuriHalI2cBusHandle* handle, bool* ready) {
    uint8_t cmd[] = SCD30_CMD_GET_DATA_READY_STATUS;
    bool success =
        furi_hal_i2c_tx(handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), SCD30_I2C_TIMEOUT_TICKS);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to request data ready status");
        return false;
    }

    uint8_t data[WORDS_TO_BYTES(1)] = {};
    success =
        furi_hal_i2c_rx(handle, SCD30_I2C_ADDRESS, data, sizeof(data), SCD30_I2C_TIMEOUT_TICKS);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to read data ready status");
        return false;
    }

    *ready = (SCD30_GET_WORD(data, 0) == 1) && (SCD30_GET_CRC(data, 0) == 0xB0);

    return true;
}

bool scd30_read_measurement(
    FuriHalI2cBusHandle* handle,
    float* temp_c,
    float* rh_pct,
    float* co2_ppm) {
    uint8_t cmd[] = SCD30_CMD_READ_MEASUREMENT;
    uint8_t data[WORDS_TO_BYTES(6)] = {};
    bool success = furi_hal_i2c_trx(
        handle, SCD30_I2C_ADDRESS, cmd, sizeof(cmd), data, sizeof(data), SCD30_I2C_TIMEOUT_TICKS);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to read measurement");
        return false;
    }

    // HACK skipping CRC validation
    uint32_t co2_ppm_raw = SCD30_GET_UINT32(data, 0);
    uint32_t temp_c_raw = SCD30_GET_UINT32(data, 2);
    uint32_t rh_pct_raw = SCD30_GET_UINT32(data, 4);

    memcpy(temp_c, &temp_c_raw, sizeof(temp_c_raw));
    memcpy(rh_pct, &rh_pct_raw, sizeof(rh_pct_raw));
    *rh_pct /= 100;
    memcpy(co2_ppm, &co2_ppm_raw, sizeof(co2_ppm_raw));

    return true;
}

bool scd30_init(FuriHalI2cBusHandle* handle) {
    bool success = scd30_trigger_continuous_measurement(handle);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to trigger continuous measurement");
        return false;
    }

    success = scd30_set_measurement_interval(handle);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to set measurement interval");
        return false;
    }

    return true;
}

bool scd30_get_measurement(
    FuriHalI2cBusHandle* handle,
    bool* ready,
    float* temp_c,
    float* rh_pct,
    float* co2_ppm) {
    bool success = scd30_get_data_ready_status(handle, ready);
    if(!success) {
        FURI_LOG_E(SCD30_TAG, "failed to get data ready status");
        return false;
    }

    if(!ready) {
        return true;
    }

    return scd30_read_measurement(handle, temp_c, rh_pct, co2_ppm);
}

bool scd30_deinit(FuriHalI2cBusHandle* handle) {
    return scd30_soft_reset(handle) && scd30_stop_continuous_measurement(handle);
}