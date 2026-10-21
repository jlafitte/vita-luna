#include "net/rtp_receiver.h"
#include "net/srtp_decryptor.h"
#include "video/h264_decoder.h"
#include "audio/audio_manager.h"
#include "ui/diagnostic_overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

#if defined(__vita__)
#include <psp2/net/net.h>
#include <psp2/kernel/threadmgr.h>
#endif

bool RtpReceiver::m_running = false;
int RtpReceiver::m_videoSock = -1;
int RtpReceiver::m_audioSock = -1;
int RtpReceiver::m_videoPort = 5004;
int RtpReceiver::m_audioPort = 5006;
uint32_t RtpReceiver::m_bitrateLimitKbps = 10000;
int RtpReceiver::m_recommendedBitrateMbps = 10;
int RtpReceiver::m_workerThreadId = -1;

std::vector<uint8_t> RtpReceiver::m_nalReassemblyBuffer;
bool RtpReceiver::m_isReassembling = false;
uint16_t RtpReceiver::m_lastVideoSeq = 0;
bool RtpReceiver::m_firstVideoPacket = true;

RtpStreamMetrics RtpReceiver::m_metrics = {};
uint64_t RtpReceiver::m_lastMetricsTimeMs = 0;
uint32_t RtpReceiver::m_bytesInInterval = 0;
uint32_t RtpReceiver::m_packetsInInterval = 0;

static uint64_t getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static std::vector<uint8_t> s_cachedSps;
static std::vector<uint8_t> s_cachedPps;
static bool s_hasSeenKeyframe = false;

#if defined(__vita__)
static int mediaWorkerThreadFunc(SceSize args, void *argp) {
    (void)args; (void)argp;
    LOG_INFO("RtpReceiver background media thread started on CPU Core 1.");
    while (RtpReceiver::isRunning()) {
        RtpReceiver::drainNetworkPackets();
        sceKernelDelayThread(500); // 0.5ms yield to prevent CPU starvation
    }
    LOG_INFO("RtpReceiver background media thread terminating.");
    return sceKernelExitDeleteThread(0);
}
#endif

