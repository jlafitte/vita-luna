#include "auth/auth_manager.h"
#include "net/network_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__)
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#endif

#if __has_include("credentials.h")
#include "credentials.h"
#endif

#ifndef LUNA_AMAZON_CLIENT_ID
#define LUNA_AMAZON_CLIENT_ID "amzn1.application-oa2-client.bf3ab358c757423bb9140f5bd73f1a94"
#endif

// Default Amazon OAuth Endpoint
static const char* AMAZON_OAUTH_CODE_URL  = "https://api.amazon.com/auth/o2/create/codepair";
static const char* AMAZON_OAUTH_TOKEN_URL = "https://api.amazon.com/auth/o2/token";
static const char* DEFAULT_CLIENT_ID       = LUNA_AMAZON_CLIENT_ID;

#define TOKEN_SAVE_PATH "ux0:data/vita-luna/tokens.dat"

AuthState AuthManager::m_state = AUTH_STATE_UNAUTHENTICATED;
AuthTokens AuthManager::m_tokens = { "", "", "", 0, 0, false };

std::string AuthManager::m_deviceCode = "";
std::string AuthManager::m_userCode = "";
std::string AuthManager::m_verificationUri = "https://amazon.com/us/code";
int AuthManager::m_expiresInSec = 600;
int AuthManager::m_pollIntervalSec = 5;
float AuthManager::m_timerCountdown = 0.0f;
float AuthManager::m_pollTimer = 0.0f;
std::string AuthManager::m_statusMessage = "Unauthenticated";

static std::string extractJsonValue(const std::string& json, const std::string& key) {
    if (json.empty() || key.empty()) return "";
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        searchKey = "\"" + key + "\" :";
        pos = json.find(searchKey);
    }
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;

    size_t endPos = pos;
    while (endPos < json.length() && json[endPos] != '"' && json[endPos] != ',' && json[endPos] != '}' && json[endPos] != '\n' && json[endPos] != '\r') {
        endPos++;
    }

    std::string val = json.substr(pos, endPos - pos);
    while (!val.empty() && (val.back() == '"' || val.back() == ' ')) val.pop_back();
    return val;
}

void AuthManager::init() {
    clearMemoryTokens();
    if (loadSavedTokens()) {
        m_state = AUTH_STATE_AUTHENTICATED;
        m_statusMessage = "Authenticated (Session Restored)";
    } else {
        m_state = AUTH_STATE_UNAUTHENTICATED;
        m_statusMessage = "Unauthenticated";
    }
}

AuthState AuthManager::getAuthState() {
    return m_state;
}

std::string AuthManager::getUserCode() {
    return m_userCode.empty() ? "------" : m_userCode;
}

std::string AuthManager::getVerificationUri() {
    return m_verificationUri;
}

int AuthManager::getTimeRemainingSec() {
    return (int)m_timerCountdown;
}

std::string AuthManager::getStatusMessage() {
    return m_statusMessage;
}

bool AuthManager::isAuthenticated() {
    return m_state == AUTH_STATE_AUTHENTICATED && m_tokens.isValid;
}

std::string AuthManager::getAccessToken() {
    return m_tokens.accessToken;
}

void AuthManager::clearMemoryTokens() {
    m_tokens.accessToken.clear();
    m_tokens.refreshToken.clear();
    m_tokens.tokenType.clear();
    m_tokens.expiresIn = 0;
    m_tokens.createdTimestamp = 0;
    m_tokens.isValid = false;

    m_deviceCode.clear();
    m_userCode.clear();
}

void AuthManager::logout() {
    clearMemoryTokens();
    m_state = AUTH_STATE_UNAUTHENTICATED;
    m_statusMessage = "Logged out successfully";

#if defined(__vita__)
    sceIoRemove(TOKEN_SAVE_PATH);
#else
    remove("tokens.dat");
#endif
    LOG_INFO("User logged out and auth tokens removed.");
}

static std::string getAmazonClientId() {
#if defined(__vita__)
    SceUID fd = sceIoOpen("ux0:data/vita-luna/client_id.txt", SCE_O_RDONLY, 0777);
    if (fd >= 0) {
        char buf[256];
        int len = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        if (len > 0) {
            buf[len] = '\0';
            std::string id(buf);
            size_t end = id.find_first_of("\r\n ");
            if (end != std::string::npos) id = id.substr(0, end);
            if (!id.empty()) return id;
        }
    }
#endif
    return DEFAULT_CLIENT_ID;
}

