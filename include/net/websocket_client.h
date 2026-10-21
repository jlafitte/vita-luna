#ifndef VITA_LUNA_WEBSOCKET_CLIENT_H
#define VITA_LUNA_WEBSOCKET_CLIENT_H

#include "common.h"
#include <string>
#include <vector>

enum WebSocketState {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_AUTHENTICATING,
    WS_STATE_OFFER_SENT,
    WS_STATE_ANSWER_RECEIVED,
    WS_STATE_ICE_EXCHANGE,
    WS_STATE_ACTIVE,
    WS_STATE_ERROR
};

class WebSocketClient {
public:
    static bool init();
    static void shutdown();

    static bool connect(const std::string& url);
    static void disconnect();

    static bool startSignalingSession(
        const std::string& url,
        const std::string& token,
        const std::string& gameId,
        int width = 1280,
        int height = 720,
        int fps = 60,
        int bitrateKbps = 10000
    );

    static void update(float deltaTime);

    static WebSocketState getState();
    static bool isConnected();
    static bool isSignalingComplete();

    static bool sendTextMessage(const std::string& text);
    static bool pollMessages(std::vector<std::string>& outMessages);

    static std::string getStatusMessage();
    static const std::string& getRemoteSdp();
    static const std::string& getSelectedIceCandidate();

private:
    static bool m_initialized;
    static WebSocketState m_state;
    static std::string m_serverUrl;
    static std::string m_statusMessage;

    static std::string m_gameId;
    static std::string m_lwaToken;
    static int m_targetWidth;
    static int m_targetHeight;
    static int m_targetFps;
    static int m_bitrateKbps;

    static float m_stateTimer;
    static std::string m_remoteSdp;
    static std::string m_selectedCandidate;

    static void advanceHandshake();
};

#endif // VITA_LUNA_WEBSOCKET_CLIENT_H
