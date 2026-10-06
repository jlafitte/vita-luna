#include "ui/diagnostic_overlay.h"
#include "graphics/renderer.h"

bool DiagnosticOverlay::m_visible = false;
float DiagnosticOverlay::m_currentFps = 60.0f;
int DiagnosticOverlay::m_frameCount = 0;
float DiagnosticOverlay::m_fpsTimer = 0.0f;

int DiagnosticOverlay::m_streamWidth = LUNA_TARGET_WIDTH;
int DiagnosticOverlay::m_streamHeight = LUNA_TARGET_HEIGHT;
float DiagnosticOverlay::m_streamFps = 60.0f;
int DiagnosticOverlay::m_bitrateKbps = 10000;
float DiagnosticOverlay::m_decodeLatencyMs = 8.4f;

void DiagnosticOverlay::init() {
    m_visible = false;
    m_currentFps = 60.0f;
    m_frameCount = 0;
    m_fpsTimer = 0.0f;
}

void DiagnosticOverlay::update(float deltaTime) {
    m_frameCount++;
    m_fpsTimer += deltaTime;
    if (m_fpsTimer >= 1.0f) {
        m_currentFps = (float)m_frameCount / m_fpsTimer;
        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }
}

void DiagnosticOverlay::toggleVisibility() {
    m_visible = !m_visible;
}

bool DiagnosticOverlay::isVisible() {
    return m_visible;
}

void DiagnosticOverlay::setStreamStats(int width, int height, float fps, int bitrateKbps, float decodeLatencyMs) {
    m_streamWidth = width;
    m_streamHeight = height;
    m_streamFps = fps;
    m_bitrateKbps = bitrateKbps;
    m_decodeLatencyMs = decodeLatencyMs;
}

void DiagnosticOverlay::render() {
    if (!m_visible) return;

    // Semi-transparent dark box on top-right
    float boxWidth = 280.0f;
    float boxHeight = 150.0f;
    float posX = VITA_SCREEN_WIDTH - boxWidth - 10.0f;
    float posY = 50.0f;

    Renderer::drawRect(posX, posY, boxWidth, boxHeight, RGBA8(10, 14, 24, 220));
    Renderer::drawRectOutline(posX, posY, boxWidth, boxHeight, 1.5f, RGBA8(0, 200, 255, 255));

    uint32_t textCol = RGBA8(240, 245, 255, 255);
    uint32_t highlightCol = RGBA8(0, 230, 180, 255);

    Renderer::drawText(posX + 10, posY + 22, highlightCol, "--- LUNA PERFORMANCE STATS ---");
    Renderer::drawTextFormatted(posX + 10, posY + 45, textCol, "UI Render FPS:    %.1f FPS", m_currentFps);
    Renderer::drawTextFormatted(posX + 10, posY + 67, textCol, "Stream Profile:   %dx%d @ %.0f FPS", m_streamWidth, m_streamHeight, m_streamFps);
    Renderer::drawTextFormatted(posX + 10, posY + 89, textCol, "Display Output:   960x544 (Downscaled)");
    Renderer::drawTextFormatted(posX + 10, posY + 111, textCol, "Bitrate Target:   %d Mbps", m_bitrateKbps / 1000);
    Renderer::drawTextFormatted(posX + 10, posY + 133, textCol, "Hardware Decode:  %.1f ms", m_decodeLatencyMs);
}
