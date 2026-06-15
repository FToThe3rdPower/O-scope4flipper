#pragma once

#include "scenes/scope_types.h"
#include "scenes/scope_scene.h"

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>

// 512 samples: enough headroom to pan a 128-px window across the buffer
#define ADC_BUFFER_SIZE   ((uint32_t)512)
#define SCOPE_EXTENSION   ".dat"
#define MAX_LEN_NAME      30

typedef struct ScopeApp ScopeApp;

// ── Time scales ────────────────────────────────────────────────────────────
typedef struct { double time; char* str; } timeperiod;

static const timeperiod time_list[] = {
    {1.0,    "1s"},
    {0.5,    "500ms"},
    {0.1,    "100ms"},
    {0.05,   "50ms"},
    {0.01,   "10ms"},
    {0.005,  "5ms"},
    {1e-3,   "1ms"},
    {5e-4,   "500us"},
    {1e-4,   "100us"},
    {5e-5,   "50us"},
    {1e-5,   "10us"},
    {5e-6,   "5us"},
    {2e-6,   "2us"},
    {1e-6,   "1us"},
};

// ── FFT window ─────────────────────────────────────────────────────────────
typedef struct { int window; char* str; } fftwindow;

static const fftwindow fft_list[] = {
    {256,  "256"},
    {512,  "512"},
    {1024, "1024"},
};

// ── Display scale ──────────────────────────────────────────────────────────
typedef struct { float scale; char* str; } scalesize;

static const scalesize scale_list[] = {
    {1.0f,   "1x"},
    {2.0f,   "2x"},
    {4.0f,   "4x"},
    {10.0f,  "10x"},
    {100.0f, "100x"},
};

// ── Measurement modes ──────────────────────────────────────────────────────
// m_voltage kept in enum but not exposed in the main menu (used internally)
enum measureenum { m_time, m_voltage, m_capture, m_record, m_fft, m_pulse, m_histogram };

// ── Trigger threshold (for PulseCount + Histogram) ────────────────────────
typedef struct { uint16_t mv; char* str; } threshold;

static const threshold threshold_list[] = {
    {50,   "50mV"},
    {100,  "100mV"},
    {200,  "200mV"},
    {300,  "300mV"},
    {500,  "500mV"},
    {750,  "750mV"},
    {1000, "1.0V"},
    {1250, "1.25V"},
    {1500, "1.5V"},
    {2000, "2.0V"},
};

// ── Main-menu model (for custom start view) ────────────────────────────────
typedef struct {
    uint8_t selected;   // 0=Run, 1=Settings, 2=Load, 3=About
    uint8_t mode_idx;   // index into the six menu modes
} StartMenuModel;

// Callbacks defined in scope_scene_start.c, registered in scope.c
void scope_scene_start_draw_cb(Canvas* canvas, void* model);
bool scope_scene_start_input_cb(InputEvent* event, void* ctx);

// ── App state ──────────────────────────────────────────────────────────────
struct ScopeApp {
    Gui*                gui;
    ViewDispatcher*     view_dispatcher;
    SceneManager*       scene_manager;
    NotificationApp*    notifications;
    VariableItemList*   variable_item_list;
    Submenu*            submenu;        // kept for back-compat; not used by start scene
    Widget*             widget;
    TextInput*          text_input;
    View*               start_view;    // Custom main-menu view

    double              time;           // Seconds per sample
    int                 fft;            // FFT window size
    float               scale;          // Display vertical scale
    enum measureenum    measurement;
    uint16_t            trigger_mv;     // Detection threshold (mV)

    char                file_name_tmp[MAX_LEN_NAME];
    uint16_t*           data;           // Saved capture buffer
    uint32_t            data_size;      // Samples in data
};

enum ScopeCustomEvent {
    ScopeCustomEventTextInputDone,
    ScopeCustomEventStartRun,
    ScopeCustomEventStartSettings,
    ScopeCustomEventStartLoad,
    ScopeCustomEventStartAbout,
};
