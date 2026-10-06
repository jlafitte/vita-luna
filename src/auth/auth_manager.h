#ifndef VITA_LUNA_AUTH_MANAGER_H
#define VITA_LUNA_AUTH_MANAGER_H

#include "common.h"
#include <string>

enum AuthState {
    AUTH_STATE_UNAUTHENTICATED,
    AUTH_STATE_REQUESTING_CODE,
    AUTH_STATE_WAITING_FOR_USER,
    AUTH_STATE_AUTHENTICATED,
    AUTH_STATE_EXPIRED,
    AUTH_STATE_ERROR
};

struct AuthTokens {
    std::string accessToken;
    std::string refreshToken;
    std::string tokenType;
    int expiresIn;
    int createdTimestamp;
    bool isValid;
};

class AuthManager {
public:
    static void init();
    static void update(float deltaTime);

    static AuthState getAuthState();
    static std::string getUserCode();
    static std::string getVerificationUri();
    static int getTimeRemainingSec();
    static std::string getStatusMessage();

    static void startDeviceAuth();
    static void logout();
    
    static bool isAuthenticated();
    static std::string getAccessToken();

private:
    static bool loadSavedTokens();
    static bool saveTokens(const AuthTokens& tokens);
    static void clearMemoryTokens();

    static AuthState m_state;
    static AuthTokens m_tokens;
    
    static std::string m_deviceCode;
    static std::string m_userCode;
    static std::string m_verificationUri;
    static int m_expiresInSec;
    static int m_pollIntervalSec;
    static float m_timerCountdown;
    static float m_pollTimer;
    static std::string m_statusMessage;
};

#endif // VITA_LUNA_AUTH_MANAGER_H
