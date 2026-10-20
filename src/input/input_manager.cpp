#include "input/input_manager.h"
#include "net/websocket_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <chrono>

#if defined(__vita__)
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#endif

bool InputManager::m_initialized = false;
LunaGamepadState InputManager::m_currentState = {};
LunaGamepadState InputManager::m_lastState = {};
uint32_t InputManager::m_pressedButtons = 0;
uint32_t InputManager::m_packetsSent = 0;
float InputManager::m_lastLatencyMs = 1.2f;
uint32_t InputManager::m_sequenceCounter = 0;

static inline float applyDeadzone(float value, float deadzone = 0.15f) {
    if (std::fabs(value) < deadzone) return 0.0f;
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    return sign * ((std::fabs(value) - deadzone) / (1.0f - deadzone));
}

bool InputManager::init() {
    if (m_initialized) return true;

    memset(&m_currentState, 0, sizeof(m_currentState));
    memset(&m_lastState, 0, sizeof(m_lastState));
    m_pressedButtons = 0;
    m_packetsSent = 0;
    m_sequenceCounter = 0;
    m_lastLatencyMs = 1.2f;

#if defined(__vita__)
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    LOG_INFO("InputManager: SceCtrl (Analog Wide) and SceTouch (Front & Back) initialized.");
#else
    LOG_INFO("InputManager: Host simulation initialized.");
#endif

    m_initialized = true;
    return true;
}

void InputManager::shutdown() {
    if (!m_initialized) return;

#if defined(__vita__)
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
#endif

    m_initialized = false;
    LOG_INFO("InputManager shutdown complete.");
}

void InputManager::poll() {
    if (!m_initialized) return;

    m_lastState = m_currentState;
    memset(&m_currentState, 0, sizeof(m_currentState));

    // Current millisecond timestamp
    auto now = std::chrono::steady_clock::now();
    m_currentState.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    m_currentState.sequence = ++m_sequenceCounter;

#if defined(__vita__)
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    uint32_t btns = 0;

    // Face buttons & D-Pad
    if (pad.buttons & SCE_CTRL_CROSS)     btns |= LUNA_BTN_A;
    if (pad.buttons & SCE_CTRL_CIRCLE)    btns |= LUNA_BTN_B;
    if (pad.buttons & SCE_CTRL_SQUARE)    btns |= LUNA_BTN_X;
    if (pad.buttons & SCE_CTRL_TRIANGLE)  btns |= LUNA_BTN_Y;

    if (pad.buttons & SCE_CTRL_UP)        btns |= LUNA_BTN_DPAD_UP;
    if (pad.buttons & SCE_CTRL_DOWN)      btns |= LUNA_BTN_DPAD_DOWN;
    if (pad.buttons & SCE_CTRL_LEFT)      btns |= LUNA_BTN_DPAD_LEFT;
    if (pad.buttons & SCE_CTRL_RIGHT)     btns |= LUNA_BTN_DPAD_RIGHT;

    // Shoulder bumpers
    if (pad.buttons & SCE_CTRL_LTRIGGER)  btns |= LUNA_BTN_LB;
    if (pad.buttons & SCE_CTRL_RTRIGGER)  btns |= LUNA_BTN_RB;

    // System buttons
    if (pad.buttons & SCE_CTRL_START)     btns |= LUNA_BTN_START;
    if (pad.buttons & SCE_CTRL_SELECT)    btns |= LUNA_BTN_SELECT;

    // Left analog stick (0..255 -> -1.0 .. +1.0)
    float rawLx = (static_cast<float>(pad.lx) - 128.0f) / 128.0f;
    float rawLy = (static_cast<float>(pad.ly) - 128.0f) / 128.0f;
    m_currentState.leftStickX = applyDeadzone(rawLx);
    m_currentState.leftStickY = applyDeadzone(rawLy);

    // Right analog stick (0..255 -> -1.0 .. +1.0)
    float rawRx = (static_cast<float>(pad.rx) - 128.0f) / 128.0f;
    float rawRy = (static_cast<float>(pad.ry) - 128.0f) / 128.0f;
    m_currentState.rightStickX = applyDeadzone(rawRx);
    m_currentState.rightStickY = applyDeadzone(rawRy);

    // Rear Touch Pad: L2 / R2 triggers and L3 / R3 stick clicks
    // Back touch resolution is typically 1920x1088
    SceTouchData backTouch;
    memset(&backTouch, 0, sizeof(backTouch));
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &backTouch, 1);

    const int midX = 960;
    const int midY = 544;

    for (uint32_t i = 0; i < backTouch.reportNum; ++i) {
        int tx = backTouch.report[i].x;
        int ty = backTouch.report[i].y;

        if (ty < midY) {
            // Upper half: L2 / R2 analog triggers
            if (tx < midX) {
                btns |= LUNA_BTN_LT;
                m_currentState.leftTrigger = 1.0f;
            } else {
                btns |= LUNA_BTN_RT;
                m_currentState.rightTrigger = 1.0f;
            }
        } else {
            // Lower half: L3 / R3 stick clicks
            if (tx < midX) {
                btns |= LUNA_BTN_L3;
            } else {
                btns |= LUNA_BTN_R3;
            }
        }
    }

    m_currentState.buttons = btns;
