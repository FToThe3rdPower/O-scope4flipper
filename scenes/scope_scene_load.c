#include <furi.h>
#include <storage/storage.h>
#include <string.h>
#include "../scope_app_i.h"

#define MAX_SAVES 20

static char save_names[MAX_SAVES][MAX_LEN_NAME + 1];
static int  save_count;
static int  load_selected;

static void load_submenu_cb(void* ctx, uint32_t index) {
    ScopeApp* app = ctx;
    load_selected = (int)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScopeCustomEventLoadSelect);
}

void scope_scene_load_on_enter(void* context) {
    ScopeApp* app = context;
    save_count    = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File*    dir     = storage_file_alloc(storage);

    if(storage_dir_open(dir, APP_DATA_PATH(""))) {
        FileInfo info;
        char     name[MAX_LEN_NAME + 5];
        while(save_count < MAX_SAVES &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            size_t len = strlen(name);
            if(len > 4 && strcmp(name + len - 4, SCOPE_EXTENSION) == 0) {
                size_t base = len - 4;
                if(base > MAX_LEN_NAME) base = MAX_LEN_NAME;
                memcpy(save_names[save_count], name, base);
                save_names[save_count][base] = '\0';
                save_count++;
            }
        }
        storage_dir_close(dir);
    }

    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);

    if(save_count == 0) {
        widget_add_string_element(
            app->widget, 64, 28, AlignCenter, AlignCenter, FontSecondary,
            "No saved captures");
        widget_add_string_element(
            app->widget, 64, 38, AlignCenter, AlignCenter, FontSecondary,
            "found on SD card");
        view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewWidget);
        return;
    }

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Load Capture");
    for(int i = 0; i < save_count; i++)
        submenu_add_item(app->submenu, save_names[i], (uint32_t)i, load_submenu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ScopeViewSubmenu);
}

bool scope_scene_load_on_event(void* context, SceneManagerEvent event) {
    ScopeApp* app      = context;
    bool      consumed = false;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == ScopeCustomEventLoadSelect &&
       load_selected < save_count) {

        FuriString* path = furi_string_alloc();
        furi_string_printf(
            path, "%s/%s%s",
            APP_DATA_PATH(""), save_names[load_selected], SCOPE_EXTENSION);

        Storage* storage = furi_record_open(RECORD_STORAGE);
        File*    file    = storage_file_alloc(storage);

        if(storage_file_open(
               file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint64_t sz = storage_file_size(file);
            uint32_t n  = (uint32_t)(sz / sizeof(uint16_t));
            if(n > 0 && sz <= 65534) {
                if(app->data) free(app->data);
                app->data = malloc(sizeof(uint16_t) * n);
                if(app->data) {
                    storage_file_read(file, app->data, (uint16_t)sz);
                    app->data_size = n;
                }
            }
            storage_file_close(file);
        }

        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        furi_string_free(path);

        if(app->data && app->data_size > 0)
            scene_manager_next_scene(app->scene_manager, ScopeScenePlayback);

        consumed = true;
    }
    return consumed;
}

void scope_scene_load_on_exit(void* context) {
    ScopeApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
