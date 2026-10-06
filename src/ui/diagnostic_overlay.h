#ifndef VITA_LUNA_DIAGNOSTIC_OVERLAY_H
#define VITA_LUNA_DIAGNOSTIC_OVERLAY_H

#include "common.h"
#include <stdint.h>

class DiagnosticOverlay {
public:
    static void init();
    static void update(float deltaTime);
    static void render();
    
    static void toggleVisibility();
    static bool isVisible();
    
    // Stream Diagnostic Metrics
    static void setStreamStats(int width, int height, float fps, int bitrateKbps, float decodeLatencyMs);

private:
    static bool m_visible;
    static float m_currentFps;
    static int m_frameCount;
    static float m_fpsTimer;
    
    static int m_streamWidth;
    static int m_streamHeight;
    static float m_streamFps;
    static int m_bitrateKbps;
    static float m_decodeLatencyMs;
};

#endif // VITA_LUNA_DIAGNOSTIC_OVERLAY_H
