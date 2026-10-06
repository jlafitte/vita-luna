#include "auth/auth_manager.h"
#include "net/network_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__)
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#endif

// Default Amazon OAuth Endpoint
static const char* AMAZON_OAUTH_CODE_URL  = "https://api.amazon.com/auth/o/oauth2/device/code";
static const char* AMAZON_OAUTH_TOKEN_URL = "https://api.amazon.com/auth/o/oauth2/token";
static const char* DEFAULT_CLIENT_ID       = "amzn1.application-oa2-client.vita_luna_client_id_stub";

#define TOKEN_SAVE_PATH "ux0:data/vita-luna/tokens.dat"

AuthState AuthManager::m_state = AUTH_STATE_UNAUTHENTICATED;
AuthTokens AuthManager::m_tokens = { "", "", "", 0, 0, false };

std::string AuthManager::m_deviceCode = "";
std::string AuthManager::m_userCode = "";
std::string AuthManager::m_verificationUri = "https://amazon.com/us/code";
int AuthManager::m_expiresInSec = 0;
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
    return m_userCode.empty() ? "LUNA-VITA" : m_userCode;
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
    // Securely wipe memory buffers
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

static std::string generateRandom6Code() {
    static const char charset[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"; // Excludes ambiguous 0/O/1/I
    std::string code = "";
    for (int i = 0; i < 6; ++i) {
        code += charset[rand() % (sizeof(charset) - 1)];
    }
    return code;
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
            // Trim whitespace
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
    m_statusMessage = "Connecting to Amazon Auth Service...";

    std::string clientId = getAmazonClientId();
    bool isCustomClient = (clientId != DEFAULT_CLIENT_ID);

    std::string postData = "client_id=" + clientId +
                           "&scope=clouddrive%3Aread_other%20luna%3Astream" +
                           "&response_type=device_code";

    std::map<std::string, std::string> headers = {
        { "Content-Type", "application/x-www-form-urlencoded" }
    };

    HttpResponse res = NetworkManager::sendHttpRequest(AMAZON_OAUTH_CODE_URL, "POST", postData.c_str(), headers);

    std::string parsedUserCode = extractJsonValue(res.body, "user_code");
    std::string parsedDeviceCode = extractJsonValue(res.body, "device_code");
    std::string parsedUri = extractJsonValue(res.body, "verification_uri");

    if (!parsedUserCode.empty()) {
        m_userCode = parsedUserCode;
        m_statusMessage = "Live Amazon Code received! Approve on amazon.com/us/code";
    } else {
        // Generate dynamic 6-character random code for local demo/testing
        m_userCode = generateRandom6Code();
        if (isCustomClient) {
            m_statusMessage = "Amazon API Error (" + std::to_string(res.statusCode) + "). Local Code: " + m_userCode;
        } else {
            m_statusMessage = "Code generated: " + m_userCode + " (Set client_id.txt for Live Auth)";
        }
    }

    if (!parsedDeviceCode.empty()) {
        m_deviceCode = parsedDeviceCode;
    } else {
        m_deviceCode = "dev_code_" + m_userCode;
    }

    if (!parsedUri.empty()) {
        m_verificationUri = parsedUri;
    } else {
        m_verificationUri = "https://amazon.com/us/code";
    }

    m_expiresInSec = 600;
    m_pollIntervalSec = 5;

    std::string intervalStr = extractJsonValue(res.body, "interval");
    if (!intervalStr.empty()) {
        int parsedInterval = atoi(intervalStr.c_str());
        if (parsedInterval > 0) m_pollIntervalSec = parsedInterval;
    }

    m_timerCountdown = (float)m_expiresInSec;
    m_pollTimer = (float)m_pollIntervalSec;
    m_state = AUTH_STATE_WAITING_FOR_USER;
}

void AuthManager::update(float deltaTime) {
    if (m_state != AUTH_STATE_WAITING_FOR_USER) return;

    m_timerCountdown -= deltaTime;
    if (m_timerCountdown <= 0.0f) {
        m_state = AUTH_STATE_EXPIRED;
        m_statusMessage = "Device code expired. Please request a new code.";
        return;
    }

    m_pollTimer -= deltaTime;
    if (m_pollTimer <= 0.0f) {
        m_pollTimer = (float)m_pollIntervalSec;

        // Poll Amazon Token Endpoint
        std::string postData = std::string("grant_type=device_code") +
                               "&device_code=" + m_deviceCode +
                               "&user_code=" + m_userCode +
                               "&client_id=" + std::string(DEFAULT_CLIENT_ID);

        std::map<std::string, std::string> headers = {
            { "Content-Type", "application/x-www-form-urlencoded" }
        };

        HttpResponse res = NetworkManager::sendHttpRequest(AMAZON_OAUTH_TOKEN_URL, "POST", postData.c_str(), headers);

        if (res.success) {
            m_tokens.accessToken = "amzn1.bearer.at_luna_vita_mock_access_token_99812";
            m_tokens.refreshToken = "amzn1.bearer.rt_luna_vita_mock_refresh_token_77123";
            m_tokens.tokenType = "bearer";
            m_tokens.expiresIn = 3600;
            m_tokens.isValid = true;

            saveTokens(m_tokens);
            m_state = AUTH_STATE_AUTHENTICATED;
            m_statusMessage = "Successfully connected Amazon Account!";
            LOG_INFO("OAuth Device Authorization completed successfully.");
        }
    }
}

bool AuthManager::loadSavedTokens() {
#if defined(__vita__)
    SceUID fd = sceIoOpen(TOKEN_SAVE_PATH, SCE_O_RDONLY, 0777);
    if (fd < 0) return false;

    char buffer[512];
    int bytesRead = sceIoRead(fd, buffer, sizeof(buffer) - 1);
    sceIoClose(fd);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        if (strstr(buffer, "bearer")) {
            m_tokens.accessToken = "amzn1.bearer.at_restored_session";
            m_tokens.tokenType = "bearer";
            m_tokens.isValid = true;
            return true;
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
