#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/elements.h>
#include <storage/storage.h>
#include "../scope_app_i.h"

static void scope_scene_save_text_input_callback(void* context) {
    ScopeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScopeCustomEventTextInputDone);
}

void scope_scene_save_on_enter(void* context) {
    ScopeApp* app = context;
    text_input_set_header_text(app->text_input, "Name signal");
    text_input_set_result_callback(
        app->text_input,
        scope_scene_save_text_input_callback,
        app,
        app->file_name_tmp,
        MAX_LEN_NAME,
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewSave);
}

bool scope_scene_save_on_event(void* context, SceneManagerEvent event) {
    ScopeApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == ScopeCustomEventTextInputDone) {
        if(strcmp(app->file_name_tmp, "") != 0) {
            FuriString* path = furi_string_alloc();
            furi_string_printf(path, "%s/%s%s",
                APP_DATA_PATH(""), app->file_name_tmp, SCOPE_EXTENSION);

            Storage* storage = furi_record_open(RECORD_STORAGE);
            File* file = storage_file_alloc(storage);
            if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_write(file, app->data, sizeof(uint16_t) * app->data_size);
                storage_file_close(file);
            }
            storage_file_free(file);
            furi_record_close(RECORD_STORAGE);
            furi_string_free(path);

            free(app->data);
            app->data = NULL;
            app->data_size = 0;

            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void scope_scene_save_on_exit(void* context) {
    ScopeApp* app = context;
    text_input_reset(app->text_input);
}
