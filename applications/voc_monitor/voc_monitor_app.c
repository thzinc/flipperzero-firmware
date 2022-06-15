#include <furi.h>
#include <furi_hal_light.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdlib.h>

#include "sensor_worker.h"

typedef struct {
    bool success;
    float temp_c;
    float rh_pct;
    bool initializing;
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
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
    VocMonitor* voc_monitor = ctx;
    string_t label;
    string_init(label);

    canvas_clear(canvas);

    furi_check(osMutexAcquire(voc_monitor->gas_mutex, osWaitForever) == osOK);
    uint8_t x_width = canvas_width(canvas);
    uint8_t x_center = x_width / 2;
    uint8_t y_curr = 0;
    if(voc_monitor->gas->success) {
        if(voc_monitor->gas->initializing) {
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
                string_printf(label, "%d", voc_monitor->gas->eco2_ppm);
                canvas_draw_str_aligned(
                    canvas, 0, y_curr, AlignLeft, AlignBottom, string_get_cstr(label));

                string_reset(label);
                string_printf(label, "%d", voc_monitor->gas->tvoc_ppb);
                canvas_draw_str_aligned(
                    canvas, x_width, y_curr, AlignRight, AlignBottom, string_get_cstr(label));
            }
            {
                canvas_set_font(canvas, FontSecondary);
                y_curr += canvas_current_font_height(canvas);

                canvas_draw_str_aligned(canvas, 0, y_curr, AlignLeft, AlignBottom, "equiv CO2");
                canvas_draw_str_aligned(
                    canvas, x_width, y_curr, AlignRight, AlignBottom, "total VOC");

                y_curr += canvas_current_font_height(canvas);
                if(voc_monitor->gas->eco2_ppm < 800) {
                    /* CDC-recommended benchmark for adequate indoor ventilation
                     * https://www.cdc.gov/coronavirus/2019-ncov/community/ventilation.html */
                    furi_hal_light_set(LightRed, 0x00);
                    furi_hal_light_set(LightGreen, 0xFF);
                    furi_hal_light_set(LightBlue, 0x00);

                    canvas_draw_str_aligned(
                        canvas, x_center, y_curr, AlignCenter, AlignBottom, "(good ventilation)");
                } else if(voc_monitor->gas->eco2_ppm < 1000) {
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
                } else if(voc_monitor->gas->eco2_ppm < 2000) {
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
        string_printf(label, "%2.1f C", (double)voc_monitor->gas->temp_c);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 0, canvas_height(canvas), AlignLeft, AlignBottom, string_get_cstr(label));

        string_reset(label);
        string_printf(label, "%3.1f RH%%", (double)voc_monitor->gas->rh_pct * 100);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            x_width,
            canvas_height(canvas),
            AlignRight,
            AlignBottom,
            string_get_cstr(label));
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

    osMutexRelease(voc_monitor->gas_mutex);

    string_clear(label);
}

static void input_callback(InputEvent* input, void* ctx) {
    VocMonitor* voc_monitor = ctx;
    osMessageQueuePut(voc_monitor->event_queue, input, 0, osWaitForever);
}

static void sensor_worker_input_callback(
    bool success,
    float temp_c,
    float rh_pct,
    bool initializing,
    uint16_t tvoc_ppb,
    uint16_t eco2_ppm,
    void* context) {
    VocMonitor* voc_monitor = context;

    furi_check(osMutexAcquire(voc_monitor->gas_mutex, osWaitForever) == osOK);

    voc_monitor->gas->success = success;
    voc_monitor->gas->temp_c = temp_c;
    voc_monitor->gas->rh_pct = rh_pct;
    voc_monitor->gas->initializing = initializing;
    voc_monitor->gas->tvoc_ppb = tvoc_ppb;
    voc_monitor->gas->eco2_ppm = eco2_ppm;

    osMutexRelease(voc_monitor->gas_mutex);
}

VocMonitor* voc_monitor_alloc() {
    VocMonitor* instance = malloc(sizeof(VocMonitor));

    {
        instance->gas = malloc(sizeof(Gas));
        instance->gas->success = false;
        instance->gas->temp_c = 0.0;
        instance->gas->rh_pct = 0.0;
        instance->gas->initializing = true;
        instance->gas->tvoc_ppb = 0;
        instance->gas->eco2_ppm = 0;
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

void voc_monitor_free(VocMonitor* instance) {
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

int32_t voc_monitor_app(void* p) {
    UNUSED(p);

    VocMonitor* voc_monitor = voc_monitor_alloc();

    sensor_worker_start(voc_monitor->sensor_worker);

    InputEvent event;
    for(bool processing = true; processing;) {
        osStatus_t status = osMessageQueueGet(voc_monitor->event_queue, &event, NULL, 100);
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
        view_port_update(voc_monitor->view_port);
    }

    sensor_worker_stop(voc_monitor->sensor_worker);

    furi_hal_light_set(LightRed | LightGreen | LightBlue, 0x00);

    voc_monitor_free(voc_monitor);
    return 0;
}