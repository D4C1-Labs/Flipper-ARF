#include "karr_poc_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const karr_poc_scene_on_enter_handlers[])(void*) = {
#include "karr_poc_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const karr_poc_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "karr_poc_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const karr_poc_scene_on_exit_handlers[])(void* context) = {
#include "karr_poc_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers karr_poc_scene_handlers = {
    .on_enter_handlers = karr_poc_scene_on_enter_handlers,
    .on_event_handlers = karr_poc_scene_on_event_handlers,
    .on_exit_handlers = karr_poc_scene_on_exit_handlers,
    .scene_num = KarrPocScene_count,
};