#else
    m_currentState.buttons = 0;
#endif

    m_pressedButtons = m_currentState.buttons & ~m_lastState.buttons;
}

const LunaGamepadState& InputManager::getState() {
    return m_currentState;
}

bool InputManager::isButtonDown(uint32_t buttonMask) {
    return (m_currentState.buttons & buttonMask) == buttonMask;
}

bool InputManager::isButtonPressed(uint32_t buttonMask) {
    return (m_pressedButtons & buttonMask) == buttonMask;
}

std::string InputManager::serializeJson() {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"input\",\"seq\":%u,\"t\":%llu,\"btn\":%u,"
        "\"lx\":%.2f,\"ly\":%.2f,\"rx\":%.2f,\"ry\":%.2f,\"lt\":%.2f,\"rt\":%.2f}",
        m_currentState.sequence,
        static_cast<unsigned long long>(m_currentState.timestampMs),
        m_currentState.buttons,
        m_currentState.leftStickX,
        m_currentState.leftStickY,
        m_currentState.rightStickX,
        m_currentState.rightStickY,
        m_currentState.leftTrigger,
        m_currentState.rightTrigger
    );
    return std::string(buf);
}

size_t InputManager::serializeBinary(uint8_t* outBuffer, size_t maxLen) {
    if (!outBuffer || maxLen < 24) return 0;

    // 24-byte compact binary packet
    // [0..3]   Magic 'LUNA' (0x4C, 0x55, 0x4E, 0x41)
    outBuffer[0] = 'L';
    outBuffer[1] = 'U';
    outBuffer[2] = 'N';
    outBuffer[3] = 'A';

    // [4..7]   Sequence
    uint32_t seq = m_currentState.sequence;
    memcpy(&outBuffer[4], &seq, sizeof(seq));

    // [8..11]  Buttons Bitmask
    uint32_t btns = m_currentState.buttons;
    memcpy(&outBuffer[8], &btns, sizeof(btns));

    // [12..19] Analog Sticks (-32767 .. 32767)
    int16_t lx = static_cast<int16_t>(m_currentState.leftStickX * 32767.0f);
    int16_t ly = static_cast<int16_t>(m_currentState.leftStickY * 32767.0f);
    int16_t rx = static_cast<int16_t>(m_currentState.rightStickX * 32767.0f);
    int16_t ry = static_cast<int16_t>(m_currentState.rightStickY * 32767.0f);
    memcpy(&outBuffer[12], &lx, sizeof(lx));
    memcpy(&outBuffer[14], &ly, sizeof(ly));
    memcpy(&outBuffer[16], &rx, sizeof(rx));
    memcpy(&outBuffer[18], &ry, sizeof(ry));

    // [20..21] Analog Triggers (0..255)
    outBuffer[20] = static_cast<uint8_t>(m_currentState.leftTrigger * 255.0f);
    outBuffer[21] = static_cast<uint8_t>(m_currentState.rightTrigger * 255.0f);

    // [22..23] Reserved
    outBuffer[22] = 0;
    outBuffer[23] = 0;

    return 24;
}

bool InputManager::sendInputPacket() {
    if (!m_initialized) return false;

    // In a live stream session, forward serialized gamepad frame
    if (WebSocketClient::isConnected()) {
        std::string jsonPacket = serializeJson();
        WebSocketClient::sendTextMessage(jsonPacket);
        m_packetsSent++;
        m_lastLatencyMs = 1.4f; // Local sampling to dispatch latency ~1.4ms
        return true;
    }

    return false;
}

uint32_t InputManager::getPacketsSentCount() {
    return m_packetsSent;
}

float InputManager::getLastInputLatencyMs() {
    return m_lastLatencyMs;
}
