#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) KarrPocScene_##id,
typedef enum {
#include "karr_poc_scene_config.h"
    KarrPocScene_count,
} KarrPocScene;
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id)          \
    void prefix##_scene_##name##_on_enter(void* context); \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "karr_poc_scene_config.h"
#undef ADD_SCENE
