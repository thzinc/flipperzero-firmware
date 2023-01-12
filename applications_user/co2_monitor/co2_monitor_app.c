#include <furi.h>
#include <furi_hal_light.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdlib.h>

#include "sensor_worker.h"

typedef struct {
    bool success;
    bool ready;
    float temp_c;
    float rh_pct;
    float co2_ppm;
} Gas;

typedef struct {
    ValueMutex gas_mutex;

    SensorWorker* sensor_worker;

    FuriMessageQueue* event_queue;

    ViewPort* view_port;
    Gui* gui;
} VocMonitor;

static void draw_callback(Canvas* canvas, void* ctx) {
    VocMonitor* co2_monitor = ctx;

    canvas_clear(canvas);

    const Gas* gas = acquire_mutex_block(&co2_monitor->gas_mutex);
    uint8_t x_width = canvas_width(canvas);
    uint8_t x_center = x_width / 2;
    uint8_t y_curr = 0;
    uint8_t y_height = canvas_height(canvas);
    if(gas->success) {
        if(!gas->ready) {
            furi_hal_light_set(LightRed | LightGreen | LightBlue, 0xFF);

            canvas_set_font(canvas, FontPrimary);
            y_curr += canvas_current_font_height(canvas);
            canvas_draw_str_aligned(
                canvas, x_center, y_curr, AlignCenter, AlignBottom, "Initializing");

            canvas_set_font(canvas, FontSecondary);
            y_curr += canvas_current_font_height(canvas);
            canvas_draw_str_aligned(
                canvas, x_center, y_curr, AlignCenter, AlignBottom, "Sensor module is acclimating");
        } else {
            {
                canvas_set_font(canvas, FontBigNumbers);
                y_curr += canvas_current_font_height(canvas);

                char buffer[12];
                snprintf(buffer, sizeof(buffer), "%4.0f", (double)gas->co2_ppm);
                canvas_draw_str_aligned(
                    canvas, x_center, y_curr, AlignCenter, AlignBottom, buffer);
            }
            {
                canvas_set_font(canvas, FontSecondary);
                y_curr += canvas_current_font_height(canvas);

                canvas_draw_str_aligned(
                    canvas, x_center, y_curr, AlignCenter, AlignBottom, "CO2 ppm");

                y_curr += canvas_current_font_height(canvas);
                if(gas->co2_ppm < 800) {
                    /* CDC-recommended benchmark for adequate indoor ventilation
                     * https://www.cdc.gov/coronavirus/2019-ncov/community/ventilation.html */
                    furi_hal_light_set(LightRed, 0x00);
                    furi_hal_light_set(LightGreen, 0xFF);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas, x_center, y_curr, AlignCenter, AlignBottom, "(good ventilation)");
                } else if(gas->co2_ppm < 1000) {
                    /* Adequate, but consider improving (filtered) airflow */
                    furi_hal_light_set(LightRed, 0x77);
                    furi_hal_light_set(LightGreen, 0xFF);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas,
                        x_center,
                        y_curr,
                        AlignCenter,
                        AlignBottom,
                        "(moderate ventilation)");
                } else if(gas->co2_ppm < 2000) {
                    /* Poor ventilation; minor health effects may be noticeable */
                    furi_hal_light_set(LightRed, 0xFF);
                    furi_hal_light_set(LightGreen, 0xFF);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas,
                        x_center,
                        y_curr,
                        AlignCenter,
                        AlignBottom,
                        "(poor ventilation; improve it!)");
                } else {
                    /* Very poor ventilation; health effects may be noticeable */
                    furi_hal_light_set(LightRed, 0xFF);
                    furi_hal_light_set(LightGreen, 0x00);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas,
                        x_center,
                        y_curr,
                        AlignCenter,
                        AlignBottom,
                        "(poor ventilation; ACT NOW!)");
                }
            }
        }

        char temperature[12];
        snprintf(temperature, sizeof(temperature), "%2.1f C", (double)gas->temp_c);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 0, y_height, AlignLeft, AlignBottom, temperature);

        char relative_humidity[12];
        snprintf(
            relative_humidity, sizeof(relative_humidity), "%3.1f RH%%", (double)gas->rh_pct * 100);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, x_width, y_height, AlignRight, AlignBottom, relative_humidity);
    } else {
        canvas_set_font(canvas, FontPrimary);
        y_curr += canvas_current_font_height(canvas);
        canvas_draw_str_aligned(
            canvas, x_center, y_curr, AlignCenter, AlignBottom, "No connection!");

        canvas_set_font(canvas, FontSecondary);
        y_curr += canvas_current_font_height(canvas);
        canvas_draw_str_aligned(
            canvas, x_center, y_curr, AlignCenter, AlignBottom, "Please connect sensor module");
    }

    release_mutex(&co2_monitor->gas_mutex, gas);
}

