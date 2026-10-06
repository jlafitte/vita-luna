#include "common.h"
#include "graphics/renderer.h"
#include "ui/ui_manager.h"
#include "ui/diagnostic_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__vita__)
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#endif

static bool g_running = true;

static void initVitaSystemModules() {
#if defined(__vita__)
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    
    // Set CPU clock to maximum performance (444 MHz) for low-latency streaming
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);

    // Initialize controller mode
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
#endif
    LOG_INFO("Vita system modules and power profile initialized.");
}

static void shutdownVitaSystemModules() {
#if defined(__vita__)
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
#endif
    LOG_INFO("Vita system modules unloaded.");
}

uint32_t readInputButtons() {
    uint32_t pressedButtons = 0;

#if defined(__vita__)
    static uint32_t lastButtons = 0;
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    pressedButtons = pad.buttons & ~lastButtons;
    lastButtons = pad.buttons;
#endif

    return pressedButtons;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    LOG_INFO("Starting Amazon Luna Client for PS Vita (%s)...", VITA_LUNA_VERSION_STR);

    initVitaSystemModules();

    if (!Renderer::init()) {
        LOG_ERROR("Failed to initialize graphics renderer!");
        shutdownVitaSystemModules();
        return -1;
    }

    UIManager::init();
    DiagnosticOverlay::init();

    // Primary Loop
    float deltaTime = 0.0166f; // ~60 FPS timing frame step

    while (g_running) {
        uint32_t input = readInputButtons();

        // Update UI State & Input
        UIManager::update(deltaTime, input);
        DiagnosticOverlay::update(deltaTime);

        // Render Frame
        Renderer::startFrame();
        
        UIManager::render();
        DiagnosticOverlay::render();
        
        Renderer::endFrame();

#if defined(__vita__)
        // Yield CPU slightly to prevent spinlock
        sceKernelDelayThread(1000); // 1ms
#endif
    }

    // Teardown
    Renderer::shutdown();
    shutdownVitaSystemModules();

    LOG_INFO("Application exited cleanly.");
#if defined(__vita__)
    sceKernelExitProcess(0);
#endif
    return 0;
}
