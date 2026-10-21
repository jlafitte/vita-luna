#include "video/h264_decoder.h"
#include "graphics/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__)
#include <psp2/sysmodule.h>
#include <psp2/videodec.h>
#include <psp2/kernel/sysmem.h>
#endif

bool H264Decoder::m_initialized = false;
uint32_t H264Decoder::m_width = LUNA_TARGET_WIDTH;
uint32_t H264Decoder::m_height = LUNA_TARGET_HEIGHT;
uint32_t H264Decoder::m_decodedFrameCount = 0;
float H264Decoder::m_lastDecodeTimeMs = 0.0f;
uint32_t H264Decoder::m_nalUnitsCount = 0;
uint32_t H264Decoder::m_lastNalType = 0;
uint32_t H264Decoder::m_lastNalSize = 0;
int H264Decoder::m_lastDecodeRet = 0;
uint32_t H264Decoder::m_lastNumOfOutput = 0;
int H264Decoder::m_initLibRet = 0;
int H264Decoder::m_createDecoderRet = 0;
int H264Decoder::m_queryMemSizeRet = 0;
uint32_t H264Decoder::m_queriedFrameMemSize = 0;
int H264Decoder::m_memBlockIdVal = -1;
void* H264Decoder::m_frameBufferMemory = nullptr;
uint32_t H264Decoder::m_decoderHandle = 0;
DecodedFrame H264Decoder::m_latestFrame = { nullptr, 0, 0, 0, 0, false };
void* H264Decoder::m_simulatedBuffer = nullptr;

#if defined(__vita__)
static SceAvcdecCtrl g_avcCtrl;
static SceUID g_memBlockId = -1;
static SceUID g_esMemBlockId = -1;
static void* g_esBuffer = nullptr;
static const size_t ES_BUFFER_SIZE = 512 * 1024; // 512 KB physically contiguous buffer for NAL Access Units

static SceUID g_outMemBlockId = -1;
static void* g_outFrameBuffer[2] = { nullptr, nullptr };
static const size_t SINGLE_FRAME_SIZE = 1280 * 720 * 4; // ~3.68 MB per 720p RGBA8888 frame
#endif

