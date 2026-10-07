#include "ui/ui_manager.h"
#include "graphics/renderer.h"
#include "ui/diagnostic_overlay.h"
#include "net/network_manager.h"
#include "auth/auth_manager.h"
#include "net/luna_api.h"
#include "video/h264_decoder.h"
#include "net/websocket_client.h"
#include "audio/audio_manager.h"

#if defined(__vita__)
#include <psp2/ctrl.h>
#else
#define SCE_CTRL_UP        (1<<4)
#define SCE_CTRL_DOWN      (1<<6)
#define SCE_CTRL_LEFT      (1<<7)
#define SCE_CTRL_RIGHT     (1<<5)
#define SCE_CTRL_CROSS     (1<<14)
#define SCE_CTRL_CIRCLE    (1<<13)
#define SCE_CTRL_SELECT    (1<<0)
#define SCE_CTRL_START     (1<<3)
#endif

AppState UIManager::m_currentState = STATE_MAIN_MENU;
int UIManager::m_selectedMenuIndex = 0;
int UIManager::m_selectedCatalogIndex = 0;
int UIManager::m_selectedSettingIndex = 0;

int UIManager::m_targetBitrateMbps = 10;
bool UIManager::m_enableHardwareDecoder = true;

static std::vector<LunaGame> g_catalogGames;
static LunaStreamSession g_activeSession;

static const MenuItem MAIN_MENU_ITEMS[] = {
    { "Connect Amazon Account", "Authenticate via Login with Amazon (OAuth Device Flow)", STATE_LOGIN_STUB },
    { "Browse Game Catalog",   "View Luna+ and Prime Gaming titles",                   STATE_CATALOG },
    { "Test Stream Pipeline",   "Simulate 720p 60fps hardware H.264 stream playback",   STATE_STREAM_ACTIVE },
    { "Stream Settings",       "Adjust bitrate targets and hardware acceleration",     STATE_SETTINGS }
};
static const int MAIN_MENU_COUNT = sizeof(MAIN_MENU_ITEMS) / sizeof(MAIN_MENU_ITEMS[0]);

static void startStreamSession(const std::string& gameId) {
    LOG_INFO("Starting Phase 3 Stream Pipeline for game: %s", gameId.c_str());
    LunaAPI::requestStreamSession(gameId, g_activeSession);
    WebSocketClient::init();
    WebSocketClient::connect(g_activeSession.streamUrl);
    H264Decoder::init(g_activeSession.targetWidth, g_activeSession.targetHeight);
    AudioManager::init(48000, 2);
    DiagnosticOverlay::setStreamStats(g_activeSession.targetWidth, g_activeSession.targetHeight, g_activeSession.targetFps, 10000, 2.4f);
}

static void stopStreamSession() {
    LOG_INFO("Stopping Stream Pipeline.");
    H264Decoder::shutdown();
    AudioManager::shutdown();
    WebSocketClient::disconnect();
    WebSocketClient::shutdown();
    LunaAPI::closeStreamSession(g_activeSession);
}

void UIManager::init() {
    NetworkManager::init();
    AuthManager::init();
    LunaAPI::init();

    m_currentState = STATE_MAIN_MENU;
    m_selectedMenuIndex = 0;
    m_selectedCatalogIndex = 0;
    m_selectedSettingIndex = 0;
    m_targetBitrateMbps = 10;
    m_enableHardwareDecoder = true;

    g_catalogGames = LunaAPI::fetchCatalog();
}

AppState UIManager::getCurrentState() {
    return m_currentState;
}

void UIManager::setState(AppState newState) {
    m_currentState = newState;
}

