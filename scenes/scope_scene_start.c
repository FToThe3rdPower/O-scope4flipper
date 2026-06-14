#include "../scope_app_i.h"
#include <gui/canvas.h>
#include <input/input.h>

typedef struct {
    const char* label;
    const char* desc1;
    const char* desc2;
} ModeEntry;

static const ModeEntry MODES[] = {
    { "Time",      "Oscilloscope: continuous",  "time-domain trace"        },
    { "Capture",   "Triggered capture;",        "saves on threshold cross"  },
    { "Record",    "Records all to SD;",        "silence is recorded too"   },
    { "FFT",       "Frequency spectrum",        "via FFT"                   },
    { "Counter",   "Pulse counter:",            "shows CPS and CPM"         },
    { "Histogram", "Pulse-height histogram",    "for spectroscopy"          },
};
#define MODE_COUNT ((int)(sizeof(MODES) / sizeof(MODES[0])))

#define ROW_RUN      0
#define ROW_SETTINGS 1
#define ROW_ABOUT    2
#define ROW_COUNT    3

// ── Layout (128×64 OLED, FontSecondary ~8px tall) ─────────────────────────
#define ROW_H        13   // uniform height for selectable rows

// Run row
#define RUN_Y_BOX    0
#define RUN_Y_TEXT   10   // baseline

// Inline mode picker within the Run row.
// Label area is fixed to the width of "Histogram" (widest label) so the
// arrows don't move. Label is centred dynamically within that area.
#define PKR_ARW_LX   26                          // "<" x position
#define PKR_LBL_X    34                          // label area start (after "<" + gap)
#define PKR_LBL_W    64                          // wide enough for "Histogram"
#define PKR_ARW_RX   (PKR_LBL_X + PKR_LBL_W + 2)  // ">" x position = 100

// Description: 2 lines below Run row
#define DESC1_Y      21   // baseline
#define DESC2_Y      30   // baseline

// Divider and lower rows
#define DIV_Y        32
#define SET_Y_BOX    33
#define SET_Y_TEXT   43
#define ABT_Y_BOX    46
#define ABT_Y_TEXT   56

// ── Draw callback ──────────────────────────────────────────────────────────
void scope_scene_start_draw_cb(Canvas* canvas, void* model_ptr) {
    StartMenuModel* m = model_ptr;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    // ── Run row ─────────────────────────────────────────────────────────────
    if(m->selected == ROW_RUN) {
        canvas_draw_rbox(canvas, 0, RUN_Y_BOX, 128, ROW_H, 2);
        canvas_invert_color(canvas);
    }
    canvas_draw_str(canvas, 2, RUN_Y_TEXT, "Run");

    if(m->mode_idx > 0)
        canvas_draw_str(canvas, PKR_ARW_LX, RUN_Y_TEXT, "<");
    if(m->mode_idx < MODE_COUNT - 1)
        canvas_draw_str(canvas, PKR_ARW_RX, RUN_Y_TEXT, ">");

    // Centre the mode label inside the fixed-width picker area
    const char* lbl = MODES[m->mode_idx].label;
    int32_t lw = (int32_t)canvas_string_width(canvas, lbl);
    canvas_draw_str(canvas, PKR_LBL_X + (PKR_LBL_W - lw) / 2, RUN_Y_TEXT, lbl);

    if(m->selected == ROW_RUN) canvas_invert_color(canvas);

    // ── Description (2 lines below Run row) ──────────────────────────────────
    canvas_draw_str(canvas, 2, DESC1_Y, MODES[m->mode_idx].desc1);
    canvas_draw_str(canvas, 2, DESC2_Y, MODES[m->mode_idx].desc2);

    // ── Divider ──────────────────────────────────────────────────────────────
    canvas_draw_line(canvas, 0, DIV_Y, 127, DIV_Y);

    // ── Settings row ─────────────────────────────────────────────────────────
    if(m->selected == ROW_SETTINGS) {
        canvas_draw_rbox(canvas, 0, SET_Y_BOX, 128, ROW_H, 2);
        canvas_invert_color(canvas);
    }
    canvas_draw_str(canvas, 2, SET_Y_TEXT, "Settings");
    if(m->selected == ROW_SETTINGS) canvas_invert_color(canvas);

    // ── About row ────────────────────────────────────────────────────────────
    if(m->selected == ROW_ABOUT) {
        canvas_draw_rbox(canvas, 0, ABT_Y_BOX, 128, ROW_H, 2);
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