bool RtpReceiver::init(int videoPort, int audioPort) {
    if (m_running) return true;

    m_videoPort = videoPort;
    m_audioPort = audioPort;
    resetMetrics();

    m_nalReassemblyBuffer.reserve(256 * 1024); // 256 KB preallocated for 720p H.264 I-frames
    m_isReassembling = false;
    m_firstVideoPacket = true;

    SrtpDecryptor::init();

#if defined(__vita__)
    // Video UDP Socket
    m_videoSock = sceNetSocket("luna_rtp_vid", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (m_videoSock >= 0) {
        int nbio = 1;
        sceNetSetsockopt(m_videoSock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio));

        int rcvbuf = 512 * 1024;
        sceNetSetsockopt(m_videoSock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        SceNetSockaddrIn vAddr;
        memset(&vAddr, 0, sizeof(vAddr));
        vAddr.sin_len = sizeof(vAddr);
        vAddr.sin_family = SCE_NET_AF_INET;
        vAddr.sin_port = sceNetHtons(videoPort);
        vAddr.sin_addr.s_addr = SCE_NET_INADDR_ANY;

        int ret = sceNetBind(m_videoSock, (SceNetSockaddr*)&vAddr, sizeof(vAddr));
        if (ret < 0) {
            LOG_WARN("RtpReceiver: Video port %d bind warning 0x%08X", videoPort, ret);
        } else {
            LOG_INFO("RtpReceiver: Video UDP socket bound to port %d", videoPort);
        }
    } else {
        LOG_WARN("RtpReceiver: sceNetSocket for video returned 0x%08X", m_videoSock);
    }

    // Audio UDP Socket
    m_audioSock = sceNetSocket("luna_rtp_aud", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (m_audioSock >= 0) {
        int nbio = 1;
        sceNetSetsockopt(m_audioSock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio));

        int rcvbuf = 128 * 1024;
        sceNetSetsockopt(m_audioSock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        SceNetSockaddrIn aAddr;
        memset(&aAddr, 0, sizeof(aAddr));
        aAddr.sin_len = sizeof(aAddr);
        aAddr.sin_family = SCE_NET_AF_INET;
        aAddr.sin_port = sceNetHtons(audioPort);
        aAddr.sin_addr.s_addr = SCE_NET_INADDR_ANY;

        int ret = sceNetBind(m_audioSock, (SceNetSockaddr*)&aAddr, sizeof(aAddr));
        if (ret < 0) {
            LOG_WARN("RtpReceiver: Audio port %d bind warning 0x%08X", audioPort, ret);
        } else {
            LOG_INFO("RtpReceiver: Audio UDP socket bound to port %d", audioPort);
        }
    }
#else
    LOG_INFO("Host simulation: RtpReceiver initialized for ports %d/%d", videoPort, audioPort);
#endif

    m_running = true;
    s_hasSeenKeyframe = false;
    s_cachedSps.clear();
    s_cachedPps.clear();
    m_lastMetricsTimeMs = getCurrentTimeMs();

#if defined(__vita__)
    // Spawn dedicated media worker thread pinned to CPU Core 1 (priority 0x10000100 - 10)
    m_workerThreadId = sceKernelCreateThread(
        "Luna_MediaWorker",
        (SceKernelThreadEntry)mediaWorkerThreadFunc,
        0x10000100 - 10,
        64 * 1024,
        0,
        0,
        NULL
    );
    if (m_workerThreadId >= 0) {
        sceKernelStartThread(m_workerThreadId, 0, NULL);
        LOG_INFO("Created multithreaded network/SRTP media worker (Thread: 0x%08X)", m_workerThreadId);
    } else {
        LOG_WARN("Failed to create media worker thread: 0x%08X (fallback to sync polling)", m_workerThreadId);
    }
#endif

    return true;
}

void RtpReceiver::shutdown() {
    if (!m_running) return;
    m_running = false;

#if defined(__vita__)
    if (m_workerThreadId >= 0) {
        sceKernelWaitThreadEnd(m_workerThreadId, NULL, NULL);
        m_workerThreadId = -1;
    }

    if (m_videoSock >= 0) {
        sceNetSocketClose(m_videoSock);
        m_videoSock = -1;
    }
    if (m_audioSock >= 0) {
        sceNetSocketClose(m_audioSock);
        m_audioSock = -1;
    }
#endif

    SrtpDecryptor::shutdown();
    m_isReassembling = false;
    m_nalReassemblyBuffer.clear();
    LOG_INFO("RtpReceiver shutdown complete.");
}

void RtpReceiver::resetMetrics() {
    memset(&m_metrics, 0, sizeof(m_metrics));
    m_metrics.currentBitrateKbps = 10000;
    m_metrics.networkJitterMs = 2.1f;
    m_bytesInInterval = 0;
    m_packetsInInterval = 0;
    m_lastMetricsTimeMs = getCurrentTimeMs();
}

bool RtpReceiver::configureSrtpKeys(const uint8_t* masterKey, const uint8_t* masterSalt) {
    return SrtpDecryptor::setSessionKeys(masterKey, masterSalt);
}

void RtpReceiver::drainNetworkPackets() {
    if (!m_running) return;

    uint8_t packetBuffer[2048];

#if defined(__vita__)
    // 1. Drain non-blocking video UDP packets
    if (m_videoSock >= 0) {
        while (m_running) {
            int ret = sceNetRecvfrom(m_videoSock, packetBuffer, sizeof(packetBuffer), 0, NULL, NULL);
            if (ret <= 0) break;

            size_t len = static_cast<size_t>(ret);
            size_t payloadOffset = 0;
            if (SrtpDecryptor::unprotectRtp(packetBuffer, len, payloadOffset)) {
                processVideoPacket(packetBuffer, len);
            }
        }
    }

    // 2. Drain non-blocking audio UDP packets
    if (m_audioSock >= 0) {
        while (m_running) {
            int ret = sceNetRecvfrom(m_audioSock, packetBuffer, sizeof(packetBuffer), 0, NULL, NULL);
            if (ret <= 0) break;

            size_t len = static_cast<size_t>(ret);
            size_t payloadOffset = 0;
            if (SrtpDecryptor::unprotectRtp(packetBuffer, len, payloadOffset)) {
                processAudioPacket(packetBuffer, len);
            }
        }
    }
#endif
}

void RtpReceiver::poll() {
    if (!m_running) return;

    // Fallback drain if worker thread is inactive
    if (m_workerThreadId < 0) {
        drainNetworkPackets();
    }

    // Fallback heartbeat animation if network stream has not yet started or is waiting
    if (m_metrics.packetsReceived == 0 && H264Decoder::isInitialized()) {
        static uint32_t idleFrameCounter = 0;
        H264Decoder::generateTestFrame(idleFrameCounter++);
    }

    // Update Telemetry Metrics every 1.0s
    uint64_t now = getCurrentTimeMs();
    uint64_t elapsed = now - m_lastMetricsTimeMs;
    if (elapsed >= 1000) {
        if (m_bytesInInterval > 0) {
            m_metrics.currentBitrateKbps = static_cast<uint32_t>((m_bytesInInterval * 8) / elapsed);
        }

        uint32_t totalPackets = m_metrics.packetsReceived + m_metrics.packetsLost;
        if (totalPackets > 0) {
            m_metrics.packetLossPercent = (static_cast<float>(m_metrics.packetsLost) / totalPackets) * 100.0f;
        }

        // Dynamic Stream Bitrate Tuning based on network telemetry
        if (m_metrics.packetLossPercent > 4.0f) {
            m_recommendedBitrateMbps = 5;  // Drop to 5 Mbps to mitigate network congestion
        } else if (m_metrics.packetLossPercent > 1.5f) {
            m_recommendedBitrateMbps = 10; // Nominal 10 Mbps
        } else if (m_metrics.packetLossPercent <= 0.5f && m_metrics.packetsReceived > 100) {
            m_recommendedBitrateMbps = 15; // Clean link: boost to 15 Mbps High-Quality
        }

        m_bytesInInterval = 0;
        m_packetsInInterval = 0;
        m_lastMetricsTimeMs = now;

        // Feed updated metrics into Diagnostic Overlay
        DiagnosticOverlay::setStreamStats(
            LUNA_TARGET_WIDTH,
            LUNA_TARGET_HEIGHT,
            LUNA_TARGET_FPS,
            m_metrics.currentBitrateKbps,
            H264Decoder::getAverageDecodeTimeMs()
        );
    }
}

void RtpReceiver::processVideoPacket(const uint8_t* data, size_t length) {
    if (!data || length < 12) return;

    // Validate RTP Version 2
    uint8_t version = (data[0] >> 6) & 0x03;
    if (version != 2) return;

    m_metrics.packetsReceived++;
    m_metrics.totalBytesReceived += length;
    m_bytesInInterval += length;
    m_packetsInInterval++;

    // Sequence Number
    uint16_t seq = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    if (m_firstVideoPacket) {
        m_firstVideoPacket = false;
        m_lastVideoSeq = seq;
    } else {
        uint16_t diff = seq - m_lastVideoSeq;
        if (diff > 1 && diff < 1000) {
            m_metrics.packetsLost += (diff - 1);
        }
        m_lastVideoSeq = seq;
    }

    // Header offset
    uint8_t cc = data[0] & 0x0F;
    size_t offset = 12 + cc * 4;
    if (data[0] & 0x10) { // Extension header
        if (offset + 4 > length) return;
        uint16_t extLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        offset += 4 + extLen * 4;
    }

    if (offset >= length) return;

    const uint8_t* payload = data + offset;
    size_t payloadLen = length - offset;
    if (payloadLen == 0) return;

    uint8_t nalHeader = payload[0];
    uint8_t nalType = nalHeader & 0x1F;

    // Case 1: Single NAL Unit Packet (RFC 6184 Section 5.6)
    if (nalType >= 1 && nalType <= 23) {
        if (nalType == 7) {
            s_cachedSps.assign(payload, payload + payloadLen);
            return;
        } else if (nalType == 8) {
            s_cachedPps.assign(payload, payload + payloadLen);
            return;
        }

        if (nalType == 5) {
            s_hasSeenKeyframe = true;
        } else if (!s_hasSeenKeyframe) {
            // Wait for first IDR keyframe with SPS/PPS before feeding P-frames to decoder
            return;
        }

        std::vector<uint8_t> completeAu;
        completeAu.reserve(payloadLen + (s_cachedSps.size() + s_cachedPps.size() + 16));

        // When receiving an IDR keyframe (Type 5), prepend cached SPS and PPS to form a complete Access Unit
        if (nalType == 5 && !s_cachedSps.empty() && !s_cachedPps.empty()) {
            completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x01);
            completeAu.insert(completeAu.end(), s_cachedSps.begin(), s_cachedSps.end());

            completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x01);
            completeAu.insert(completeAu.end(), s_cachedPps.begin(), s_cachedPps.end());
        }

        completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x00); completeAu.push_back(0x01);
        completeAu.insert(completeAu.end(), payload, payload + payloadLen);

        DecodedFrame frame;
        H264Decoder::decodeNALUnit(completeAu.data(), completeAu.size(), frame);
    }
    // Case 2: Fragmentation Unit A (FU-A) (RFC 6184 Section 5.8)
    else if (nalType == 28 && payloadLen >= 2) {
        uint8_t fuIndicator = payload[0];
        uint8_t fuHeader = payload[1];
        bool isStart = (fuHeader & 0x80) != 0;
        bool isEnd = (fuHeader & 0x40) != 0;
        uint8_t originalNalType = fuHeader & 0x1F;
        uint8_t reconstructedHeader = (fuIndicator & 0xE0) | originalNalType;

        if (originalNalType == 5) {
            s_hasSeenKeyframe = true;
        } else if (!s_hasSeenKeyframe) {
            // Drop initial fragmented P-frames until first IDR arrives
            return;
        }

        if (isStart) {
            m_nalReassemblyBuffer.clear();
            if (originalNalType == 5 && !s_cachedSps.empty() && !s_cachedPps.empty()) {
                m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x00);
                m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x01);
                m_nalReassemblyBuffer.insert(m_nalReassemblyBuffer.end(), s_cachedSps.begin(), s_cachedSps.end());

                m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x00);
                m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x01);
                m_nalReassemblyBuffer.insert(m_nalReassemblyBuffer.end(), s_cachedPps.begin(), s_cachedPps.end());
            }

            m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x00);
            m_nalReassemblyBuffer.push_back(0x00); m_nalReassemblyBuffer.push_back(0x01);
            m_nalReassemblyBuffer.push_back(reconstructedHeader);
            m_nalReassemblyBuffer.insert(m_nalReassemblyBuffer.end(), payload + 2, payload + payloadLen);
            m_isReassembling = true;
        } else if (m_isReassembling) {
            m_nalReassemblyBuffer.insert(m_nalReassemblyBuffer.end(), payload + 2, payload + payloadLen);
            if (isEnd) {
                DecodedFrame frame;
                H264Decoder::decodeNALUnit(m_nalReassemblyBuffer.data(), m_nalReassemblyBuffer.size(), frame);
                m_isReassembling = false;
                m_nalReassemblyBuffer.clear();
            }
        }
    }
    // Case 3: Single-Time Aggregation Packet type A (STAP-A) (RFC 6184 Section 5.7)
    // Commonly used by FFmpeg and WebRTC to aggregate SPS + PPS parameter sets
    else if (nalType == 24 && payloadLen >= 3) {
        size_t stapOffset = 1; // Skip 1-byte STAP-A indicator
        while (stapOffset + 2 <= payloadLen) {
            uint16_t naluSize = (static_cast<uint16_t>(payload[stapOffset]) << 8) | payload[stapOffset + 1];
            stapOffset += 2;
            if (naluSize == 0 || stapOffset + naluSize > payloadLen) break;

            uint8_t innerType = payload[stapOffset] & 0x1F;
            if (innerType == 7) {
                s_cachedSps.assign(payload + stapOffset, payload + stapOffset + naluSize);
            } else if (innerType == 8) {
                s_cachedPps.assign(payload + stapOffset, payload + stapOffset + naluSize);
            }
            stapOffset += naluSize;
        }
    }
}

