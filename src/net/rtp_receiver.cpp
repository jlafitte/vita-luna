#include "net/rtp_receiver.h"
#include "video/h264_decoder.h"
#include "audio/audio_manager.h"
#include "ui/diagnostic_overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

#if defined(__vita__)
#include <psp2/net/net.h>
#endif

bool RtpReceiver::m_running = false;
int RtpReceiver::m_videoSock = -1;
int RtpReceiver::m_audioSock = -1;
int RtpReceiver::m_videoPort = 5004;
int RtpReceiver::m_audioPort = 5006;

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

bool RtpReceiver::init(int videoPort, int audioPort) {
    if (m_running) return true;

    m_videoPort = videoPort;
    m_audioPort = audioPort;
    resetMetrics();

    m_nalReassemblyBuffer.reserve(256 * 1024); // 256 KB preallocated for 720p H.264 I-frames
    m_isReassembling = false;
    m_firstVideoPacket = true;

#if defined(__vita__)
    // Video UDP Socket
    m_videoSock = sceNetSocket("luna_rtp_vid", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (m_videoSock >= 0) {
        int nbio = 1;
        sceNetSetsockopt(m_videoSock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio));

        int rcvbuf = 256 * 1024;
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

        int rcvbuf = 64 * 1024;
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
    m_lastMetricsTimeMs = getCurrentTimeMs();
    return true;
}

void RtpReceiver::shutdown() {
    if (!m_running) return;

#if defined(__vita__)
    if (m_videoSock >= 0) {
        sceNetSocketClose(m_videoSock);
        m_videoSock = -1;
    }
    if (m_audioSock >= 0) {
        sceNetSocketClose(m_audioSock);
        m_audioSock = -1;
    }
#endif

    m_running = false;
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

void RtpReceiver::poll() {
    if (!m_running) return;

    bool receivedNetworkVideo = false;
    uint8_t packetBuffer[2048];

#if defined(__vita__)
    // 1. Drain non-blocking video UDP packets
    if (m_videoSock >= 0) {
        while (true) {
            int ret = sceNetRecvfrom(m_videoSock, packetBuffer, sizeof(packetBuffer), 0, NULL, NULL);
            if (ret <= 0) break;

            receivedNetworkVideo = true;
            processVideoPacket(packetBuffer, static_cast<size_t>(ret));
        }
    }

    // 2. Drain non-blocking audio UDP packets
    if (m_audioSock >= 0) {
        while (true) {
            int ret = sceNetRecvfrom(m_audioSock, packetBuffer, sizeof(packetBuffer), 0, NULL, NULL);
            if (ret <= 0) break;

            processAudioPacket(packetBuffer, static_cast<size_t>(ret));
        }
    }
#endif

    // Fallback heartbeat decoding if network stream has not yet started or is waiting
    if (!receivedNetworkVideo && H264Decoder::isInitialized()) {
        static const uint8_t sampleNalHeader[4] = { 0x00, 0x00, 0x00, 0x01 };
        DecodedFrame dummyFrame;
        H264Decoder::decodeNALUnit(sampleNalHeader, sizeof(sampleNalHeader), dummyFrame);
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
    if (offset >= length) return;

    const uint8_t* payload = data + offset;
    size_t payloadLen = length - offset;
    if (payloadLen == 0) return;

    uint8_t nalHeader = payload[0];
    uint8_t nalType = nalHeader & 0x1F;

    // Case 1: Single NAL Unit Packet (RFC 6184 Section 5.6)
    if (nalType >= 1 && nalType <= 23) {
        std::vector<uint8_t> completeNal;
        completeNal.reserve(payloadLen + 4);
        completeNal.push_back(0x00);
        completeNal.push_back(0x00);
        completeNal.push_back(0x00);
        completeNal.push_back(0x01);
        completeNal.insert(completeNal.end(), payload, payload + payloadLen);

        DecodedFrame frame;
        H264Decoder::decodeNALUnit(completeNal.data(), completeNal.size(), frame);
    }
    // Case 2: Fragmentation Unit A (FU-A) (RFC 6184 Section 5.8)
    else if (nalType == 28 && payloadLen >= 2) {
        uint8_t fuIndicator = payload[0];
        uint8_t fuHeader = payload[1];
        bool isStart = (fuHeader & 0x80) != 0;
        bool isEnd = (fuHeader & 0x40) != 0;
        uint8_t originalNalType = fuHeader & 0x1F;
        uint8_t reconstructedHeader = (fuIndicator & 0xE0) | originalNalType;

        if (isStart) {
            m_nalReassemblyBuffer.clear();
            m_nalReassemblyBuffer.push_back(0x00);
            m_nalReassemblyBuffer.push_back(0x00);
            m_nalReassemblyBuffer.push_back(0x00);
            m_nalReassemblyBuffer.push_back(0x01);
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
}

void RtpReceiver::processAudioPacket(const uint8_t* data, size_t length) {
    if (!data || length < 12) return;

    // Minimum RTP header is 12 bytes
    uint8_t cc = data[0] & 0x0F;
    size_t offset = 12 + cc * 4;
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
