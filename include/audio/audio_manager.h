#ifndef VITA_LUNA_AUDIO_MANAGER_H
#define VITA_LUNA_AUDIO_MANAGER_H

#include "common.h"
#include <cstdint>
#include <cstddef>

class AudioManager {
public:
    static bool init(uint32_t sampleRate = 48000, uint32_t channels = 2);
    static void shutdown();

    static bool isInitialized();
    static bool submitStereoPCM(const int16_t* pcmSamples, size_t numFrames);

private:
    static bool m_initialized;
    static uint32_t m_sampleRate;
    static uint32_t m_channels;
    static int m_audioPort;
};

#endif // VITA_LUNA_AUDIO_MANAGER_H
