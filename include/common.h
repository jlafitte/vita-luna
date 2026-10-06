#ifndef VITA_LUNA_COMMON_H
#define VITA_LUNA_COMMON_H

#include <psp2/types.h>
#include <psp2/kernel/clib.h>

// PS Vita Native Screen Dimensions
#define VITA_SCREEN_WIDTH  960
#define VITA_SCREEN_HEIGHT 544

// Target Stream Configuration
#define LUNA_TARGET_WIDTH  1280
#define LUNA_TARGET_HEIGHT 720
#define LUNA_TARGET_FPS    60

// Application Version
#define VITA_LUNA_VERSION_STR "v0.1.0-alpha"

// Logging Macro
#ifdef DEBUG
    #define LOG_INFO(fmt, ...)  sceClibPrintf("[LUNA INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  sceClibPrintf("[LUNA WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) sceClibPrintf("[LUNA ERR]  " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_INFO(fmt, ...)  ((void)0)
    #define LOG_WARN(fmt, ...)  ((void)0)
    #define LOG_ERROR(fmt, ...) sceClibPrintf("[LUNA ERR]  " fmt "\n", ##__VA_ARGS__)
#endif

// System App States
enum AppState {
    STATE_MAIN_MENU,
    STATE_LOGIN_STUB,
    STATE_CATALOG,
    STATE_STREAM_ACTIVE,
    STATE_SETTINGS
};

#endif // VITA_LUNA_COMMON_H
