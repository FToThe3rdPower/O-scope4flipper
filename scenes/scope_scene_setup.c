#include "../scope_app_i.h"

static void led_cb(VariableItem* item) {
    ScopeApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, led_list[idx].str);
    app->led_brightness = led_list[idx].value;
}

void scope_scene_setup_on_enter(void* context) {
    ScopeApp* app = context;
    VariableItemList* vil = app->variable_item_list;
    VariableItem* item;

    // LED brightness
    item = variable_item_list_add(vil, "LED Brightness", COUNT_OF(led_list), led_cb, app);
    for(uint32_t i = 0; i < COUNT_OF(led_list); i++) {
        if(led_list[i].value == app->led_brightness) {
            variable_item_set_current_value_index(item, i);
            variable_item_set_current_value_text(item, led_list[i].str);
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
