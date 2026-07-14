#include "precir_app.h"

/* ---- Handler lookup macros ---- */

#define SCENE_ON_ENTER(name) precir_scene_##name##_on_enter
#define SCENE_ON_EVENT(name) precir_scene_##name##_on_event
#define SCENE_ON_EXIT(name)  precir_scene_##name##_on_exit

/* ---- on_enter handlers ---- */

static const AppSceneOnEnterCallback scene_on_enter[] = {
    SCENE_ON_ENTER(main_menu),
    SCENE_ON_ENTER(profile_list),
    SCENE_ON_ENTER(profile_actions),
    SCENE_ON_ENTER(plid_input),
    SCENE_ON_ENTER(dm_config),
    SCENE_ON_ENTER(image_select),
    SCENE_ON_ENTER(segment_config),
    SCENE_ON_ENTER(transmit),
    SCENE_ON_ENTER(calibrate),
    SCENE_ON_ENTER(about),
};

/* ---- on_event handlers ---- */

static const AppSceneOnEventCallback scene_on_event[] = {
    SCENE_ON_EVENT(main_menu),
    SCENE_ON_EVENT(profile_list),
    SCENE_ON_EVENT(profile_actions),
    SCENE_ON_EVENT(plid_input),
    SCENE_ON_EVENT(dm_config),
    SCENE_ON_EVENT(image_select),
    SCENE_ON_EVENT(segment_config),
    SCENE_ON_EVENT(transmit),
    SCENE_ON_EVENT(calibrate),
    SCENE_ON_EVENT(about),
};

/* ---- on_exit handlers ---- */

static const AppSceneOnExitCallback scene_on_exit[] = {
    SCENE_ON_EXIT(main_menu),
    SCENE_ON_EXIT(profile_list),
    SCENE_ON_EXIT(profile_actions),
    SCENE_ON_EXIT(plid_input),
    SCENE_ON_EXIT(dm_config),
    SCENE_ON_EXIT(image_select),
    SCENE_ON_EXIT(segment_config),
    SCENE_ON_EXIT(transmit),
    SCENE_ON_EXIT(calibrate),
    SCENE_ON_EXIT(about),
};

/* ---- Exported handler table ---- */

const SceneManagerHandlers precir_scene_handlers = {
    .on_enter_handlers = scene_on_enter,
    .on_event_handlers = scene_on_event,
    .on_exit_handlers = scene_on_exit,
    .scene_num = PrecIRSceneCount,
};
