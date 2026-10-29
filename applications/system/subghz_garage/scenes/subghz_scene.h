#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) SubGhzScene##id,
typedef enum {
#include "subghz_scene_config.h"
    SubGhzSceneNum,
} SubGhzScene;
#undef ADD_SCENE

extern const SceneManagerHandlers subghz_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "subghz_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "subghz_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "subghz_scene_config.h"
#undef ADD_SCENE