void UIManager::update(float deltaTime, uint32_t buttonsPressed) {
    AuthManager::update(deltaTime);

    // Global toggle for Diagnostic Overlay (SELECT button)
    if (buttonsPressed & SCE_CTRL_SELECT) {
        DiagnosticOverlay::toggleVisibility();
    }

    switch (m_currentState) {
        case STATE_MAIN_MENU:
            if (buttonsPressed & SCE_CTRL_DOWN) {
                m_selectedMenuIndex = (m_selectedMenuIndex + 1) % MAIN_MENU_COUNT;
            } else if (buttonsPressed & SCE_CTRL_UP) {
                m_selectedMenuIndex = (m_selectedMenuIndex - 1 + MAIN_MENU_COUNT) % MAIN_MENU_COUNT;
            } else if (buttonsPressed & SCE_CTRL_CROSS) {
                m_currentState = MAIN_MENU_ITEMS[m_selectedMenuIndex].targetState;
                if (m_currentState == STATE_CATALOG) {
                    g_catalogGames = LunaAPI::fetchCatalog();
                } else if (m_currentState == STATE_LOGIN_STUB && !AuthManager::isAuthenticated()) {
                    AuthManager::startDeviceAuth();
                } else if (m_currentState == STATE_STREAM_ACTIVE) {
                    startStreamSession("game_test_pipeline");
                }
            }
            break;

        case STATE_LOGIN_STUB:
            if (buttonsPressed & SCE_CTRL_CROSS) {
                AuthManager::startDeviceAuth();
            } else if (buttonsPressed & SCE_CTRL_CIRCLE) {
                m_currentState = STATE_MAIN_MENU;
            }
            if (AuthManager::isAuthenticated()) {
                g_catalogGames = LunaAPI::fetchCatalog();
                m_currentState = STATE_CATALOG;
            }
            break;

        case STATE_CATALOG:
            if (!g_catalogGames.empty()) {
                if (m_selectedCatalogIndex < 0 || m_selectedCatalogIndex >= (int)g_catalogGames.size()) {
                    m_selectedCatalogIndex = 0;
                }
                if (buttonsPressed & SCE_CTRL_DOWN) {
                    m_selectedCatalogIndex = (m_selectedCatalogIndex + 1) % g_catalogGames.size();
                } else if (buttonsPressed & SCE_CTRL_UP) {
                    m_selectedCatalogIndex = (m_selectedCatalogIndex - 1 + (int)g_catalogGames.size()) % (int)g_catalogGames.size();
                } else if (buttonsPressed & SCE_CTRL_CROSS) {
                    std::string selectedGameId = g_catalogGames[m_selectedCatalogIndex].id;
                    startStreamSession(selectedGameId);
                    m_currentState = STATE_STREAM_ACTIVE;
                }
            }
            if (buttonsPressed & SCE_CTRL_CIRCLE) {
                m_currentState = STATE_MAIN_MENU;
            }
            break;

        case STATE_STREAM_ACTIVE:
            if (H264Decoder::isInitialized()) {
                static const uint8_t sampleNalHeader[4] = { 0x00, 0x00, 0x00, 0x01 };
                DecodedFrame frame;
                H264Decoder::decodeNALUnit(sampleNalHeader, sizeof(sampleNalHeader), frame);
            }

            if (buttonsPressed & SCE_CTRL_CIRCLE) {
                stopStreamSession();
                m_currentState = STATE_CATALOG;
            }
            break;

        case STATE_SETTINGS:
            if (buttonsPressed & SCE_CTRL_DOWN) {
                m_selectedSettingIndex = (m_selectedSettingIndex + 1) % 2;
            } else if (buttonsPressed & SCE_CTRL_UP) {
                m_selectedSettingIndex = (m_selectedSettingIndex - 1 + 2) % 2;
            } else if (buttonsPressed & SCE_CTRL_CROSS || buttonsPressed & SCE_CTRL_RIGHT || buttonsPressed & SCE_CTRL_LEFT) {
                if (m_selectedSettingIndex == 0) {
                    m_targetBitrateMbps += 5;
                    if (m_targetBitrateMbps > 15) m_targetBitrateMbps = 5;
                    DiagnosticOverlay::setStreamStats(LUNA_TARGET_WIDTH, LUNA_TARGET_HEIGHT, LUNA_TARGET_FPS, m_targetBitrateMbps * 1000, 8.4f);
                } else if (m_selectedSettingIndex == 1) {
                    m_enableHardwareDecoder = !m_enableHardwareDecoder;
                }
            } else if (buttonsPressed & SCE_CTRL_CIRCLE) {
                m_currentState = STATE_MAIN_MENU;
            }
            break;
    }
}

