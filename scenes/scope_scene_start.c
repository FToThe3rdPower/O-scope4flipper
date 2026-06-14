#include "../scope_app_i.h"
#include <gui/canvas.h>
#include <input/input.h>

typedef struct {
    const char* label;
    const char* desc1;
    const char* desc2;
} ModeEntry;

static const ModeEntry MODES[] = {
    { "Time",      "Oscilloscope: continuous",  "time-domain trace"           },
    { "Capture",   "Triggered capture;",        "saves on threshold cross"    },
    { "Record",    "Records all to SD;",        "silence is recorded too"     },
    { "FFT",       "Frequency spectrum",        "via FFT"                     },
    { "Counter",   "Pulse counter:",            "shows CPS and CPM"           },
    { "Histogram", "Pulse-height histogram",    "for spectroscopy"            },
};
#define MODE_COUNT ((int)(sizeof(MODES) / sizeof(MODES[0])))

#define ROW_RUN      0
#define ROW_SETTINGS 1
#define ROW_ABOUT    2
#define ROW_COUNT    3

// ── Layout (128×64 Monochrome, FontSecondary ~8px tall) ─────────────────────────
// Run row: y=0..10 (11px)
#define RUN_Y_TOP   0
#define RUN_Y_H     11
#define RUN_Y_TEXT  9

// Mode picker box: below Run row, only as wide as "Histogram" (widest label).
// FontSecondary ≈6px/char; 9 chars + ~4px pad each side ≈ 62px → 64px box.
// Arrows sit outside the box.
#define PKR_Y_TOP   (RUN_Y_TOP + RUN_Y_H)      // = 11
#define PKR_H       16                           // 2 font-heights tall
#define PKR_W       64
#define PKR_X       ((128 - PKR_W) / 2)         // = 32 (centred)
#define PKR_Y_TEXT  (PKR_Y_TOP + 12)            // = 23 (8px font centred in 16px box)
#define PKR_ARW_LX  (PKR_X - 8)                 // = 24
#define PKR_ARW_RX  (PKR_X + PKR_W + 2)         // = 98

// Description: 2 lines below picker
#define DESC1_Y     (PKR_Y_TOP + PKR_H + 8)     // = 35 (baseline)
#define DESC2_Y     (DESC1_Y + 9)               // = 44 (baseline)

// Divider + lower rows
#define DIV_Y       (DESC2_Y + 1)               // = 45
#define SET_Y_TOP   (DIV_Y + 1)                 // = 46
#define SET_Y_H     9
#define SET_Y_TEXT  (SET_Y_TOP + 8)             // = 54
#define ABT_Y_TOP   (SET_Y_TOP + SET_Y_H)       // = 55
#define ABT_Y_H     9
#define ABT_Y_TEXT  (ABT_Y_TOP + 8)             // = 63

