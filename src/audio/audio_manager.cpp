#include "audio/audio_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__)
#include <psp2/audioout.h>
#endif

bool AudioManager::m_initialized = false;
uint32_t AudioManager::m_sampleRate = 48000;
uint32_t AudioManager::m_channels = 2;
int AudioManager::m_audioPort = -1;

int16_t AudioManager::m_ringBuffer[AudioManager::RING_BUFFER_CAPACITY * 2] = {0};
size_t AudioManager::m_writeIndex = 0;
size_t AudioManager::m_readIndex = 0;
size_t AudioManager::m_bufferedFrames = 0;
uint32_t AudioManager::m_underrunCount = 0;

bool AudioManager::init(uint32_t sampleRate, uint32_t channels) {
    if (m_initialized) return true;

    m_sampleRate = sampleRate;
    m_channels = channels;
    resetBuffer();

#if defined(__vita__)
    m_audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 512, sampleRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (m_audioPort < 0) {
        LOG_ERROR("sceAudioOutOpenPort failed: 0x%08X", m_audioPort);
        return false;
    }
    // Explicitly set output volume to unity gain (0dB); ports start muted by default
    int vols[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    sceAudioOutSetVolume(m_audioPort, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vols);

    LOG_INFO("PS Vita Audio Output Port initialized (Port: %d, %uHz Stereo, Vol: 0dB)", m_audioPort, sampleRate);
#else
    LOG_INFO("Host simulation: Audio output initialized (%uHz Stereo)", sampleRate);
#endif

    m_initialized = true;
    return true;
}

void AudioManager::shutdown() {
    if (!m_initialized) return;

#if defined(__vita__)
    if (m_audioPort >= 0) {
        sceAudioOutReleasePort(m_audioPort);
        m_audioPort = -1;
    }
#endif

    m_initialized = false;
    LOG_INFO("AudioManager shutdown complete.");
}

bool AudioManager::isInitialized() {
    return m_initialized;
}

alignas(16) static int16_t g_silencePCM[1024] = {0};

bool AudioManager::submitStereoPCM(const int16_t* pcmSamples, size_t numFrames) {
    if (!m_initialized || !pcmSamples || numFrames == 0) return false;

    for (size_t i = 0; i < numFrames; ++i) {
        if (m_bufferedFrames >= RING_BUFFER_CAPACITY) {
            // Buffer full: drop oldest frame to maintain low-latency streaming
            m_readIndex = (m_readIndex + 1) % RING_BUFFER_CAPACITY;
            m_bufferedFrames--;
        }

        m_ringBuffer[m_writeIndex * 2 + 0] = pcmSamples[i * 2 + 0];
        m_ringBuffer[m_writeIndex * 2 + 1] = pcmSamples[i * 2 + 1];
        m_writeIndex = (m_writeIndex + 1) % RING_BUFFER_CAPACITY;
        m_bufferedFrames++;
    }

    return true;
}

void AudioManager::pumpAudio() {
    if (!m_initialized) return;

#if defined(__vita__)
    if (m_audioPort < 0) return;

    alignas(16) static int16_t outBlock[1024]; // 512 frames * 2 channels

    if (m_bufferedFrames >= 512) {
        for (size_t i = 0; i < 512; ++i) {
            outBlock[i * 2 + 0] = m_ringBuffer[m_readIndex * 2 + 0];
            outBlock[i * 2 + 1] = m_ringBuffer[m_readIndex * 2 + 1];
            m_readIndex = (m_readIndex + 1) % RING_BUFFER_CAPACITY;
        }
        m_bufferedFrames -= 512;
        sceAudioOutOutput(m_audioPort, outBlock);
    } else {
        // Output silence during jitter buffer starvation
        m_underrunCount++;
        sceAudioOutOutput(m_audioPort, g_silencePCM);
    }
#endif
}

void AudioManager::resetBuffer() {
    m_writeIndex = 0;
    m_readIndex = 0;
    m_bufferedFrames = 0;
    m_underrunCount = 0;
    memset(m_ringBuffer, 0, sizeof(m_ringBuffer));
}

size_t AudioManager::getBufferedFrameCount() {
    return m_bufferedFrames;
}

uint32_t AudioManager::getUnderrunCount() {
    return m_underrunCount;
}
