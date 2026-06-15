#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include "../scope_app_i.h"

#define PB_DISPLAY_W  128
#define PB_DISPLAY_H   64
#define PB_PAN_STEP    16
#define PB_VSHIFT_MV  100
#define PB_VDDA_MV   2500

static uint16_t* g_pb_data;
static uint32_t  g_pb_size;
static int32_t   g_pb_h_offset;
static int16_t   g_pb_v_offset_mv;
static float     g_pb_scale;

static inline int32_t pb_mv_to_y(float mv) {
    float shifted = mv + (float)g_pb_v_offset_mv;
    return 63 - (int32_t)((shifted / (float)PB_VDDA_MV) * g_pb_scale * (float)(PB_DISPLAY_H - 1));
}

static int32_t pb_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void pb_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    static char hud[32];
    canvas_draw_str(canvas, 2, 10, "PLAYBACK");

    if(g_pb_size > PB_DISPLAY_W) {
        snprintf(
            hud, sizeof(hud), "%lu/%lu",
            (unsigned long)(g_pb_h_offset + 1),
            (unsigned long)g_pb_size);
        canvas_draw_str(canvas, 54, 10, hud);
    }

    if(g_pb_v_offset_mv != 0) {
        snprintf(hud, sizeof(hud), "%+dmV", (int)g_pb_v_offset_mv);
        canvas_draw_str(canvas, 2, 20, hud);
    }

    for(uint32_t px = 1; px < PB_DISPLAY_W; px++) {
        uint32_t si = (uint32_t)g_pb_h_offset + px;
        if(si >= g_pb_size) break;
        int32_t y0 = pb_mv_to_y((float)g_pb_data[si - 1]);
        int32_t y1 = pb_mv_to_y((float)g_pb_data[si]);
        if(y0 < 0 && y1 < 0) continue;
        canvas_draw_line(
            canvas,
            (int32_t)(px - 1), pb_clamp(y0, 0, PB_DISPLAY_H - 1),
            (int32_t)px,       pb_clamp(y1, 0, PB_DISPLAY_H - 1));
    }
}

static void pb_input_callback(InputEvent* ev, void* ctx) {
    furi_message_queue_put((FuriMessageQueue*)ctx, ev, FuriWaitForever);
}

void scope_scene_playback_on_enter(void* context) {
    ScopeApp* app = context;

    g_pb_data        = app->data;
    g_pb_size        = app->data_size;
    g_pb_h_offset    = 0;
    g_pb_v_offset_mv = 0;
    g_pb_scale       = app->scale;

    FuriMessageQueue* eq = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort*         vp = view_port_alloc();
    view_port_draw_callback_set(vp, pb_draw_callback, vp);
    view_port_input_callback_set(vp, pb_input_callback, eq);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    InputEvent ev;
    bool       running = true;
    while(running) {
        if(furi_message_queue_get(eq, &ev, 150) == FuriStatusOk) {
            if(ev.type == InputTypePress || ev.type == InputTypeRepeat) {
                int32_t max_off = (int32_t)g_pb_size - PB_DISPLAY_W;
                if(max_off < 0) max_off = 0;
                switch(ev.key) {
                case InputKeyLeft:
                    g_pb_h_offset -= PB_PAN_STEP;
                    if(g_pb_h_offset < 0) g_pb_h_offset = 0;
                    break;
                case InputKeyRight:
                    g_pb_h_offset += PB_PAN_STEP;
                    if(g_pb_h_offset > max_off) g_pb_h_offset = max_off;
                    break;
                case InputKeyUp:
                    g_pb_v_offset_mv += PB_VSHIFT_MV;
                    if(g_pb_v_offset_mv > 2500) g_pb_v_offset_mv = 2500;
                    break;
                case InputKeyDown:
                    g_pb_v_offset_mv -= PB_VSHIFT_MV;
                    if(g_pb_v_offset_mv < -2500) g_pb_v_offset_mv = -2500;
                    break;
                default:
                    running = false;
                    break;
                }
            }
        }
        view_port_update(vp);
    }

    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(eq);

    free(app->data);
    app->data      = NULL;
    app->data_size = 0;

    scene_manager_previous_scene(app->scene_manager);
}

bool scope_scene_playback_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scope_scene_playback_on_exit(void* context) {
    UNUSED(context);
}
