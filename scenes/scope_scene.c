#include "../scope_app_i.h"

#define ADD_SCENE(prefix, name, id) scope_scene_##name##_on_enter,
void (*const scope_on_enter_handlers[])(void*) = {
#include "scope_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) scope_scene_##name##_on_event,
bool (*const scope_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "scope_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) scope_scene_##name##_on_exit,
void (*const scope_on_exit_handlers[])(void*) = {
#include "scope_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers scope_scene_handlers = {
    .on_enter_handlers = scope_on_enter_handlers,
    .on_event_handlers = scope_on_event_handlers,
    .on_exit_handlers  = scope_on_exit_handlers,
    .scene_num         = ScopeSceneNum,
};
