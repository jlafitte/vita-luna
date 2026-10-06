#ifndef VITA_LUNA_API_H
#define VITA_LUNA_API_H

#include "common.h"
#include <string>
#include <vector>

struct LunaGame {
    std::string id;
    std::string title;
    std::string channel;
    std::string publisher;
    bool isEntitled;
    std::string bannerUrl;
};

struct LunaStreamSession {
    std::string sessionId;
    std::string streamUrl;
    std::string rtpHost;
    int rtpVideoPort;
    int rtpAudioPort;
    std::string codec;
    int targetWidth;
    int targetHeight;
    int targetFps;
    bool isConnected;
};

class LunaAPI {
public:
    static bool init();

    static std::vector<LunaGame> fetchCatalog();
    static bool requestStreamSession(const std::string& gameId, LunaStreamSession& outSession);
    static void closeStreamSession(LunaStreamSession& session);

private:
    static bool m_initialized;
};

#endif // VITA_LUNA_API_H
