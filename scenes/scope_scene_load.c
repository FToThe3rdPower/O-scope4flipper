#include <furi.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include "../scope_app_i.h"

void scope_scene_load_on_enter(void* context) {
    ScopeApp* app = context;

    // Ensure the app data directory exists (may not if user has never saved)
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, APP_DATA_PATH(""));
    furi_record_close(RECORD_STORAGE);

    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    FuriString* result  = furi_string_alloc();
    FuriString* start   = furi_string_alloc_set(APP_DATA_PATH(""));

    const DialogsFileBrowserOptions opts = {
        .extension   = SCOPE_EXTENSION,
        .icon        = NULL,
        .skip_assets = true,
    };

    bool picked = dialog_file_browser_show(dialogs, result, start, &opts);

    furi_record_close(RECORD_DIALOGS);
    furi_string_free(start);

    bool loaded = false;
    if(picked) {
        storage       = furi_record_open(RECORD_STORAGE);
        File* file    = storage_file_alloc(storage);

        if(storage_file_open(
               file, furi_string_get_cstr(result), FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint64_t sz = storage_file_size(file);
            uint32_t n  = (uint32_t)(sz / sizeof(uint16_t));
            if(n > 0 && sz <= 65534) {
                if(app->data) free(app->data);
                app->data = malloc(sizeof(uint16_t) * n);
                if(app->data) {
                    storage_file_read(file, app->data, (uint16_t)sz);
                    app->data_size = n;
                    loaded         = true;
                }
            }
            storage_file_close(file);
        }

        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
    }

    furi_string_free(result);

    if(loaded) {
        scene_manager_next_scene(app->scene_manager, ScopeScenePlayback);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool scope_scene_load_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scope_scene_load_on_exit(void* context) {
    UNUSED(context);
}
