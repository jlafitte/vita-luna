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

private:
    static bool m_initialized;
    static uint32_t m_width;
    static uint32_t m_height;
    static uint32_t m_decodedFrameCount;
    static float m_lastDecodeTimeMs;

    static void* m_frameBufferMemory;
    static uint32_t m_decoderHandle;
};

#endif // VITA_LUNA_H264_DECODER_H