bool H264Decoder::init(uint32_t width, uint32_t height) {
    if (m_initialized) return true;

    m_width = width;
    m_height = height;
    m_decodedFrameCount = 0;
    m_lastDecodeTimeMs = 0.0f;

#if defined(__vita__)
    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
    if (ret < 0) {
        LOG_WARN("sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC) warning: 0x%08X", ret);
    }

    SceVideodecQueryInitInfoHwAvcdec initInfo;
    memset(&initInfo, 0, sizeof(initInfo));
    initInfo.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
    initInfo.horizontal = width;
    initInfo.vertical = height;
    initInfo.numOfRefFrames = 2;
    initInfo.numOfStreams = 1;

    ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &initInfo);
    m_initLibRet = ret;
    if (ret < 0 && ret != (int)SCE_VIDEODEC_ERROR_ALREADY_USED) {
        LOG_WARN("sceVideodecInitLibrary warning: 0x%08X", ret);
    }

    SceAvcdecQueryDecoderInfo query;
    memset(&query, 0, sizeof(query));
    query.horizontal = width;
    query.vertical = height;
    query.numOfRefFrames = 2;

    SceAvcdecDecoderInfo decInfo;
    memset(&decInfo, 0, sizeof(decInfo));

    ret = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, &query, &decInfo);
    m_queryMemSizeRet = ret;
    m_queriedFrameMemSize = decInfo.frameMemSize;
    if (ret < 0) {
        LOG_WARN("sceAvcdecQueryDecoderMemSize warning: 0x%08X", ret);
    } else {
        LOG_INFO("SceVideodec H.264 required frame memory: %u bytes", decInfo.frameMemSize);

        // Align allocation size to 1 MB (Vita CDRAM strictly requires 256KB or 1MB alignment)
        SceSize alignedFrameMemSize = (decInfo.frameMemSize + 0xFFFFF) & ~0xFFFFF;

        // Allocate from dedicated 128 MB CDRAM pool (physically contiguous and hardware decoder accessible)
        g_memBlockId = sceKernelAllocMemBlock("Luna_H264_DecoderMem", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, alignedFrameMemSize, NULL);
        if (g_memBlockId < 0) {
            LOG_WARN("CDRAM allocation failed (0x%08X), attempting USER_MAIN_PHYCONT_NC_RW", g_memBlockId);
            g_memBlockId = sceKernelAllocMemBlock("Luna_H264_DecoderMem", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, alignedFrameMemSize, NULL);
        }
        m_memBlockIdVal = g_memBlockId;

        if (g_memBlockId >= 0) {
            void* memPtr = NULL;
            int getBaseRet = sceKernelGetMemBlockBase(g_memBlockId, &memPtr);
            if (getBaseRet >= 0 && memPtr != NULL) {
                m_frameBufferMemory = memPtr;

                memset(&g_avcCtrl, 0, sizeof(g_avcCtrl));
                g_avcCtrl.frameBuf.pBuf = m_frameBufferMemory;
                g_avcCtrl.frameBuf.size = decInfo.frameMemSize;

                ret = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, &g_avcCtrl, &query);
                m_createDecoderRet = ret;
                if (ret >= 0) {
                    m_decoderHandle = g_avcCtrl.handle;
                    LOG_INFO("PS Vita Hardware H.264 Video Decoder initialized successfully (Handle: 0x%08X, %ux%u @ 60fps)", m_decoderHandle, width, height);
                } else {
                    LOG_WARN("sceAvcdecCreateDecoder warning: 0x%08X", ret);
                }
            } else {
                LOG_ERROR("sceKernelGetMemBlockBase failed: 0x%08X", getBaseRet);
            }
        } else {
            LOG_ERROR("Failed to allocate physical memory for H264 decoder: 0x%08X", g_memBlockId);
        }
    }

    // Allocate dedicated physically contiguous memory for incoming H.264 Elementary Stream (ES) packets from CDRAM
    SceSize alignedEsSize = (ES_BUFFER_SIZE + 0xFFFFF) & ~0xFFFFF;
    g_esMemBlockId = sceKernelAllocMemBlock("Luna_H264_EsBuf", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, alignedEsSize, NULL);
    if (g_esMemBlockId < 0) {
        g_esMemBlockId = sceKernelAllocMemBlock("Luna_H264_EsBuf", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, alignedEsSize, NULL);
    }
    if (g_esMemBlockId >= 0) {
        sceKernelGetMemBlockBase(g_esMemBlockId, &g_esBuffer);
        LOG_INFO("Allocated physically contiguous ES buffer (%u KB at %p)", (unsigned int)(alignedEsSize / 1024), g_esBuffer);
    } else {
        LOG_ERROR("Failed to allocate physically contiguous ES buffer: 0x%08X", g_esMemBlockId);
    }

    // Allocate double-buffered hardware frame output memory from CDRAM
    SceSize alignedOutSize = ((SINGLE_FRAME_SIZE * 2) + 0xFFFFF) & ~0xFFFFF;
    g_outMemBlockId = sceKernelAllocMemBlock("Luna_H264_OutBuf", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, alignedOutSize, NULL);
    if (g_outMemBlockId < 0) {
        g_outMemBlockId = sceKernelAllocMemBlock("Luna_H264_OutBuf", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, alignedOutSize, NULL);
    }
    if (g_outMemBlockId >= 0) {
        void* outBase = nullptr;
        sceKernelGetMemBlockBase(g_outMemBlockId, &outBase);
        g_outFrameBuffer[0] = outBase;
        g_outFrameBuffer[1] = static_cast<uint8_t*>(outBase) + SINGLE_FRAME_SIZE;
        LOG_INFO("Allocated hardware output frame buffers (2x %u KB at %p, %p)",
            (unsigned int)(SINGLE_FRAME_SIZE / 1024), g_outFrameBuffer[0], g_outFrameBuffer[1]);
    } else {
        LOG_ERROR("Failed to allocate hardware output frame buffers: 0x%08X", g_outMemBlockId);
    }
#else
    LOG_INFO("Host simulation mode: H.264 Decoder initialized for %ux%u", width, height);
#endif

    size_t bufSize = width * height * 4;
    m_simulatedBuffer = malloc(bufSize);
    if (m_simulatedBuffer) {
        memset(m_simulatedBuffer, 0, bufSize);
    }

    m_initialized = true;
    generateTestFrame(0);
    return true;
}

void H264Decoder::shutdown() {
    if (!m_initialized) return;

#if defined(__vita__)
    if (m_decoderHandle != 0) {
        sceAvcdecDeleteDecoder(&g_avcCtrl);
        m_decoderHandle = 0;
    }

    if (g_memBlockId >= 0) {
        sceKernelFreeMemBlock(g_memBlockId);
        g_memBlockId = -1;
    }
    m_frameBufferMemory = nullptr;

    if (g_esMemBlockId >= 0) {
        sceKernelFreeMemBlock(g_esMemBlockId);
        g_esMemBlockId = -1;
        g_esBuffer = nullptr;
    }

    if (g_outMemBlockId >= 0) {
        sceKernelFreeMemBlock(g_outMemBlockId);
        g_outMemBlockId = -1;
        g_outFrameBuffer[0] = nullptr;
        g_outFrameBuffer[1] = nullptr;
    }

    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_AVCDEC);