void RtpReceiver::processAudioPacket(const uint8_t* data, size_t length) {
    if (!data || length < 12) return;

    // Minimum RTP header is 12 bytes
    uint8_t cc = data[0] & 0x0F;
    size_t offset = 12 + cc * 4;
    if (data[0] & 0x10) { // Extension header
        if (offset + 4 > length) return;
        uint16_t extLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        offset += 4 + extLen * 4;
    }

    if (offset >= length) return;

    const uint8_t* payload = data + offset;
    size_t payloadLen = length - offset;

    // Forward PCM audio payload directly to PS Vita AudioManager
    if (payloadLen >= sizeof(int16_t) * 2) {
        const int16_t* pcmSamples = reinterpret_cast<const int16_t*>(payload);
        size_t numFrames = payloadLen / (sizeof(int16_t) * 2);
        AudioManager::submitStereoPCM(pcmSamples, numFrames);
    }
}

bool RtpReceiver::isRunning() {
    return m_running;
}

const RtpStreamMetrics& RtpReceiver::getMetrics() {
    return m_metrics;
}

int RtpReceiver::getRecommendedBitrateMbps() {
    return m_recommendedBitrateMbps;
}

void RtpReceiver::setBitrateLimitKbps(uint32_t kbps) {
    m_bitrateLimitKbps = kbps;
    m_recommendedBitrateMbps = static_cast<int>(kbps / 1000);
}

uint32_t RtpReceiver::getBitrateLimitKbps() {
    return m_bitrateLimitKbps;
}
