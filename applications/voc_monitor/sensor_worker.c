#include <furi.h>
#include <furi_hal.h>
#include <math.h>
#include "sensor_worker.h"
#include "drivers/aht10.h"
#include "drivers/sgp30.h"
#include "drivers/general_call.h"

#define TAG "SensorWorker"
#define RETRY_TIMEOUT_MS 1000
#define MEASUREMENT_INTERVAL_MS 250

// Adapted from https://github.com/skgrange/threadr/blob/fd42380883133fe7a47c479e778afe644a507334/R/absolute_humidity.R
#define RH_TO_AH(temp_c, rh_pct)                                                   \
    ((6.112 * expf((17.67 * temp_c) / (temp_c + 243.5)) * (rh_pct)*100 * 2.1674) / \
     (273.15 + temp_c))

struct SensorWorker {
    FuriThread* thread;
    bool is_started;

    SensorWorkerCallback callback;
    void* callback_context;
};

static int32_t worker_thread(void* context) {
    furi_assert(context);
    SensorWorker* instance = context;

    bool success = true;
    float temp_c = 0;
    float rh_pct = 0;
    uint32_t initializedAfterTicks = 0;
    bool initializing = true;
    uint16_t tvoc_ppb = 0;
    uint16_t eco2_ppm = 0;
    while(instance->is_started) {
        if(!success) {
            FURI_LOG_I(
                TAG,
                "last operation failed; waiting %d ms before reinitializing",
                RETRY_TIMEOUT_MS);
            instance->callback(
                success,
                temp_c,
                rh_pct,
                initializing,
                tvoc_ppb,
                eco2_ppm,
                instance->callback_context);
            furi_hal_delay_ms(RETRY_TIMEOUT_MS);
        }

        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
        success = general_call_reset(&furi_hal_i2c_handle_external) &&
                  aht10_init(&furi_hal_i2c_handle_external) &&
                  sgp30_init(&furi_hal_i2c_handle_external, &initializedAfterTicks);
        furi_hal_i2c_release(&furi_hal_i2c_handle_external);

        if(!success) {
            continue;
        }

        while(instance->is_started) {
            initializing = furi_hal_get_tick() < initializedAfterTicks;

            furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

            while(1) {
                success = aht10_get_measurement(&furi_hal_i2c_handle_external, &temp_c, &rh_pct);
                if(!success) {
                    break;
                }

                success =
                    sgp30_set_humidity(&furi_hal_i2c_handle_external, RH_TO_AH(temp_c, rh_pct));
                if(!success) {
                    break;
                }

                success =
                    sgp30_get_measurement(&furi_hal_i2c_handle_external, &tvoc_ppb, &eco2_ppm);
                break;
            }

            furi_hal_i2c_release(&furi_hal_i2c_handle_external);

            if(!success) {
                break;
            }

            instance->callback(
                success,
                temp_c,
                rh_pct,
                initializing,
                tvoc_ppb,
                eco2_ppm,
                instance->callback_context);
            furi_hal_delay_ms(MEASUREMENT_INTERVAL_MS);
        }
    }

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    success = aht10_deinit(&furi_hal_i2c_handle_external) &&
              general_call_reset(&furi_hal_i2c_handle_external);
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    if(!success) {
        FURI_LOG_W(TAG, "failed to deinitialize sensors; may draw unnecessary power");
    }

    return 0;
}

SensorWorker* sensor_worker_alloc() {
    SensorWorker* instance = malloc(sizeof(SensorWorker));

    instance->thread = furi_thread_alloc();
    furi_thread_set_name(instance->thread, "SensorWorker");
    furi_thread_set_stack_size(instance->thread, 2048);
    furi_thread_set_context(instance->thread, instance);
    furi_thread_set_callback(instance->thread, worker_thread);

    instance->is_started = false;

    return instance;
}

void sensor_worker_set_callback(
    SensorWorker* instance,
    SensorWorkerCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->callback_context = context;
}

void sensor_worker_free(SensorWorker* instance) {
    furi_assert(instance);
    furi_thread_free(instance->thread);
    free(instance);
}

void sensor_worker_start(SensorWorker* instance) {
    furi_assert(instance);
    furi_assert(instance->is_started == false);

    instance->is_started = true;
    furi_thread_start(instance->thread);
}

void sensor_worker_stop(SensorWorker* instance) {
    furi_assert(instance);
    furi_assert(instance->is_started == true);

    instance->is_started = false;
    furi_thread_join(instance->thread);
}