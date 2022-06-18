#include <furi.h>
#include <furi_hal.h>
#include <math.h>
#include "sensor_worker.h"
#include "drivers/scd30.h"

#define TAG "SensorWorker"
#define RETRY_TIMEOUT_MS 1000
#define MEASUREMENT_INTERVAL_MS 2000

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
    bool ready = false;
    float temp_c = 0;
    float rh_pct = 0;
    float co2_ppm = 0;
    while(instance->is_started) {
        if(!success) {
            FURI_LOG_I(
                TAG,
                "last operation failed; waiting %d ms before reinitializing",
                RETRY_TIMEOUT_MS);
            instance->callback(
                success, ready, temp_c, rh_pct, co2_ppm, instance->callback_context);
            furi_hal_delay_ms(RETRY_TIMEOUT_MS);
        }

        furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
        success = scd30_init(&furi_hal_i2c_handle_external);
        furi_hal_i2c_release(&furi_hal_i2c_handle_external);

        if(!success) {
            continue;
        }

        while(instance->is_started) {
            furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
            success = scd30_get_measurement(
                &furi_hal_i2c_handle_external, &ready, &temp_c, &rh_pct, &co2_ppm);
            furi_hal_i2c_release(&furi_hal_i2c_handle_external);

            if(!success) {
                break;
            }

            instance->callback(
                success, ready, temp_c, rh_pct, co2_ppm, instance->callback_context);
            furi_hal_delay_ms(MEASUREMENT_INTERVAL_MS);
        }
    }

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    success = scd30_deinit(&furi_hal_i2c_handle_external);
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    if(!success) {
        FURI_LOG_W(TAG, "failed to deinitialize sensor; may draw unnecessary power");
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