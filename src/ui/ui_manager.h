#ifndef VITA_LUNA_UI_MANAGER_H
#define VITA_LUNA_UI_MANAGER_H

#include "common.h"
#include <stdint.h>

struct MenuItem {
    const char* label;
    const char* description;
    AppState targetState;
};

class UIManager {
public:
    static void init();
    static void update(float deltaTime, uint32_t buttonsPressed);
    static void render();
    
    static AppState getCurrentState();
    static void setState(AppState newState);

private:
    static void renderMainMenu();
    static void renderLoginStub();
    static void renderCatalog();
    static void renderStreamActive();
    static void renderSettings();

    static AppState m_currentState;
    static int m_selectedMenuIndex;
    static int m_selectedCatalogIndex;
    static int m_selectedSettingIndex;
    
    // Setting Options
    static int m_targetBitrateMbps;
    static bool m_enableHardwareDecoder;
};

#endif // VITA_LUNA_UI_MANAGER_H
