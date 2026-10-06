#include "net/network_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__)
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/libssl.h>
#include <psp2/net/http.h>
#include <psp2/sysmodule.h>
#endif

bool NetworkManager::m_initialized = false;
int NetworkManager::m_httpTemplateId = -1;

#if defined(__vita__)
static char g_net_memory[1 * 1024 * 1024]; // 1MB buffer for SceNet
static char g_ssl_memory[300 * 1024];      // 300KB buffer for SceSsl
#endif

bool NetworkManager::init() {
    if (m_initialized) return true;

#if defined(__vita__)
    SceNetInitParam netParam;
    memset(&netParam, 0, sizeof(netParam));
    netParam.memory = g_net_memory;
    netParam.size = sizeof(g_net_memory);
    netParam.flags = 0;

    int ret = sceNetInit(&netParam);
    if (ret < 0) {
        LOG_ERROR("sceNetInit failed: 0x%08X", ret);
        return false;
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        LOG_WARN("sceNetCtlInit warning: 0x%08X", ret);
    }

    ret = sceSslInit(sizeof(g_ssl_memory));
    if (ret < 0) {
        LOG_ERROR("sceSslInit failed: 0x%08X", ret);
        return false;
    }

    ret = sceHttpInit(1 * 1024 * 1024); // 1MB pool for HTTP
    if (ret < 0) {
        LOG_ERROR("sceHttpInit failed: 0x%08X", ret);
        return false;
    }

    m_httpTemplateId = sceHttpCreateTemplate("vita-luna/" VITA_LUNA_VERSION_STR, SCE_HTTP_VERSION_1_1, SCE_TRUE);
    if (m_httpTemplateId < 0) {
        LOG_ERROR("sceHttpCreateTemplate failed: 0x%08X", m_httpTemplateId);
        return false;
    }
#endif

    m_initialized = true;
    LOG_INFO("NetworkManager initialized successfully.");
    return true;
}

void NetworkManager::shutdown() {
    if (!m_initialized) return;

#if defined(__vita__)
    if (m_httpTemplateId >= 0) {
        sceHttpDeleteTemplate(m_httpTemplateId);
        m_httpTemplateId = -1;
    }

    sceHttpTerm();
    sceSslEnd();
    sceNetCtlTerm();
    sceNetTerm();
#endif

    m_initialized = false;
    LOG_INFO("NetworkManager shutdown complete.");
}

bool NetworkManager::isConnected() {
    if (!m_initialized) return false;

#if defined(__vita__)
    int state = 0;
    int ret = sceNetCtlInetGetState(&state);
    if (ret >= 0 && state == SCE_NETCTL_STATE_CONNECTED) {
        return true;
    }
    return false;
#else
    return true;
#endif
}

std::string NetworkManager::getIPAddress() {
#if defined(__vita__)
    SceNetCtlInfo info;
    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0) {
        return std::string(info.ip_address);
    }
#endif
    return "127.0.0.1";
}

HttpResponse NetworkManager::sendHttpRequest(
    const char* url,
    const char* method,
    const char* postData,
    const std::map<std::string, std::string>& customHeaders
) {
    HttpResponse response;
    response.statusCode = 0;
    response.success = false;

    if (!url || !method) {
        response.errorMessage = "Invalid URL or HTTP method";
        return response;
    }

#if defined(__vita__)
    if (m_httpTemplateId < 0) {
        response.errorMessage = "Network template not initialized";
        return response;
    }

    int httpMethod = SCE_HTTP_METHOD_GET;
    if (strcmp(method, "POST") == 0) httpMethod = SCE_HTTP_METHOD_POST;
    else if (strcmp(method, "PUT") == 0) httpMethod = SCE_HTTP_METHOD_PUT;
    else if (strcmp(method, "DELETE") == 0) httpMethod = SCE_HTTP_METHOD_DELETE;

    uint64_t postLength = postData ? strlen(postData) : 0;
    int connId = sceHttpCreateConnectionWithURL(m_httpTemplateId, url, SCE_TRUE);
    if (connId < 0) {
        response.errorMessage = "Failed to create HTTP connection";
        return response;
    }

    int reqId = sceHttpCreateRequestWithURL(connId, httpMethod, url, postLength);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        response.errorMessage = "Failed to create HTTP request";
        return response;
    }

    // Set Custom Headers
    for (const auto& pair : customHeaders) {
        sceHttpAddRequestHeader(reqId, pair.first.c_str(), pair.second.c_str(), SCE_HTTP_HEADER_ADD);
    }

    // Send Request
    int ret = 0;
    if (postData && postLength > 0) {
        ret = sceHttpSendRequest(reqId, postData, postLength);
    } else {
        ret = sceHttpSendRequest(reqId, NULL, 0);
    }

    if (ret < 0) {
        sceHttpDeleteRequest(reqId);
        sceHttpDeleteConnection(connId);
        response.errorMessage = "Failed to send HTTP request";
        return response;
    }

    // Read HTTP Status Code
    int statusCode = 0;
    sceHttpGetStatusCode(reqId, &statusCode);
    response.statusCode = statusCode;

    // Read Response Body
    char buffer[4096];
    std::string responseBody;
    while (true) {
        int readBytes = sceHttpReadData(reqId, buffer, sizeof(buffer) - 1);
        if (readBytes < 0) {
            break; // Read error or finished
        }
        if (readBytes == 0) {
            break; // EOF
        }
        buffer[readBytes] = '\0';
        responseBody.append(buffer, readBytes);
    }

    response.body = responseBody;
    response.success = (statusCode >= 200 && statusCode < 300);

    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
#else
    // Simulated dev response for host testing
    (void)url; (void)method; (void)postData; (void)customHeaders;
    response.statusCode = 200;
    response.success = true;
    response.body = "{\"status\":\"ok\"}";
#endif

    return response;
}
