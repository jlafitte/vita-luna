#include "graphics/renderer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(__vita__)
#include <vita2d.h>
#include <psp2/pgf.h>
#endif

bool Renderer::m_initialized = false;
#if defined(__vita__)
static vita2d_pgf* g_font = nullptr;
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
    if (!text) return;

#if defined(__vita__)
    if (g_font) {
        vita2d_pgf_draw_text(g_font, (int)x, (int)y, color, 1.0f, text);
    }
#else
    (void)x; (void)y; (void)color; (void)text;
#endif
}

void Renderer::drawTextFormatted(float x, float y, uint32_t color, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    drawText(x, y, color, buffer);
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
