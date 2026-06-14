#include "../scope_app_i.h"

static void timeperiod_cb(VariableItem* item) {
    ScopeApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, time_list[idx].str);
    app->time = time_list[idx].time;
}

static void fft_cb(VariableItem* item) {
    ScopeApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, fft_list[idx].str);
    app->fft = fft_list[idx].window;
}

static void scale_cb(VariableItem* item) {
    ScopeApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, scale_list[idx].str);
    app->scale = scale_list[idx].scale;
}

static void threshold_cb(VariableItem* item) {
    ScopeApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, threshold_list[idx].str);
    app->trigger_mv = threshold_list[idx].mv;
}

void scope_scene_setup_on_enter(void* context) {
    ScopeApp* app = context;
    VariableItemList* vil = app->variable_item_list;
    VariableItem* item;

    // Time period
    item = variable_item_list_add(vil, "Time/sample", COUNT_OF(time_list), timeperiod_cb, app);
    for(uint32_t i = 0; i < COUNT_OF(time_list); i++) {
        if(time_list[i].time == app->time) {
            variable_item_set_current_value_index(item, i);
            variable_item_set_current_value_text(item, time_list[i].str);
            break;
        }
    }

    // FFT window
    item = variable_item_list_add(vil, "FFT window", COUNT_OF(fft_list), fft_cb, app);
    for(uint32_t i = 0; i < COUNT_OF(fft_list); i++) {
        if(fft_list[i].window == app->fft) {
            variable_item_set_current_value_index(item, i);
            variable_item_set_current_value_text(item, fft_list[i].str);
            break;
        }
    }

    // Scale
    item = variable_item_list_add(vil, "Scale", COUNT_OF(scale_list), scale_cb, app);
    for(uint32_t i = 0; i < COUNT_OF(scale_list); i++) {
        if(scale_list[i].scale == app->scale) {
            variable_item_set_current_value_index(item, i);
            variable_item_set_current_value_text(item, scale_list[i].str);
            break;
        }
    }

    // Trigger threshold
    item = variable_item_list_add(vil, "Threshold", COUNT_OF(threshold_list), threshold_cb, app);
    for(uint32_t i = 0; i < COUNT_OF(threshold_list); i++) {
        if(threshold_list[i].mv == app->trigger_mv) {
            variable_item_set_current_value_index(item, i);
            variable_item_set_current_value_text(item, threshold_list[i].str);
            break;
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewVariableItemList);
}

bool scope_scene_setup_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scope_scene_setup_on_exit(void* context) {
    ScopeApp* app = context;
    variable_item_list_reset(app->variable_item_list);
    widget_reset(app->widget);
}