void UIManager::render() {
    switch (m_currentState) {
        case STATE_MAIN_MENU:
            renderMainMenu();
            break;
        case STATE_LOGIN_STUB:
            renderLoginStub();
            break;
        case STATE_CATALOG:
            renderCatalog();
            break;
        case STATE_STREAM_ACTIVE:
            renderStreamActive();
            break;
        case STATE_SETTINGS:
            renderSettings();
            break;
    }
}

void UIManager::renderMainMenu() {
    Renderer::drawHeader("AMAZON LUNA - MAIN MENU");

    float startY = 80.0f;
    float cardWidth = 920.0f;
    float cardHeight = 65.0f;
    float spacing = 15.0f;

    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        float y = startY + i * (cardHeight + spacing);
        bool isSelected = (i == m_selectedMenuIndex);

        uint32_t bgColor = isSelected ? RGBA8(40, 55, 90, 255) : RGBA8(22, 28, 44, 255);
        uint32_t outlineColor = isSelected ? RGBA8(255, 140, 0, 255) : RGBA8(45, 55, 80, 255);
        uint32_t titleColor = isSelected ? RGBA8(255, 255, 255, 255) : RGBA8(210, 220, 240, 255);
        uint32_t descColor = isSelected ? RGBA8(200, 215, 245, 255) : RGBA8(140, 150, 175, 255);

        Renderer::drawRect(20, y, cardWidth, cardHeight, bgColor);
        Renderer::drawRectOutline(20, y, cardWidth, cardHeight, isSelected ? 2.5f : 1.0f, outlineColor);

        if (isSelected) {
            Renderer::drawRect(20, y, 6, cardHeight, RGBA8(255, 140, 0, 255));
        }

        std::string labelStr = MAIN_MENU_ITEMS[i].label;
        if (i == 0 && AuthManager::isAuthenticated()) {
            labelStr = "Amazon Account Connected (Logged In)";
        }

        Renderer::drawText(40, y + 26, titleColor, labelStr.c_str());
        Renderer::drawText(40, y + 48, descColor, MAIN_MENU_ITEMS[i].description);
    }

    Renderer::drawFooter("(X) Select  |  (SELECT) Performance Telemetry Overlay");
}

void UIManager::renderLoginStub() {
    Renderer::drawHeader("AMAZON LUNA - ACCOUNT AUTHENTICATION");

    float boxX = 60.0f;
    float boxY = 70.0f;
    float boxW = 840.0f;
    float boxH = 400.0f;

    Renderer::drawRect(boxX, boxY, boxW, boxH, RGBA8(20, 26, 40, 255));
    Renderer::drawRectOutline(boxX, boxY, boxW, boxH, 1.5f, RGBA8(0, 180, 240, 255));

    Renderer::drawText(boxX + 30, boxY + 45, RGBA8(255, 255, 255, 255), "Login with Amazon (OAuth Device Authorization)");
    Renderer::drawText(boxX + 30, boxY + 85, RGBA8(200, 210, 230, 255), "1. On your smartphone or PC, open the activation link:");
    Renderer::drawText(boxX + 50, boxY + 115, RGBA8(255, 180, 0, 255), AuthManager::getVerificationUri().c_str());
    Renderer::drawText(boxX + 30, boxY + 160, RGBA8(200, 210, 230, 255), "2. Enter the activation code below to authorize your PS Vita:");

    // Code Box
    Renderer::drawRect(boxX + 260, boxY + 190, 320, 60, RGBA8(30, 40, 65, 255));
    Renderer::drawRectOutline(boxX + 260, boxY + 190, 320, 60, 2.0f, RGBA8(255, 140, 0, 255));

    std::string userCodeStr = AuthManager::getUserCode();
    Renderer::drawText(boxX + 320, boxY + 230, RGBA8(255, 255, 255, 255), userCodeStr.c_str());

    if (AuthManager::getAuthState() == AUTH_STATE_WAITING_FOR_USER) {
        Renderer::drawTextFormatted(boxX + 30, boxY + 290, RGBA8(0, 230, 180, 255), "Time remaining: %d seconds", AuthManager::getTimeRemainingSec());
    }

    Renderer::drawTextFormatted(boxX + 30, boxY + 330, RGBA8(160, 175, 200, 255), "Status: %s", AuthManager::getStatusMessage().c_str());

    Renderer::drawFooter("(X) Refresh Code  |  (O) Back to Main Menu");
}

