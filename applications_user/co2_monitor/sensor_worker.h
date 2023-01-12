#pragma once

typedef struct SensorWorker SensorWorker;

typedef void (*SensorWorkerCallback)(
    bool success,
    bool ready,
    float temp_c,
    float rh_pct,
    float co2_ppm,
    void* context);

SensorWorker* sensor_worker_alloc();
void sensor_worker_set_callback(
    SensorWorker* instance,
    SensorWorkerCallback callback,
    void* context);
void sensor_worker_free(SensorWorker* instance);
void sensor_worker_start(SensorWorker* instance);
void sensor_worker_stop(SensorWorker* instance);