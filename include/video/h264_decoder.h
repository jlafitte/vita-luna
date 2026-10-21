#ifndef VITA_LUNA_H264_DECODER_H
#define VITA_LUNA_H264_DECODER_H

#include "common.h"
#include <cstdint>
#include <cstddef>

struct DecodedFrame {
    void* pixelData;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pixelFormat;
    bool isValid;
};

class H264Decoder {
public:
    static bool init(uint32_t width = LUNA_TARGET_WIDTH, uint32_t height = LUNA_TARGET_HEIGHT);
    static void shutdown();

    static bool isInitialized();
    static bool decodeNALUnit(const uint8_t* nalData, size_t length, DecodedFrame& outFrame);

    static uint32_t getDecodedFrameCount();
    static float getAverageDecodeTimeMs();
    static const DecodedFrame& getLatestFrame();
    static bool generateTestFrame(uint32_t frameIndex);

    // Hardware AVC Diagnostic Telemetry
    static uint32_t getNalUnitCount();
    static uint32_t getLastNalType();
    static uint32_t getLastNalSize();
    static int getLastDecodeRet();
    static uint32_t getLastNumOfOutput();
    static uint32_t getDecoderHandle();
    static int getInitLibRet();
    static int getCreateDecoderRet();
    static int getQueryMemSizeRet();
    static uint32_t getQueriedFrameMemSize();
    static int getMemBlockId();
    static bool hasContiguousBuffers();

private:
    static bool m_initialized;
    static uint32_t m_width;
    static uint32_t m_height;
    static uint32_t m_decodedFrameCount;
    static float m_lastDecodeTimeMs;

    static uint32_t m_nalUnitsCount;
    static uint32_t m_lastNalType;
    static uint32_t m_lastNalSize;
    static int m_lastDecodeRet;
    static uint32_t m_lastNumOfOutput;
    static int m_initLibRet;
    static int m_createDecoderRet;
    static int m_queryMemSizeRet;
    static uint32_t m_queriedFrameMemSize;
    static int m_memBlockIdVal;

    static void* m_frameBufferMemory;
    static uint32_t m_decoderHandle;
    static DecodedFrame m_latestFrame;
    static void* m_simulatedBuffer;
};

#endif // VITA_LUNA_H264_DECODER_H