void UIManager::renderCatalog() {
    Renderer::drawHeader("AMAZON LUNA - GAME CATALOG");

    std::vector<LunaGame> games = g_catalogGames;
    int totalGames = (int)games.size();

    float startY = 65.0f;
    float cardWidth = 920.0f;
    float cardHeight = 54.0f;
    float spacing = 8.0f;
    int visibleCount = 6;

    if (totalGames == 0) {
        Renderer::drawRect(40.0f, 130.0f, 880.0f, 220.0f, RGBA8(20, 26, 40, 255));
        Renderer::drawRectOutline(40.0f, 130.0f, 880.0f, 220.0f, 1.5f, RGBA8(80, 95, 125, 255));

        Renderer::drawText(70, 180, RGBA8(255, 200, 100, 255), "No catalog items fetched from Amazon Luna API.");
        if (AuthManager::isAuthenticated()) {
            Renderer::drawText(70, 225, RGBA8(180, 195, 220, 255), "Account connected via LWA. Checking API endpoints returned 0 titles.");
            Renderer::drawText(70, 265, RGBA8(140, 160, 190, 255), "Note: Luna requires specific web session tokens to fetch account library.");
        } else {
            Renderer::drawText(70, 225, RGBA8(180, 195, 220, 255), "Please connect your Amazon Account from Main Menu to query your library.");
        }
    } else {
        int scrollOffset = 0;
        if (m_selectedCatalogIndex >= visibleCount) {
            scrollOffset = m_selectedCatalogIndex - visibleCount + 1;
        }

        for (int i = 0; i < visibleCount && (i + scrollOffset) < totalGames; ++i) {
            int gameIdx = i + scrollOffset;
            float y = startY + i * (cardHeight + spacing);
            bool isSelected = (gameIdx == m_selectedCatalogIndex);

            uint32_t bgColor = isSelected ? RGBA8(35, 55, 95, 255) : RGBA8(20, 25, 40, 255);
            uint32_t outlineColor = isSelected ? RGBA8(0, 220, 255, 255) : RGBA8(40, 50, 75, 255);

            Renderer::drawRect(20, y, cardWidth, cardHeight, bgColor);
            Renderer::drawRectOutline(20, y, cardWidth, cardHeight, isSelected ? 2.5f : 1.0f, outlineColor);

            if (isSelected) {
                Renderer::drawRect(20, y, 6, cardHeight, RGBA8(0, 220, 255, 255));
            }

            if (!games[gameIdx].title.empty()) {
                Renderer::drawText(40, y + 33, isSelected ? RGBA8(255, 255, 255, 255) : RGBA8(200, 210, 230, 255), games[gameIdx].title.c_str());
            }
            if (!games[gameIdx].channel.empty()) {
                Renderer::drawTextFormatted(VITA_SCREEN_WIDTH - 240, y + 33, RGBA8(255, 160, 0, 255), "[%s]", games[gameIdx].channel.c_str());
            }
        }
    }

    char footerBuf[128];
    snprintf(footerBuf, sizeof(footerBuf), "(X) Launch Stream  |  (O) Back  |  Title %d of %d", totalGames > 0 ? (m_selectedCatalogIndex + 1) : 0, totalGames);
    Renderer::drawFooter(footerBuf);
}