// ── Draw callback ──────────────────────────────────────────────────────────
void scope_scene_start_draw_cb(Canvas* canvas, void* model_ptr) {
    StartMenuModel* m = model_ptr;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    // ── Run row ─────────────────────────────────────────────────────────────
    if(m->selected == ROW_RUN) {
        canvas_draw_rbox(canvas, 0, RUN_Y_TOP, 128, RUN_Y_H, 2);
        canvas_invert_color(canvas);
    }
    canvas_draw_str(canvas, 2, RUN_Y_TEXT, "Run");
    if(m->selected == ROW_RUN) canvas_invert_color(canvas);

    // ── Mode picker (always visible, below Run row) ──────────────────────────
    canvas_draw_rframe(canvas, PKR_X, PKR_Y_TOP, PKR_W, PKR_H, 2);

    if(m->mode_idx > 0)
        canvas_draw_str(canvas, PKR_ARW_LX, PKR_Y_TEXT, "<");
    if(m->mode_idx < MODE_COUNT - 1)
        canvas_draw_str(canvas, PKR_ARW_RX, PKR_Y_TEXT, ">");

    // Centre mode label inside picker box
    const char* lbl = MODES[m->mode_idx].label;
    int32_t lw = (int32_t)canvas_string_width(canvas, lbl);
    canvas_draw_str(canvas, PKR_X + (PKR_W - lw) / 2, PKR_Y_TEXT, lbl);

    // ── Description (2 lines) ────────────────────────────────────────────────
    canvas_draw_str(canvas, 2, DESC1_Y, MODES[m->mode_idx].desc1);
    canvas_draw_str(canvas, 2, DESC2_Y, MODES[m->mode_idx].desc2);

    // ── Divider ──────────────────────────────────────────────────────────────
    canvas_draw_line(canvas, 0, DIV_Y, 127, DIV_Y);

    // ── Settings row ─────────────────────────────────────────────────────────
    if(m->selected == ROW_SETTINGS) {
        canvas_draw_rbox(canvas, 0, SET_Y_TOP, 128, SET_Y_H, 2);
        canvas_invert_color(canvas);
    }
    canvas_draw_str(canvas, 2, SET_Y_TEXT, "Settings");
    if(m->selected == ROW_SETTINGS) canvas_invert_color(canvas);

    // ── About row ────────────────────────────────────────────────────────────
    if(m->selected == ROW_ABOUT) {
        canvas_draw_rbox(canvas, 0, ABT_Y_TOP, 128, ABT_Y_H, 2);
        canvas_invert_color(canvas);
    }
    canvas_draw_str(canvas, 2, ABT_Y_TEXT, "About");
    if(m->selected == ROW_ABOUT) canvas_invert_color(canvas);
}

// ── Input callback ─────────────────────────────────────────────────────────
bool scope_scene_start_input_cb(InputEvent* event, void* ctx) {
    ScopeApp* app = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    StartMenuModel* m = view_get_model(app->start_view);

    bool consumed = true;
    bool changed  = false;

    switch(event->key) {
    case InputKeyUp:
        if(m->selected > 0) { m->selected--; changed = true; }
        break;
    case InputKeyDown:
        if(m->selected < ROW_COUNT - 1) { m->selected++; changed = true; }
        break;
    case InputKeyLeft:
        if(m->selected == ROW_RUN && m->mode_idx > 0) {
            m->mode_idx--;
            changed = true;
        }
        break;
    case InputKeyRight:
        if(m->selected == ROW_RUN && m->mode_idx < MODE_COUNT - 1) {
            m->mode_idx++;
            changed = true;
        }
        break;
    case InputKeyOk:
        view_commit_model(app->start_view, false);
        if(m->selected == ROW_RUN) {
            view_dispatcher_send_custom_event(app->view_dispatcher, ScopeCustomEventStartRun);
        } else if(m->selected == ROW_SETTINGS) {
            view_dispatcher_send_custom_event(app->view_dispatcher, ScopeCustomEventStartSettings);
        } else {
            view_dispatcher_send_custom_event(app->view_dispatcher, ScopeCustomEventStartAbout);
        }
        return true;
    default:
        consumed = false;
        break;
    }

    view_commit_model(app->start_view, changed);
    return consumed;
}

// ── Scene callbacks ────────────────────────────────────────────────────────
void scope_scene_start_on_enter(void* context) {
    ScopeApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewStart);
}

bool scope_scene_start_on_event(void* context, SceneManagerEvent event) {
    ScopeApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case ScopeCustomEventStartRun: {
            StartMenuModel* m = view_get_model(app->start_view);
            static const enum measureenum mode_map[] = {
                m_time, m_capture, m_record, m_fft, m_pulse, m_histogram,
            };
            app->measurement = mode_map[m->mode_idx];
            view_commit_model(app->start_view, false);
            scene_manager_next_scene(app->scene_manager, ScopeSceneRun);
            consumed = true;
            break;
        }
        case ScopeCustomEventStartSettings:
            scene_manager_next_scene(app->scene_manager, ScopeSceneSetup);
            consumed = true;
            break;
        case ScopeCustomEventStartAbout:
            scene_manager_next_scene(app->scene_manager, ScopeSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void scope_scene_start_on_exit(void* context) {
    UNUSED(context);
}
