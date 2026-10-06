#ifndef VITA_LUNA_NETWORK_MANAGER_H
#define VITA_LUNA_NETWORK_MANAGER_H

#include "common.h"
#include <string>
#include <vector>
#include <map>

struct HttpResponse {
    int statusCode;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success;
    std::string errorMessage;
};

class NetworkManager {
public:
    static bool init();
    static void shutdown();
    
    static bool isConnected();
    static std::string getIPAddress();
    
    // Synchronous HTTPS Request Wrapper
    static HttpResponse sendHttpRequest(
        const char* url,
        const char* method,
        const char* postData = nullptr,
        const std::map<std::string, std::string>& customHeaders = {}
    );

private:
    static bool m_initialized;
    static int m_httpTemplateId;
};

#endif // VITA_LUNA_NETWORK_MANAGER_H
