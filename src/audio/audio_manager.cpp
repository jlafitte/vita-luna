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

bool AudioManager::init(uint32_t sampleRate, uint32_t channels) {
    if (m_initialized) return true;

    m_sampleRate = sampleRate;
    m_channels = channels;

#if defined(__vita__)
    m_audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 512, sampleRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (m_audioPort < 0) {
        LOG_ERROR("sceAudioOutOpenPort failed: 0x%08X", m_audioPort);
        return false;
    }
    LOG_INFO("PS Vita Audio Output Port initialized (Port: %d, %uHz Stereo)", m_audioPort, sampleRate);
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
    if (!m_initialized) return false;

#if defined(__vita__)
    if (m_audioPort >= 0) {
        const int16_t* bufToOutput = (pcmSamples && numFrames >= 512) ? pcmSamples : g_silencePCM;
        sceAudioOutOutput(m_audioPort, bufToOutput);
        return true;
    }
#else
    (void)pcmSamples; (void)numFrames;
#endif
    return true;
}
