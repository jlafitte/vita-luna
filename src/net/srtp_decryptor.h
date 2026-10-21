#ifndef VITA_LUNA_SRTP_DECRYPTOR_H
#define VITA_LUNA_SRTP_DECRYPTOR_H

#include "common.h"
#include <cstdint>
#include <cstddef>

class SrtpDecryptor {
public:
    static bool init();
    static void shutdown();

    // Configure 128-bit master key (16 bytes) and master salt (14 bytes) from signaling
    static bool setSessionKeys(const uint8_t* masterKey, const uint8_t* masterSalt);

    // In-place RFC 3711 SRTP unprotection (AES-128-ICM decryption and tag stripping)
    // Returns true if packet is valid (or unencrypted fallback), adjusts length to payload size
    static bool unprotectRtp(uint8_t* packetData, size_t& inOutLength, size_t& outPayloadOffset);

    static bool isKeyConfigured();
    static uint64_t getPacketsDecryptedCount();

private:
    static bool m_initialized;
    static bool m_hasKeys;
    static uint64_t m_packetsDecrypted;

    // Derived session keys
    static uint8_t m_sessionKey[16];
    static uint8_t m_sessionSalt[14];
    static uint32_t m_roundKeys[44]; // AES-128 expanded keys

    // AES-128 core helpers
    static void aes128KeyExpansion(const uint8_t* key, uint32_t* roundKeys);
    static void aes128EncryptBlock(const uint32_t* roundKeys, const uint8_t* in, uint8_t* out);
};

#endif // VITA_LUNA_SRTP_DECRYPTOR_H