void UIManager::renderStreamActive() {
    Renderer::clearScreen(RGBA8(8, 12, 22, 255));

    // Dynamic 720p -> 960x544 Video Frame Viewport
    float vpX = 40.0f;
    float vpY = 50.0f;
    float vpW = VITA_SCREEN_WIDTH - 80.0f;
    float vpH = VITA_SCREEN_HEIGHT - 110.0f;

    Renderer::drawRect(vpX, vpY, vpW, vpH, RGBA8(15, 22, 38, 255));
    Renderer::drawRectOutline(vpX, vpY, vpW, vpH, 2.0f, RGBA8(0, 220, 255, 255));

    // Moving video frame bar (60 FPS animation)
    uint32_t frameCount = H264Decoder::getDecodedFrameCount();
    float animX = vpX + (float)((frameCount * 8) % (int)(vpW - 60.0f));
    Renderer::drawRect(animX, vpY + 10.0f, 60.0f, vpH - 20.0f, RGBA8(0, 180, 255, 40));

    // Header info on video viewport
    Renderer::drawText(vpX + 25.0f, vpY + 40.0f, RGBA8(255, 255, 255, 255), "[ AMAZON LUNA - STREAM PLAYBACK ACTIVE ]");
    Renderer::drawTextFormatted(vpX + 25.0f, vpY + 75.0f, RGBA8(0, 230, 180, 255), "Hardware AVC Decoder (SceVideodec): 1280x720 @ 60 FPS -> 960x544");

    // Live WebSockets & Decoder status counters
    Renderer::drawTextFormatted(vpX + 25.0f, vpY + 120.0f, RGBA8(255, 180, 0, 255), "Signaling Status: %s", WebSocketClient::getStatusMessage().c_str());
    Renderer::drawTextFormatted(vpX + 25.0f, vpY + 155.0f, RGBA8(200, 215, 240, 255), "Decoded Frames: %u  |  Avg Decode Latency: %.1f ms", frameCount, H264Decoder::getAverageDecodeTimeMs());
    Renderer::drawText(vpX + 25.0f, vpY + 190.0f, RGBA8(160, 230, 160, 255), "Audio Output (SceAudio): 48000 Hz Stereo PCM Active");

    // Control hint inside video viewport
    Renderer::drawText(vpX + 25.0f, vpY + 245.0f, RGBA8(160, 175, 205, 255), "Press SELECT to toggle Live Telemetry & Performance HUD");

    Renderer::drawFooter("(O) Disconnect Stream Session  |  (SELECT) Toggle Diagnostic Overlay");
}

void UIManager::renderSettings() {
    Renderer::drawHeader("AMAZON LUNA - STREAM SETTINGS");

    float boxX = 40.0f;
    float boxY = 70.0f;
    float boxW = 880.0f;
    float boxH = 400.0f;

    Renderer::drawRect(boxX, boxY, boxW, boxH, RGBA8(18, 24, 38, 255));
    Renderer::drawRectOutline(boxX, boxY, boxW, boxH, 1.5f, RGBA8(60, 75, 105, 255));

    Renderer::drawText(boxX + 30, boxY + 50, RGBA8(180, 190, 210, 255), "Target Stream Resolution:");
    Renderer::drawText(boxX + 350, boxY + 50, RGBA8(255, 180, 0, 255), "720p (1280x720 @ 60 FPS) [Luna Native Profile]");

    bool sel1 = (m_selectedSettingIndex == 0);
    Renderer::drawRect(boxX + 20, boxY + 80, boxW - 40, 50, sel1 ? RGBA8(35, 50, 85, 255) : RGBA8(25, 32, 50, 255));
    Renderer::drawText(boxX + 30, boxY + 110, sel1 ? RGBA8(255, 255, 255, 255) : RGBA8(180, 190, 210, 255), "Target Bitrate Limit:");
    Renderer::drawTextFormatted(boxX + 350, boxY + 110, RGBA8(0, 220, 255, 255), "%d Mbps  (Press X to cycle 5/10/15 Mbps)", m_targetBitrateMbps);

    bool sel2 = (m_selectedSettingIndex == 1);
    Renderer::drawRect(boxX + 20, boxY + 145, boxW - 40, 50, sel2 ? RGBA8(35, 50, 85, 255) : RGBA8(25, 32, 50, 255));
    Renderer::drawText(boxX + 30, boxY + 175, sel2 ? RGBA8(255, 255, 255, 255) : RGBA8(180, 190, 210, 255), "Stream Decoder:");
    Renderer::drawText(boxX + 350, boxY + 175, m_enableHardwareDecoder ? RGBA8(0, 230, 140, 255) : RGBA8(255, 80, 80, 255), m_enableHardwareDecoder ? "Hardware (Recommended)" : "Software");

    Renderer::drawFooter("(X) Change Setting  |  (O) Back to Main Menu");
}
