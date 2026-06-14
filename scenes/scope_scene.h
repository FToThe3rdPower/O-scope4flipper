#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) prefix##Scene##id,
typedef enum {
#include "scope_scene_config.h"
    ScopeSceneNum,
} ScopeScene;
#undef ADD_SCENE

extern const SceneManagerHandlers scope_scene_handlers;

#define ADD_SCENE(prefix, name, id)                                        \
    void scope_scene_##name##_on_enter(void*);                             \
    bool scope_scene_##name##_on_event(void*, SceneManagerEvent);          \
    void scope_scene_##name##_on_exit(void*);
#include "scope_scene_config.h"
#undef ADD_SCENE
