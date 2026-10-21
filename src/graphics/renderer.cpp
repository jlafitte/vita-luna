#include "graphics/renderer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(__vita__)
#include <vita2d.h>
#include <psp2/pgf.h>
#endif

bool Renderer::m_initialized = false;
uint32_t Renderer::m_streamTexWidth = 0;
uint32_t Renderer::m_streamTexHeight = 0;

#if defined(__vita__)
static vita2d_pgf* g_font = nullptr;
static vita2d_texture* g_streamTexture = nullptr;
#endif

bool Renderer::init() {
    if (m_initialized) return true;

#if defined(__vita__)
    vita2d_init();
    vita2d_set_clear_color(RGBA8(15, 17, 26, 255));
    g_font = vita2d_load_default_pgf();
    if (!g_font) {
        LOG_WARN("Could not load default PGF font. Text rendering will use fallback.");
    }
#endif

    m_initialized = true;
    LOG_INFO("Renderer initialized successfully.");
    return true;
}

void Renderer::shutdown() {
    if (!m_initialized) return;

    destroyStreamTexture();

#if defined(__vita__)
    if (g_font) {
        vita2d_free_pgf(g_font);
        g_font = nullptr;
    }
    vita2d_fini();
#endif

    m_initialized = false;
    LOG_INFO("Renderer shutdown complete.");
}

void Renderer::startFrame() {
#if defined(__vita__)
    vita2d_start_drawing();
    vita2d_clear_screen();
#endif
}

void Renderer::endFrame() {
#if defined(__vita__)
    vita2d_end_drawing();
    vita2d_swap_buffers();
#endif
}

void Renderer::clearScreen(uint32_t color) {
#if defined(__vita__)
    vita2d_set_clear_color(color);
    vita2d_clear_screen();
#else
    (void)color;
#endif
}

void Renderer::drawBackground() {
    clearScreen(RGBA8(15, 17, 26, 255));
}

void Renderer::drawRect(float x, float y, float width, float height, uint32_t color) {
#if defined(__vita__)
    vita2d_draw_rectangle(x, y, width, height, color);
#else
    (void)x; (void)y; (void)width; (void)height; (void)color;
#endif
}

void Renderer::drawRectOutline(float x, float y, float width, float height, float thickness, uint32_t color) {
    // Draw 4 borders
    drawRect(x, y, width, thickness, color); // Top
    drawRect(x, y + height - thickness, width, thickness, color); // Bottom
    drawRect(x, y, thickness, height, color); // Left
    drawRect(x + width - thickness, y, thickness, height, color); // Right
}

void Renderer::drawText(float x, float y, uint32_t color, const char* text) {
    if (!text || text[0] == '\0') return;

#if defined(__vita__)
    if (g_font) {
        vita2d_pgf_draw_text(g_font, (int)x, (int)y, color, 1.0f, text);
    }
#else
    (void)x; (void)y; (void)color; (void)text;
#endif
}

void Renderer::drawTextFormatted(float x, float y, uint32_t color, const char* format, ...) {
    if (!format || format[0] == '\0') return;

    char buffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len > 0) {
        buffer[sizeof(buffer) - 1] = '\0';
        drawText(x, y, color, buffer);
    }
}

void Renderer::drawHeader(const char* title) {
    // Dark banner at top
    drawRect(0, 0, VITA_SCREEN_WIDTH, 44, RGBA8(25, 30, 48, 255));
    // Accent line (Luna Orange / Cyan theme)
    drawRect(0, 42, VITA_SCREEN_WIDTH, 2, RGBA8(255, 140, 0, 255));
    
    drawText(20, 28, RGBA8(255, 255, 255, 255), title ? title : "AMAZON LUNA - VITA CLIENT");
    drawText(VITA_SCREEN_WIDTH - 120, 28, RGBA8(180, 190, 210, 255), VITA_LUNA_VERSION_STR);
}

void Renderer::drawFooter(const char* promptText) {
    // Bottom banner
    drawRect(0, VITA_SCREEN_HEIGHT - 32, VITA_SCREEN_WIDTH, 32, RGBA8(20, 24, 38, 255));
    drawRect(0, VITA_SCREEN_HEIGHT - 32, VITA_SCREEN_WIDTH, 1, RGBA8(60, 70, 95, 255));

    if (promptText) {
        drawText(20, VITA_SCREEN_HEIGHT - 10, RGBA8(200, 210, 225, 255), promptText);
    } else {
        drawText(20, VITA_SCREEN_HEIGHT - 10, RGBA8(200, 210, 225, 255), "(X) Select  |  (O) Back  |  (SELECT) Stats  |  (START) Settings");
    }
}

bool Renderer::createStreamTexture(uint32_t width, uint32_t height) {
    m_streamTexWidth = width;
    m_streamTexHeight = height;

#if defined(__vita__)
    if (g_streamTexture) {
        vita2d_free_texture(g_streamTexture);
        g_streamTexture = nullptr;
    }

    g_streamTexture = vita2d_create_empty_texture_format(width, height, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_RGBA);
    if (!g_streamTexture) {
        LOG_ERROR("Failed to allocate vita2d video stream texture (%ux%u)", width, height);
        return false;
    }

    vita2d_texture_set_filters(g_streamTexture, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    LOG_INFO("Allocated hardware video stream texture (%ux%u, RGBA8)", width, height);
#else
    LOG_INFO("Host simulation: Created stream texture %ux%u", width, height);
#endif
    return true;
}

void Renderer::destroyStreamTexture() {
#if defined(__vita__)
    if (g_streamTexture) {
        vita2d_free_texture(g_streamTexture);
        g_streamTexture = nullptr;
        LOG_INFO("Freed hardware video stream texture.");
    }
#endif
    m_streamTexWidth = 0;
    m_streamTexHeight = 0;
}

bool Renderer::updateStreamTexture(const void* pixelData, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!pixelData) return false;

#if defined(__vita__)
    if (!g_streamTexture) return false;

    void* dst = vita2d_texture_get_datap(g_streamTexture);
    if (!dst) return false;

    uint32_t dstStride = vita2d_texture_get_stride(g_streamTexture);
    const uint8_t* srcRow = static_cast<const uint8_t*>(pixelData);
    uint8_t* dstRow = static_cast<uint8_t*>(dst);

    uint32_t copyBytes = width * 4;
    if (copyBytes > pitch && pitch > 0) copyBytes = pitch;
    if (copyBytes > dstStride) copyBytes = dstStride;

    for (uint32_t y = 0; y < height && y < m_streamTexHeight; ++y) {
        memcpy(dstRow + y * dstStride, srcRow + y * pitch, copyBytes);
    }
    return true;
#else
    (void)width; (void)height; (void)pitch;
    return true;
#endif
}

void Renderer::drawStreamTexture(float x, float y, float width, float height) {
#if defined(__vita__)
    if (g_streamTexture) {
        float texW = (float)vita2d_texture_get_width(g_streamTexture);
        float texH = (float)vita2d_texture_get_height(g_streamTexture);
        float scaleX = width / texW;
        float scaleY = height / texH;
        vita2d_draw_texture_scale(g_streamTexture, x, y, scaleX, scaleY);
        return;
    }
#endif

    // Fallback viewport background with subtle scanline accent
    drawRect(x, y, width, height, RGBA8(12, 16, 28, 255));
    drawRectOutline(x, y, width, height, 1.5f, RGBA8(0, 180, 220, 180));
}

bool Renderer::hasStreamTexture() {
#if defined(__vita__)
    return g_streamTexture != nullptr;
#else
    return m_streamTexWidth > 0 && m_streamTexHeight > 0;
#endif
}
