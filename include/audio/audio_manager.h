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
    static void pumpAudio();
    static void resetBuffer();
    static size_t getBufferedFrameCount();
    static uint32_t getUnderrunCount();

private:
    static bool m_initialized;
    static uint32_t m_sampleRate;
    static uint32_t m_channels;
    static int m_audioPort;

    // PCM Stereo Ring Buffer (8192 stereo frames / ~170ms jitter resilience)
    static const size_t RING_BUFFER_CAPACITY = 8192;
    static int16_t m_ringBuffer[RING_BUFFER_CAPACITY * 2];
    static size_t m_writeIndex;
    static size_t m_readIndex;
    static size_t m_bufferedFrames;
    static uint32_t m_underrunCount;
};

#endif // VITA_LUNA_AUDIO_MANAGER_H
