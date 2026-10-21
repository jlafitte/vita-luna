#ifndef VITA_LUNA_RENDERER_H
#define VITA_LUNA_RENDERER_H

#include "common.h"
#include <stdint.h>

// RGBA Color Helper
#define RGBA8(r, g, b, a) (((a) << 24) | ((b) << 16) | ((g) << 8) | (r))

class Renderer {
public:
    static bool init();
    static void shutdown();
    
    static void startFrame();
    static void endFrame();
    
    static void clearScreen(uint32_t color);
    static void drawBackground();
    static void drawRect(float x, float y, float width, float height, uint32_t color);
    static void drawRectOutline(float x, float y, float width, float height, float thickness, uint32_t color);
    static void drawText(float x, float y, uint32_t color, const char* text);
    static void drawTextFormatted(float x, float y, uint32_t color, const char* format, ...);
    
    static void drawHeader(const char* title);
    static void drawFooter(const char* promptText);
    
    // Live Video Stream Texture Management
    static bool createStreamTexture(uint32_t width = LUNA_TARGET_WIDTH, uint32_t height = LUNA_TARGET_HEIGHT);
    static void destroyStreamTexture();
    static bool updateStreamTexture(const void* pixelData, uint32_t width, uint32_t height, uint32_t pitch);
    static void drawStreamTexture(float x, float y, float width, float height);
    static bool hasStreamTexture();

private:
    static bool m_initialized;
    static uint32_t m_streamTexWidth;
    static uint32_t m_streamTexHeight;
};

#endif // VITA_LUNA_RENDERER_H