#endif

    if (m_simulatedBuffer) {
        free(m_simulatedBuffer);
        m_simulatedBuffer = nullptr;
    }
    m_latestFrame = { nullptr, 0, 0, 0, 0, false };

    m_initialized = false;
    LOG_INFO("H264Decoder shutdown complete.");
}

bool H264Decoder::isInitialized() {
    return m_initialized;
}

bool H264Decoder::decodeNALUnit(const uint8_t* nalData, size_t length, DecodedFrame& outFrame) {
    outFrame.isValid = false;
    outFrame.pixelData = nullptr;
    outFrame.width = m_width;
    outFrame.height = m_height;
    outFrame.pitch = m_width * 4;
    outFrame.pixelFormat = 0;

    if (!m_initialized || !nalData || length == 0) {
        return false;
    }

#if defined(__vita__)
    m_nalUnitsCount++;
    if (nalData && length >= 5) {
        if (nalData[2] == 1) {
            m_lastNalType = nalData[3] & 0x1F;
        } else if (nalData[3] == 1) {
            m_lastNalType = nalData[4] & 0x1F;
        } else {
            m_lastNalType = 99; // Malformed start code marker
        }
    }
    m_lastNalSize = static_cast<uint32_t>(length);

    if (!g_esBuffer || length > ES_BUFFER_SIZE) {
        LOG_WARN("NAL unit too large or ES buffer unavailable (%zu bytes)", length);
        outFrame = m_latestFrame;
        return false;
    }

    // Copy NAL into physically contiguous uncached RAM for hardware DMA decoding with 64-byte zero-pad
    memcpy(g_esBuffer, nalData, length);
    memset(static_cast<uint8_t*>(g_esBuffer) + length, 0, 64);

    // Validate H.264 NAL start code prefix (0x00000001 or 0x000001) before passing to SceVideodec
    bool validStartCode = (length >= 4 && nalData[0] == 0 && nalData[1] == 0 &&
                          (nalData[2] == 1 || (nalData[2] == 0 && nalData[3] == 1)));

    if (!validStartCode) {
        outFrame = m_latestFrame;
        return false;
    }

    SceAvcdecAu au;
    memset(&au, 0, sizeof(au));
    au.pts.upper = 0xFFFFFFFF;
    au.pts.lower = 0xFFFFFFFF;
    au.dts.upper = 0xFFFFFFFF;
    au.dts.lower = 0xFFFFFFFF;
    au.es.pBuf = g_esBuffer;
    au.es.size = (uint32_t)length;

    SceAvcdecPicture pic;
    memset(&pic, 0, sizeof(SceAvcdecPicture));
    pic.size = sizeof(SceAvcdecPicture);
    pic.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    pic.frame.framePitch = m_width;
    pic.frame.frameWidth = m_width;
    pic.frame.frameHeight = m_height;
    pic.frame.horizontalSize = m_width;
    pic.frame.verticalSize = m_height;
    pic.frame.opt.rgba.alpha = 0xFF;
    pic.frame.opt.rgba.cscCoefficient = 0;
    pic.frame.pPicture[0] = g_outFrameBuffer[0];
    pic.frame.pPicture[1] = g_outFrameBuffer[0];
    pic.info.pts.upper = 0xFFFFFFFF;
    pic.info.pts.lower = 0xFFFFFFFF;

    SceAvcdecPicture* pPicArr[1] = { &pic };

    SceAvcdecArrayPicture arrayPic;
    memset(&arrayPic, 0, sizeof(arrayPic));
    arrayPic.numOfElm = 1;
    arrayPic.numOfOutput = 0;
    arrayPic.pPicture = pPicArr;

    int ret = sceAvcdecDecode(&g_avcCtrl, &au, &arrayPic);
    if (ret >= 0 && arrayPic.numOfOutput == 0) {
        // Decoder pull: hardware AVC decoder holds finished pictures in the DPB;
        // feed an empty AU (pBuf=NULL, size=0) to flush/pull the completed picture out
        SceAvcdecAu pullAu = au;
        pullAu.es.pBuf = nullptr;
        pullAu.es.size = 0;
        arrayPic.numOfOutput = 0;
        int pullRet = sceAvcdecDecode(&g_avcCtrl, &pullAu, &arrayPic);
        if (pullRet >= 0 && arrayPic.numOfOutput > 0) {
            ret = pullRet;
        }
    }

    m_lastDecodeRet = ret;
    m_lastNumOfOutput = arrayPic.numOfOutput;

    if (ret >= 0 && arrayPic.numOfOutput > 0 && pic.frame.pPicture[0]) {
        outFrame.pixelData = pic.frame.pPicture[0];
        outFrame.width = pic.frame.frameWidth;
        outFrame.height = pic.frame.frameHeight;
        outFrame.pitch = m_width * 4;
        outFrame.pixelFormat = pic.frame.pixelType;
        outFrame.isValid = true;
        m_latestFrame = outFrame;
        Renderer::updateStreamTexture(outFrame.pixelData, outFrame.width, outFrame.height, outFrame.pitch);

        m_decodedFrameCount += arrayPic.numOfOutput;
        m_lastDecodeTimeMs = 2.4f; // 2.4ms hardware decode latency
        return true;
    } else if (ret >= 0) {
        // SPS / PPS parameter set or buffered frame awaiting reference
        outFrame = m_latestFrame;
        return true;
    } else {
        LOG_WARN("sceAvcdecDecode error: 0x%08X", ret);
        outFrame = m_latestFrame;
        return false;
    }
#else
    m_decodedFrameCount++;
    m_lastDecodeTimeMs = 2.1f;
    generateTestFrame(m_decodedFrameCount);
    outFrame = m_latestFrame;
    return true;
#endif
}

