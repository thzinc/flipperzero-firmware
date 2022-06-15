#include "sgp30.h"
#include <furi.h>
#include <furi_hal.h>

#define SGP30_TAG "SGP30"
#define SGP30_I2C_TIMEOUT_TICKS 50
#define SGP30_INITIALIZATION_DURATION_MS 15000
#define SGP30_CMD_INIT_AIR_QUALITY \
    { 0x20, 0x03 }
#define SGP30_CMD_INIT_AIR_QUALITY_TIMEOUT_MS 10
#define SGP30_CMD_MEASURE_AIR_QUALITY \
    { 0x20, 0x08 }
#define SGP30_CMD_MEASURE_AIR_QUALITY_TIMEOUT_MS 12
#define SGP30_CMD_SET_HUMIDITY(...) \
    { 0x20, 0x61, ##__VA_ARGS__ }
#define SGP30_CMD_SET_HUMIDITY_TIMEOUT_MS 10

bool sgp30_init_air_quality(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SGP30_CMD_INIT_AIR_QUALITY;
    return furi_hal_i2c_tx(handle, SGP30_I2C_ADDRESS, cmd, sizeof(cmd), SGP30_I2C_TIMEOUT_TICKS);
}

bool sgp30_measure_air_quality(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = SGP30_CMD_MEASURE_AIR_QUALITY;
    return furi_hal_i2c_tx(handle, SGP30_I2C_ADDRESS, cmd, sizeof(cmd), SGP30_I2C_TIMEOUT_TICKS);
}

bool sgp30_read_air_quality(
    FuriHalI2cBusHandle* handle,
    uint16_t* tvoc_ppb,
    uint8_t* tvoc_crc,
    uint16_t* eco2_ppm,
    uint8_t* eco2_crc) {
    uint8_t data[6] = {};
    bool success =
        furi_hal_i2c_rx(handle, SGP30_I2C_ADDRESS, data, sizeof(data), SGP30_I2C_TIMEOUT_TICKS);
    if(!success) {
        return false;
    }

    *eco2_ppm = data[0] << 8 | data[1];
    *eco2_crc = data[2];
    *tvoc_ppb = data[3] << 8 | data[4];
    *tvoc_crc = data[5];
    return true;
}

bool sgp30_init(FuriHalI2cBusHandle* handle, uint32_t* initializedAfterTicks) {
    bool success = furi_hal_i2c_is_device_ready(handle, SGP30_I2C_ADDRESS, 2);
    if(!success) {
        FURI_LOG_E(SGP30_TAG, "failed to find sensor at address %x (8-bit)", SGP30_I2C_ADDRESS);
        return false;
    }

    success = sgp30_init_air_quality(handle);
    if(!success) {
        FURI_LOG_E(SGP30_TAG, "failed to initialize sensor");
        return false;
    }

    furi_hal_delay_ms(SGP30_CMD_INIT_AIR_QUALITY_TIMEOUT_MS);

    *initializedAfterTicks =
        furi_hal_get_tick() + furi_hal_ms_to_ticks(SGP30_INITIALIZATION_DURATION_MS);

    return true;
}

bool sgp30_get_measurement(FuriHalI2cBusHandle* handle, uint16_t* tvoc_ppb, uint16_t* eco2_ppm) {
    bool success = sgp30_measure_air_quality(handle);
    if(!success) {
        FURI_LOG_E(SGP30_TAG, "failed to measure air quality");
        return false;
    }

    furi_hal_delay_ms(SGP30_CMD_MEASURE_AIR_QUALITY_TIMEOUT_MS);

    uint8_t tvoc_crc = 0;
    uint8_t eco2_crc = 0;
    success = sgp30_read_air_quality(handle, tvoc_ppb, &tvoc_crc, eco2_ppm, &eco2_crc);
    if(!success) {
        FURI_LOG_E(SGP30_TAG, "failed to read air quality");
        return false;
    }

    // HACK skipping CRC validation

    return true;
}

bool sgp30_set_humidity(FuriHalI2cBusHandle* handle, float ah_gm3) {
    uint16_t fp = (uint16_t)floor(ah_gm3 * 256);
    uint8_t cmd[] = SGP30_CMD_SET_HUMIDITY(fp >> 8, fp & 0xFF);
    bool success =
        furi_hal_i2c_tx(handle, SGP30_I2C_ADDRESS, cmd, sizeof(cmd), SGP30_I2C_TIMEOUT_TICKS);
    if(!success) {
        FURI_LOG_E(
            SGP30_TAG, "failed to set humidity %f g/㎥ (%x fixed point)", (double)ah_gm3, fp);
        return false;
    }

    return true;
}