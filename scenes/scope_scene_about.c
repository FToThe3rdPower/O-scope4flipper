#include "../scope_app_i.h"

void scope_scene_about_on_enter(void* context) {
    ScopeApp* app = context;
    widget_add_text_scroll_element(
        app->widget,
        0, 0, 128, 64,
        "O-scope v0.1\n"
        "---\n"
        "Signal: pin 16/PC0\n"
        "GND:    pin 18\n"
        "Range:  0-2.5V\n"
        "---\n"
        "Modes:\n"
        " Time  - frequency\n"
        " Volt  - min/max/Vpp\n"
        " Capture - save .dat\n"
        " FFT   - spectrum\n"
        " Pulse - CPS/CPM\n"
        " Hist  - pulse height\n"
        "---\n"
        "Run controls:\n"
        " OK    - pause/unpause\n"
        " Up/Dn - level shift\n"
        " L/R   - pan (paused)\n"
        " Hist+paused: L=clear\n"
        "---\n"
        "github.com/FToThe3rdPower");
    view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewWidget);
}

bool scope_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scope_scene_about_on_exit(void* context) {
    ScopeApp* app = context;
    widget_reset(app->widget);
}