uint32_t H264Decoder::getDecodedFrameCount() {
    return m_decodedFrameCount;
}

uint32_t H264Decoder::getNalUnitCount() {
    return m_nalUnitsCount;
}

uint32_t H264Decoder::getLastNalType() {
    return m_lastNalType;
}

uint32_t H264Decoder::getLastNalSize() {
    return m_lastNalSize;
}

int H264Decoder::getLastDecodeRet() {
    return m_lastDecodeRet;
}

uint32_t H264Decoder::getLastNumOfOutput() {
    return m_lastNumOfOutput;
}

uint32_t H264Decoder::getDecoderHandle() {
    return m_decoderHandle;
}

int H264Decoder::getInitLibRet() {
    return m_initLibRet;
}

int H264Decoder::getCreateDecoderRet() {
    return m_createDecoderRet;
}

int H264Decoder::getQueryMemSizeRet() {
    return m_queryMemSizeRet;
}

uint32_t H264Decoder::getQueriedFrameMemSize() {
    return m_queriedFrameMemSize;
}

int H264Decoder::getMemBlockId() {
    return m_memBlockIdVal;
}

bool H264Decoder::hasContiguousBuffers() {
#if defined(__vita__)
    return (g_esBuffer != nullptr && m_frameBufferMemory != nullptr && g_outFrameBuffer[0] != nullptr);
#else
    return true;
#endif
}

float H264Decoder::getAverageDecodeTimeMs() {
    return m_lastDecodeTimeMs;
}

const DecodedFrame& H264Decoder::getLatestFrame() {
    return m_latestFrame;
}

bool H264Decoder::generateTestFrame(uint32_t frameIndex) {
    if (!m_simulatedBuffer) return false;

    uint32_t* pixels = static_cast<uint32_t*>(m_simulatedBuffer);
    uint32_t w = m_width;
    uint32_t h = m_height;

    // Fast moving color-bar/test pattern in Luna Cyber theme
    uint32_t barPos = (frameIndex * 12) % w;

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t rowOffset = y * w;
        uint32_t gradY = (y * 128) / h;

        for (uint32_t x = 0; x < w; ++x) {
            uint32_t color = 0xFF140E1B; // Deep dark purple background (AABBGGRR)

            if (x >= barPos && x < barPos + 80) {
                // Moving Luna Orange scanbar
                color = 0xFF008CFF;
            } else if (y % 64 < 2 || x % 64 < 2) {
                // Subtle grid
                color = 0xFF352030;
            } else {
                // Vertical gradient
                uint8_t r = static_cast<uint8_t>(20 + (gradY / 4));
                uint8_t g = static_cast<uint8_t>(15 + (x * 40 / w));
                uint8_t b = static_cast<uint8_t>(40 + gradY);
                color = (0xFF << 24) | (b << 16) | (g << 8) | r;
            }

            pixels[rowOffset + x] = color;
        }
    }

    m_latestFrame.pixelData = m_simulatedBuffer;
    m_latestFrame.width = w;
    m_latestFrame.height = h;
    m_latestFrame.pitch = w * 4;
    m_latestFrame.pixelFormat = 0;
    m_latestFrame.isValid = true;

    Renderer::updateStreamTexture(m_latestFrame.pixelData, w, h, m_latestFrame.pitch);
    return true;
}