void AuthManager::startDeviceAuth() {
    m_state = AUTH_STATE_REQUESTING_CODE;
    m_statusMessage = "Requesting live code from Amazon...";

    std::string clientId = getAmazonClientId();
    std::string postData = "client_id=" + clientId + "&scope=profile";

    std::map<std::string, std::string> headers = {
        { "Content-Type", "application/x-www-form-urlencoded" }
    };

    LOG_INFO("Requesting live Amazon device code pair from %s...", AMAZON_OAUTH_CODE_URL);
    HttpResponse res = NetworkManager::sendHttpRequest(AMAZON_OAUTH_CODE_URL, "POST", postData.c_str(), headers);

    std::string parsedUserCode = extractJsonValue(res.body, "user_code");
    std::string parsedDeviceCode = extractJsonValue(res.body, "device_code");
    std::string parsedUri = extractJsonValue(res.body, "verification_uri");

    if (!parsedUserCode.empty()) {
        m_userCode = parsedUserCode;
        m_deviceCode = parsedDeviceCode;
        m_verificationUri = !parsedUri.empty() ? parsedUri : "https://amazon.com/us/code";

        std::string intervalStr = extractJsonValue(res.body, "interval");
        int parsedInterval = !intervalStr.empty() ? atoi(intervalStr.c_str()) : 5;
        m_pollIntervalSec = (parsedInterval > 0) ? parsedInterval : 5;

        m_timerCountdown = 600.0f;
        m_pollTimer = (float)m_pollIntervalSec;
        m_state = AUTH_STATE_WAITING_FOR_USER;
        m_statusMessage = "Live Amazon Code: " + m_userCode + ". Approve on amazon.com/us/code";
        LOG_INFO("Received Amazon User Code: %s, Device Code: %s", m_userCode.c_str(), m_deviceCode.c_str());
    } else {
        m_statusMessage = "Failed to fetch Amazon code (HTTP " + std::to_string(res.statusCode) + "). Press X to retry.";
        m_state = AUTH_STATE_ERROR;
        LOG_ERROR("Failed to fetch Amazon codepair: %s", res.body.c_str());
    }
}

void AuthManager::update(float deltaTime) {
    if (m_state != AUTH_STATE_WAITING_FOR_USER) return;

    m_timerCountdown -= deltaTime;
    if (m_timerCountdown <= 0.0f) {
        m_state = AUTH_STATE_EXPIRED;
        m_statusMessage = "Device code expired. Press X to request a new code.";
        return;
    }

    m_pollTimer -= deltaTime;
    if (m_pollTimer <= 0.0f) {
        m_pollTimer = (float)m_pollIntervalSec;

        std::string clientId = getAmazonClientId();
        std::string postData = "grant_type=device_code"
                               "&client_id=" + clientId +
                               "&user_code=" + m_userCode +
                               "&device_code=" + m_deviceCode;

        std::map<std::string, std::string> headers = {
            { "Content-Type", "application/x-www-form-urlencoded" }
        };

        HttpResponse res = NetworkManager::sendHttpRequest(AMAZON_OAUTH_TOKEN_URL, "POST", postData.c_str(), headers);

        std::string accessToken = extractJsonValue(res.body, "access_token");
        std::string refreshToken = extractJsonValue(res.body, "refresh_token");

        if (!accessToken.empty()) {
            m_tokens.accessToken = accessToken;
            m_tokens.refreshToken = refreshToken;
            m_tokens.tokenType = "bearer";
            m_tokens.expiresIn = 3600;
            m_tokens.isValid = true;

            saveTokens(m_tokens);
            m_state = AUTH_STATE_AUTHENTICATED;
            m_statusMessage = "Successfully connected Amazon Account!";
            LOG_INFO("Amazon OAuth Device Authorization completed successfully!");
        } else {
            std::string errStr = extractJsonValue(res.body, "error");
            if (errStr == "authorization_pending") {
                m_statusMessage = "Polling Amazon... Open amazon.com/us/code & enter " + m_userCode;
            } else if (errStr == "slow_down") {
                m_pollIntervalSec += 5;
                m_statusMessage = "Slow down polling (" + std::to_string(m_pollIntervalSec) + "s). Enter " + m_userCode;
            } else if (!errStr.empty()) {
                m_statusMessage = "Waiting for approval (" + errStr + "). Code: " + m_userCode;
            } else {
                m_statusMessage = "Polling Amazon Auth Service (HTTP " + std::to_string(res.statusCode) + ")...";
            }
        }
    }
}

bool AuthManager::loadSavedTokens() {
#if defined(__vita__)
    SceUID fd = sceIoOpen(TOKEN_SAVE_PATH, SCE_O_RDONLY, 0777);
    if (fd < 0) return false;

    char buffer[1024];
    int bytesRead = sceIoRead(fd, buffer, sizeof(buffer) - 1);
    sceIoClose(fd);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string data(buffer);
        size_t tokenPos = data.find("access_token=");
        if (tokenPos != std::string::npos) {
            tokenPos += 13;
            size_t endPos = data.find_first_of("\r\n", tokenPos);
            std::string token = (endPos != std::string::npos) ? data.substr(tokenPos, endPos - tokenPos) : data.substr(tokenPos);
            if (!token.empty()) {
                m_tokens.accessToken = token;
                m_tokens.tokenType = "bearer";
                m_tokens.isValid = true;
                LOG_INFO("Successfully restored Amazon access token from disk.");
                return true;
            }
        }
    }
#endif
    return false;
}

bool AuthManager::saveTokens(const AuthTokens& tokens) {
    if (!tokens.isValid) return false;

#if defined(__vita__)
    sceIoMkdir("ux0:data/vita-luna", 0777);

    SceUID fd = sceIoOpen(TOKEN_SAVE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return false;

    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "token_type=%s\naccess_token=%s\n",
                       tokens.tokenType.c_str(), tokens.accessToken.c_str());
    sceIoWrite(fd, buffer, len);
    sceIoClose(fd);
    return true;
#else
    (void)tokens;
    return true;
#endif
}
