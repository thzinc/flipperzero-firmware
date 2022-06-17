#include "general_call.h"
#include <furi.h>
#include <furi_hal.h>

#define GENERAL_CALL_TAG "General Call"
#define GENERAL_CALL_I2C_TIMEOUT_TICKS 50
#define GENERAL_CALL_CMD_RESET \
    { 0x06 }

bool general_call_reset(FuriHalI2cBusHandle* handle) {
    uint8_t cmd[] = GENERAL_CALL_CMD_RESET;
    return furi_hal_i2c_tx(
        handle, GENERAL_CALL_I2C_ADDRESS, cmd, sizeof(cmd), GENERAL_CALL_I2C_TIMEOUT_TICKS);
}
