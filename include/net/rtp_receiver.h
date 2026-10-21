#ifndef VITA_LUNA_RTP_RECEIVER_H
#define VITA_LUNA_RTP_RECEIVER_H

#include "common.h"
#include <cstdint>
#include <cstddef>
#include <vector>

struct RtpStreamMetrics {
    uint32_t packetsReceived;
    uint32_t packetsLost;
    float packetLossPercent;
    uint32_t currentBitrateKbps;
    float networkJitterMs;
    uint32_t totalBytesReceived;
};

class RtpReceiver {
public:
    static bool init(int videoPort = 5004, int audioPort = 5006);
    static void shutdown();

    // Polls UDP sockets, depayloads RTP video/audio, and feeds decoders
    static void poll();

    static bool isRunning();
    static const RtpStreamMetrics& getMetrics();

    // Reset stream metrics upon new session start
    static void resetMetrics();

    // Dynamic Stream Tuning
    static int getRecommendedBitrateMbps();
    static void setBitrateLimitKbps(uint32_t kbps);
    static uint32_t getBitrateLimitKbps();

    // SRTP Session Configuration
    static bool configureSrtpKeys(const uint8_t* masterKey, const uint8_t* masterSalt);

    // Network drain logic (invoked by worker thread or fallback polling)
    static void drainNetworkPackets();

private:
    static bool m_running;
    static int m_videoSock;
    static int m_audioSock;
    static int m_videoPort;
    static int m_audioPort;
    static uint32_t m_bitrateLimitKbps;
    static int m_recommendedBitrateMbps;
    static int m_workerThreadId;

    // H.264 FU-A (Fragmentation Unit) Reassembly Buffer
    static std::vector<uint8_t> m_nalReassemblyBuffer;
    static bool m_isReassembling;
    static uint16_t m_lastVideoSeq;
    static bool m_firstVideoPacket;

    // Metrics tracking
    static RtpStreamMetrics m_metrics;
    static uint64_t m_lastMetricsTimeMs;
    static uint32_t m_bytesInInterval;
    static uint32_t m_packetsInInterval;

    static void processVideoPacket(const uint8_t* data, size_t length);
    static void processAudioPacket(const uint8_t* data, size_t length);
};

#endif // VITA_LUNA_RTP_RECEIVER_H
