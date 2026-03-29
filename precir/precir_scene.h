#pragma once

#include <gui/scene_manager.h>

/* ---- Scene IDs ---- */

typedef enum {
    PrecIRSceneMainMenu,
    PrecIRScenePLIDInput,
    PrecIRSceneDMConfig,
    PrecIRSceneImageSelect,
    PrecIRSceneSegmentConfig,
    PrecIRSceneTransmit,
    PrecIRSceneAbout,
    PrecIRSceneCount,
} PrecIRScene;

/* ---- Custom event IDs ---- */

typedef enum {
    /* Main menu choices */
    PrecIREventSendImage,
    PrecIREventSetSegments,
    PrecIREventAbout,
    /* Navigation / actions */
    PrecIREventPLIDEntered,
    PrecIREventConfigDone,
    PrecIREventImageSelected,
    PrecIREventStartTransmit,
    PrecIREventTransmitDone,
    PrecIREventBack,
} PrecIREvent;

/* ---- Scene handler declarations ---- */

/* Each scene implements on_enter, on_event, on_exit */
#define PRECIR_SCENE_DECL(name)                                                   \
    void precir_scene_##name##_on_enter(void* context);                           \
    bool precir_scene_##name##_on_event(void* context, SceneManagerEvent event);  \
    void precir_scene_##name##_on_exit(void* context);

PRECIR_SCENE_DECL(main_menu)
PRECIR_SCENE_DECL(plid_input)
PRECIR_SCENE_DECL(dm_config)
PRECIR_SCENE_DECL(image_select)
PRECIR_SCENE_DECL(segment_config)
PRECIR_SCENE_DECL(transmit)
PRECIR_SCENE_DECL(about)

/** Scene handler arrays (defined in precir_scene.c). */
extern const SceneManagerHandlers precir_scene_handlers;
