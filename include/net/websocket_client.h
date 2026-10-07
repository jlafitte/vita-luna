#ifndef VITA_LUNA_WEBSOCKET_CLIENT_H
#define VITA_LUNA_WEBSOCKET_CLIENT_H

#include "common.h"
#include <string>
#include <vector>

enum WebSocketState {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_ERROR
};

class WebSocketClient {
public:
    static bool init();
    static void shutdown();

    static bool connect(const std::string& url);
    static void disconnect();

    static WebSocketState getState();
    static bool isConnected();

    static bool sendTextMessage(const std::string& text);
    static bool pollMessages(std::vector<std::string>& outMessages);

    static std::string getStatusMessage();

private:
    static bool m_initialized;
    static WebSocketState m_state;
    static std::string m_serverUrl;
    static std::string m_statusMessage;
};

#endif // VITA_LUNA_WEBSOCKET_CLIENT_H
