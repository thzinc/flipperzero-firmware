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
    Gas* gas;
    osMutexId_t* gas_mutex;

    SensorWorker* sensor_worker;

    osMessageQueueId_t event_queue;

    ViewPort* view_port;
    Gui* gui;
} VocMonitor;

static void draw_callback(Canvas* canvas, void* ctx) {
    VocMonitor* co2_monitor = ctx;
    string_t label;
    string_init(label);

    canvas_clear(canvas);

    furi_check(osMutexAcquire(co2_monitor->gas_mutex, osWaitForever) == osOK);
    uint8_t x_width = canvas_width(canvas);
    uint8_t x_center = x_width / 2;
    uint8_t y_curr = 0;
    uint8_t y_height = canvas_height(canvas);
    if(co2_monitor->gas->success) {
        if(!co2_monitor->gas->ready) {
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

                string_reset(label);
                string_printf(label, "%4.0f", (double)co2_monitor->gas->co2_ppm);
                canvas_draw_str_aligned(
                    canvas, x_center, y_curr, AlignCenter, AlignBottom, string_get_cstr(label));
            }
            {
                canvas_set_font(canvas, FontSecondary);
                y_curr += canvas_current_font_height(canvas);

                canvas_draw_str_aligned(
                    canvas, x_center, y_curr, AlignCenter, AlignBottom, "CO2 ppm");

                y_curr += canvas_current_font_height(canvas);
                if(co2_monitor->gas->co2_ppm < 800) {
                    /* CDC-recommended benchmark for adequate indoor ventilation
                     * https://www.cdc.gov/coronavirus/2019-ncov/community/ventilation.html */
                    furi_hal_light_set(LightRed, 0x00);
                    furi_hal_light_set(LightGreen, 0xFF);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas, x_center, y_curr, AlignCenter, AlignBottom, "(good ventilation)");
                } else if(co2_monitor->gas->co2_ppm < 1000) {
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
                } else if(co2_monitor->gas->co2_ppm < 2000) {
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

        string_reset(label);
        string_printf(label, "%2.1f C", (double)co2_monitor->gas->temp_c);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 0, y_height, AlignLeft, AlignBottom, string_get_cstr(label));

        string_reset(label);
        string_printf(label, "%3.1f RH%%", (double)co2_monitor->gas->rh_pct * 100);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, x_width, y_height, AlignRight, AlignBottom, string_get_cstr(label));
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

    osMutexRelease(co2_monitor->gas_mutex);

    string_clear(label);
}

static void input_callback(InputEvent* input, void* ctx) {
    VocMonitor* co2_monitor = ctx;
    osMessageQueuePut(co2_monitor->event_queue, input, 0, osWaitForever);
}

static void sensor_worker_input_callback(
    bool success,
    bool ready,
    float temp_c,
    float rh_pct,
    float co2_ppm,
    void* context) {
    VocMonitor* co2_monitor = context;

    furi_check(osMutexAcquire(co2_monitor->gas_mutex, osWaitForever) == osOK);

    co2_monitor->gas->success = success;
    co2_monitor->gas->ready = ready;
    co2_monitor->gas->temp_c = temp_c;
    co2_monitor->gas->rh_pct = rh_pct;
    co2_monitor->gas->co2_ppm = co2_ppm;

    osMutexRelease(co2_monitor->gas_mutex);
}

VocMonitor* co2_monitor_alloc() {
    VocMonitor* instance = malloc(sizeof(VocMonitor));

    {
        instance->gas = malloc(sizeof(Gas));
        instance->gas->success = false;
        instance->gas->ready = false;
        instance->gas->temp_c = 0.0;
        instance->gas->rh_pct = 0.0;
        instance->gas->co2_ppm = 0.0;
        instance->gas_mutex = osMutexNew(NULL);
        instance->sensor_worker = sensor_worker_alloc();
        sensor_worker_set_callback(
            instance->sensor_worker, sensor_worker_input_callback, instance);
    }

    {
        instance->view_port = view_port_alloc();
        view_port_draw_callback_set(instance->view_port, draw_callback, instance);
        view_port_input_callback_set(instance->view_port, input_callback, instance);

        instance->gui = furi_record_open("gui");
        instance->event_queue = osMessageQueueNew(8, sizeof(InputEvent), NULL);
        gui_add_view_port(instance->gui, instance->view_port, GuiLayerFullscreen);
    }

    return instance;
}

void co2_monitor_free(VocMonitor* instance) {
    furi_assert(instance);

    view_port_enabled_set(instance->view_port, false);
    gui_remove_view_port(instance->gui, instance->view_port);
    furi_record_close("gui");
    view_port_free(instance->view_port);

    osMessageQueueDelete(instance->event_queue);

    osMutexDelete(instance->gas_mutex);
    free(instance->gas);

    sensor_worker_free(instance->sensor_worker);

    free(instance);
}

int32_t co2_monitor_app(void* p) {
    UNUSED(p);

    VocMonitor* co2_monitor = co2_monitor_alloc();

    sensor_worker_start(co2_monitor->sensor_worker);

    InputEvent event;
    for(bool processing = true; processing;) {
        osStatus_t status = osMessageQueueGet(co2_monitor->event_queue, &event, NULL, 100);
        if(status == osOK) {
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