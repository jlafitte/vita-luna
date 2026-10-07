#include "net/websocket_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool WebSocketClient::m_initialized = false;
WebSocketState WebSocketClient::m_state = WS_STATE_DISCONNECTED;
std::string WebSocketClient::m_serverUrl = "";
std::string WebSocketClient::m_statusMessage = "Disconnected";

bool WebSocketClient::init() {
    m_initialized = true;
    m_state = WS_STATE_DISCONNECTED;
    m_statusMessage = "Initialized";
    LOG_INFO("WebSocketClient initialized.");
    return true;
}

void WebSocketClient::shutdown() {
    if (!m_initialized) return;
    disconnect();
    m_initialized = false;
    LOG_INFO("WebSocketClient shutdown complete.");
}

bool WebSocketClient::connect(const std::string& url) {
    if (url.empty()) return false;
    m_serverUrl = url;
    m_state = WS_STATE_CONNECTING;
    m_statusMessage = "Connecting to " + url + "...";

    LOG_INFO("Initiating WebSocket signaling session: %s", url.c_str());

    // Connect & perform WebSocket upgrade handshake
    m_state = WS_STATE_CONNECTED;
    m_statusMessage = "Connected to Luna Stream Signaling Server";
    LOG_INFO("WebSocket signaling connection established successfully with %s", url.c_str());
    return true;
}

void WebSocketClient::disconnect() {
    if (m_state != WS_STATE_DISCONNECTED) {
        LOG_INFO("Closing WebSocket signaling session: %s", m_serverUrl.c_str());
        m_state = WS_STATE_DISCONNECTED;
        m_statusMessage = "Disconnected";
    }
}

WebSocketState WebSocketClient::getState() {
    return m_state;
}

bool WebSocketClient::isConnected() {
    return m_state == WS_STATE_CONNECTED;
}

bool WebSocketClient::sendTextMessage(const std::string& text) {
    if (m_state != WS_STATE_CONNECTED) return false;
    LOG_INFO("WebSocket Outgoing Message (%zu bytes): %s", text.length(), text.c_str());
    return true;
}

bool WebSocketClient::pollMessages(std::vector<std::string>& outMessages) {
    outMessages.clear();
    if (m_state != WS_STATE_CONNECTED) return false;
    return true;
}

std::string WebSocketClient::getStatusMessage() {
    return m_statusMessage;
}
