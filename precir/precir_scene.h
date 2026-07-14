#pragma once

#include <gui/scene_manager.h>

/* ---- Scene IDs ---- */

typedef enum {
    PrecIRSceneMainMenu,
    PrecIRSceneProfileList,
    PrecIRSceneProfileActions,
    PrecIRScenePLIDInput,
    PrecIRSceneDMConfig,
    PrecIRSceneImageSelect,
    PrecIRSceneSegmentConfig,
    PrecIRSceneTransmit,
    PrecIRSceneCalibrate,
    PrecIRSceneAbout,
    PrecIRSceneCount,
} PrecIRScene;

/* ---- Custom event IDs ---- */

typedef enum {
    PrecIREventProfileList = 100,
    PrecIREventNewTag = 101,
    PrecIREventTestClear = 102,
    PrecIREventAbout = 103,
    PrecIREventPLIDEntered = 104,
    PrecIREventSendSavedImage = 105,
    PrecIREventChooseBMP = 106,
    PrecIREventClearSaved = 107,
    PrecIREventEditSettings = 108,
    PrecIREventSetSegments = 109,
    PrecIREventDeleteTag = 110,
    PrecIREventConfigDone = 111,
    PrecIREventTransmitDone = 112,
    PrecIREventCalibrationSave = 113,
    PrecIREventCalibrationNext = 114,
} PrecIREvent;

/* ---- Cross-scene operation state ---- */

typedef enum {
    PrecIRTransmitKindImage = 0,
    PrecIRTransmitKindClearSaved = 1,
    PrecIRTransmitKindTestClear = 2,
    PrecIRTransmitKindSegments = 3,
} PrecIRTransmitKind;

typedef enum {
    PrecIRCalibrationStateIdle = 0,
    PrecIRCalibrationStateSending = 1,
    PrecIRCalibrationStateWaiting = 2,
    PrecIRCalibrationStateReady = 3,
    PrecIRCalibrationStateError = 4,
} PrecIRCalibrationState;

/* ---- Scene handler declarations ---- */

/* Each scene implements on_enter, on_event, on_exit */
#define PRECIR_SCENE_DECL(name)                                                  \
    void precir_scene_##name##_on_enter(void* context);                          \
    bool precir_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void precir_scene_##name##_on_exit(void* context);

PRECIR_SCENE_DECL(main_menu)
PRECIR_SCENE_DECL(profile_list)
PRECIR_SCENE_DECL(profile_actions)
PRECIR_SCENE_DECL(plid_input)
PRECIR_SCENE_DECL(dm_config)
PRECIR_SCENE_DECL(image_select)
PRECIR_SCENE_DECL(segment_config)
PRECIR_SCENE_DECL(transmit)
PRECIR_SCENE_DECL(calibrate)
PRECIR_SCENE_DECL(about)

/** Scene handler arrays (defined in precir_scene.c). */
extern const SceneManagerHandlers precir_scene_handlers;
