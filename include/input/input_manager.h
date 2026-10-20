#ifndef VITA_LUNA_INPUT_MANAGER_H
#define VITA_LUNA_INPUT_MANAGER_H

#include "common.h"
#include <cstdint>
#include <string>

// Standard Gamepad Button Bitmask (W3C / XInput / Amazon Luna mapping)
enum LunaButton : uint32_t {
    LUNA_BTN_A          = (1 << 0),  // Cross (PS Vita)
    LUNA_BTN_B          = (1 << 1),  // Circle (PS Vita)
    LUNA_BTN_X          = (1 << 2),  // Square (PS Vita)
    LUNA_BTN_Y          = (1 << 3),  // Triangle (PS Vita)
    LUNA_BTN_LB         = (1 << 4),  // Left Bumper / L1
    LUNA_BTN_RB         = (1 << 5),  // Right Bumper / R1
    LUNA_BTN_LT         = (1 << 6),  // Left Trigger / L2 (Rear Touch Top-Left)
    LUNA_BTN_RT         = (1 << 7),  // Right Trigger / R2 (Rear Touch Top-Right)
    LUNA_BTN_SELECT     = (1 << 8),  // Select / Back
    LUNA_BTN_START      = (1 << 9),  // Start
    LUNA_BTN_L3         = (1 << 10), // Left Stick Click (Rear Touch Bottom-Left)
    LUNA_BTN_R3         = (1 << 11), // Right Stick Click (Rear Touch Bottom-Right)
    LUNA_BTN_DPAD_UP    = (1 << 12), // D-Pad Up
    LUNA_BTN_DPAD_DOWN  = (1 << 13), // D-Pad Down
    LUNA_BTN_DPAD_LEFT  = (1 << 14), // D-Pad Left
    LUNA_BTN_DPAD_RIGHT = (1 << 15), // D-Pad Right
    LUNA_BTN_GUIDE      = (1 << 16)  // PS / Guide
};

struct LunaGamepadState {
    uint32_t buttons;
    float leftStickX;     // -1.0f (Left) to +1.0f (Right)
    float leftStickY;     // -1.0f (Up) to +1.0f (Down)
    float rightStickX;    // -1.0f (Left) to +1.0f (Right)
    float rightStickY;    // -1.0f (Up) to +1.0f (Down)
    float leftTrigger;    // 0.0f (Released) to 1.0f (Fully Pressed)
    float rightTrigger;   // 0.0f (Released) to 1.0f (Fully Pressed)
    uint64_t timestampMs;
    uint32_t sequence;
};

class InputManager {
public:
    static bool init();
    static void shutdown();

    // Polls SceCtrl and SceTouch to update controller state
    static void poll();

    static const LunaGamepadState& getState();
    static bool isButtonDown(uint32_t buttonMask);
    static bool isButtonPressed(uint32_t buttonMask);

    // Serialization for Luna Stream Session
    static std::string serializeJson();
    static size_t serializeBinary(uint8_t* outBuffer, size_t maxLen);

    // Transmits input report over streaming socket
    static bool sendInputPacket();

    static uint32_t getPacketsSentCount();
    static float getLastInputLatencyMs();

private:
    static bool m_initialized;
    static LunaGamepadState m_currentState;
    static LunaGamepadState m_lastState;
    static uint32_t m_pressedButtons;
    static uint32_t m_packetsSent;
    static float m_lastLatencyMs;
    static uint32_t m_sequenceCounter;
};

#endif // VITA_LUNA_INPUT_MANAGER_H