static void input_callback(InputEvent* input, void* ctx) {
    VocMonitor* co2_monitor = ctx;
    furi_message_queue_put(co2_monitor->event_queue, input, FuriWaitForever);
}

static void sensor_worker_input_callback(
    bool success,
    bool ready,
    float temp_c,
    float rh_pct,
    float co2_ppm,
    void* context) {
    VocMonitor* co2_monitor = context;

    Gas* gas = acquire_mutex_block(&co2_monitor->gas_mutex);
    gas->success = success;
    gas->ready = ready;
    gas->temp_c = temp_c;
    gas->rh_pct = rh_pct;
    gas->co2_ppm = co2_ppm;

    release_mutex(&co2_monitor->gas_mutex, gas);
}

void co2_monitor_free(VocMonitor* instance) {
    furi_assert(instance);

    view_port_enabled_set(instance->view_port, false);
    gui_remove_view_port(instance->gui, instance->view_port);
    furi_record_close("gui");
    view_port_free(instance->view_port);

    furi_message_queue_free(instance->event_queue);

    delete_mutex(&instance->gas_mutex);

    sensor_worker_free(instance->sensor_worker);

    free(instance);
}

int32_t co2_monitor_app(void* p) {
    UNUSED(p);

    VocMonitor* co2_monitor = malloc(sizeof(VocMonitor));

    Gas* gas = malloc(sizeof(Gas));
    gas->success = false;
    gas->ready = false;
    gas->temp_c = 0.0;
    gas->rh_pct = 0.0;
    gas->co2_ppm = 0.0;

    if(!init_mutex(&co2_monitor->gas_mutex, gas, sizeof(Gas))) {
        FURI_LOG_E("CO2Monitor", "cannot create mutex\r\n");
        free(gas);
        return 255;
    }

    co2_monitor->sensor_worker = sensor_worker_alloc();
    sensor_worker_set_callback(
        co2_monitor->sensor_worker, sensor_worker_input_callback, co2_monitor);

    co2_monitor->view_port = view_port_alloc();
    view_port_draw_callback_set(co2_monitor->view_port, draw_callback, co2_monitor);
    view_port_input_callback_set(co2_monitor->view_port, input_callback, co2_monitor);

    co2_monitor->gui = furi_record_open("gui");
    co2_monitor->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    gui_add_view_port(co2_monitor->gui, co2_monitor->view_port, GuiLayerFullscreen);

    sensor_worker_start(co2_monitor->sensor_worker);

    InputEvent event;
    for(bool processing = true; processing;) {
        FuriStatus status = furi_message_queue_get(co2_monitor->event_queue, &event, 100);
        if(status == FuriStatusOk) {
            if(event.type == InputTypePress) {
                switch(event.key) {
                case InputKeyBack:
                    processing = false;
                    break;
                default:
                    break;
                }
            }
        }
        view_port_update(co2_monitor->view_port);
    }

    sensor_worker_stop(co2_monitor->sensor_worker);

    furi_hal_light_set(LightRed | LightGreen | LightBlue, 0x00);

    co2_monitor_free(co2_monitor);
    return 0;
}