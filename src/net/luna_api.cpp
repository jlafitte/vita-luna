#include "net/luna_api.h"
#include "net/network_manager.h"
#include "auth/auth_manager.h"
#include <stdio.h>

bool LunaAPI::m_initialized = false;

bool LunaAPI::init() {
    m_initialized = true;
    LOG_INFO("LunaAPI backend interface initialized.");
    return true;
}

std::vector<LunaGame> LunaAPI::fetchCatalog() {
    std::vector<LunaGame> catalog;

    // Default entitlement titles (Luna+ / Prime Gaming)
    catalog.push_back({ "game_control", "Control: Ultimate Edition", "Luna+", "505 Games", true, "" });
    catalog.push_back({ "game_re2", "Resident Evil 2 (Remake)", "Luna+", "Capcom", true, "" });
    catalog.push_back({ "game_fortnite", "Fortnite", "Prime Gaming", "Epic Games", true, "" });
    catalog.push_back({ "game_metro", "Metro Exodus", "Luna+", "Deep Silver", true, "" });
    catalog.push_back({ "game_sonic", "Sonic Mania", "Luna+", "SEGA", true, "" });
    catalog.push_back({ "game_trackmania", "Trackmania", "Ubisoft+", "Ubisoft", true, "" });
    catalog.push_back({ "game_jackbox", "The Jackbox Party Pack 9", "Jackbox Channel", "Jackbox Games", true, "" });

    if (AuthManager::isAuthenticated()) {
        std::map<std::string, std::string> headers = {
            { "Authorization", "Bearer " + AuthManager::getAccessToken() },
            { "Accept", "application/json" }
        };
        // Perform HTTP request to query live Luna channels if online
        NetworkManager::sendHttpRequest("https://api.luna.amazon.com/v1/channels/entitlements", "GET", nullptr, headers);
    }

    return catalog;
}

bool LunaAPI::requestStreamSession(const std::string& gameId, LunaStreamSession& outSession) {
    LOG_INFO("Requesting Luna stream session for game: %s", gameId.c_str());

    outSession.sessionId = "session_luna_" + gameId + "_720p60";
    outSession.streamUrl = "wss://stream.luna.amazon.com/webrtc/v1/session";
    outSession.rtpHost = "stream-us-east-1.luna.amazon.com";
    outSession.rtpVideoPort = 5004;
    outSession.rtpAudioPort = 5006;
    outSession.codec = "H.264 (AVC High Profile)";
    outSession.targetWidth = LUNA_TARGET_WIDTH;
    outSession.targetHeight = LUNA_TARGET_HEIGHT;
    outSession.targetFps = LUNA_TARGET_FPS;
    outSession.isConnected = true;

    return true;
}

void LunaAPI::closeStreamSession(LunaStreamSession& session) {
    if (session.isConnected) {
        LOG_INFO("Closing Luna stream session: %s", session.sessionId.c_str());
        session.isConnected = false;
    }
}
