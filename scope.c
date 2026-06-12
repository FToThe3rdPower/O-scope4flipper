#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#include "scope_app_i.h"

void assert_failed(uint8_t* file, uint32_t line) {
    UNUSED(file);
    UNUSED(line);
    while(1) {}
}

static bool scope_custom_event_cb(void* ctx, uint32_t event) {
    ScopeApp* app = ctx;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool scope_back_event_cb(void* ctx) {
    ScopeApp* app = ctx;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void scope_tick_event_cb(void* ctx) {
    ScopeApp* app = ctx;
    scene_manager_handle_tick_event(app->scene_manager);
}

static ScopeApp* scope_app_alloc(void) {
    ScopeApp* app = malloc(sizeof(ScopeApp));

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager   = scene_manager_alloc(&scope_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, scope_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, scope_back_event_cb);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, scope_tick_event_cb, 100);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ScopeViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ScopeViewSubmenu,
        submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ScopeViewWidget,
        widget_get_view(app->widget));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ScopeViewSave,
        text_input_get_view(app->text_input));

    // Defaults
    app->time        = 1e-3;   // 1 ms/sample → 1 kHz
    app->scale       = 1.0f;
    app->fft         = 256;
    app->measurement = m_time;
    app->trigger_mv  = 200;    // 200 mV default threshold
    app->data        = NULL;
    app->data_size   = 0;

    scene_manager_next_scene(app->scene_manager, ScopeSceneStart);
    return app;
}

static void scope_app_free(ScopeApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ScopeViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, ScopeViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, ScopeViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, ScopeViewSave);
    text_input_free(app->text_input);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    if(app->data) free(app->data);
    free(app);
}

int32_t scope_main(void* p) {
    UNUSED(p);
    ScopeApp* app = scope_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    scope_app_free(app);
    return 0;
}
