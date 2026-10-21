#include "net/srtp_decryptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool SrtpDecryptor::m_initialized = false;
bool SrtpDecryptor::m_hasKeys = false;
uint64_t SrtpDecryptor::m_packetsDecrypted = 0;

uint8_t SrtpDecryptor::m_sessionKey[16] = {0};
uint8_t SrtpDecryptor::m_sessionSalt[14] = {0};
uint32_t SrtpDecryptor::m_roundKeys[44] = {0};

// FIPS-197 Standard AES S-Box
static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5e, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint32_t RCON[11] = {
    0x00000000, 0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000
};

static inline uint32_t rotWord(uint32_t w) {
    return (w << 8) | (w >> 24);
}

static inline uint32_t subWord(uint32_t w) {
    return (static_cast<uint32_t>(SBOX[(w >> 24) & 0xFF]) << 24) |
           (static_cast<uint32_t>(SBOX[(w >> 16) & 0xFF]) << 16) |
           (static_cast<uint32_t>(SBOX[(w >>  8) & 0xFF]) <<  8) |
           (static_cast<uint32_t>(SBOX[ w        & 0xFF]));
}

static inline uint8_t xtime(uint8_t x) {
    return (x << 1) ^ (((x >> 7) & 1) * 0x1B);
}

void SrtpDecryptor::aes128KeyExpansion(const uint8_t* key, uint32_t* roundKeys) {
    for (int i = 0; i < 4; ++i) {
        roundKeys[i] = (static_cast<uint32_t>(key[4 * i]) << 24) |
                       (static_cast<uint32_t>(key[4 * i + 1]) << 16) |
                       (static_cast<uint32_t>(key[4 * i + 2]) << 8) |
                       (static_cast<uint32_t>(key[4 * i + 3]));
    }
    for (int i = 4; i < 44; ++i) {
        uint32_t temp = roundKeys[i - 1];
        if (i % 4 == 0) {
            temp = subWord(rotWord(temp)) ^ RCON[i / 4];
        }
        roundKeys[i] = roundKeys[i - 4] ^ temp;
    }
}

void SrtpDecryptor::aes128EncryptBlock(const uint32_t* roundKeys, const uint8_t* in, uint8_t* out) {
    uint8_t state[4][4];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            state[j][i] = in[i * 4 + j] ^ (roundKeys[i] >> (24 - j * 8));
        }
    }

    for (int round = 1; round <= 10; ++round) {
        // SubBytes
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                state[i][j] = SBOX[state[i][j]];
            }
        }

        // ShiftRows
        uint8_t temp;
        // Row 1
        temp = state[1][0];
        state[1][0] = state[1][1]; state[1][1] = state[1][2];
        state[1][2] = state[1][3]; state[1][3] = temp;
        // Row 2
        temp = state[2][0]; uint8_t temp2 = state[2][1];
        state[2][0] = state[2][2]; state[2][1] = state[2][3];
        state[2][2] = temp;        state[2][3] = temp2;
        // Row 3
        temp = state[3][3];
        state[3][3] = state[3][2]; state[3][2] = state[3][1];
        state[3][1] = state[3][0]; state[3][0] = temp;

        // MixColumns (rounds 1 to 9)
        if (round < 10) {
            for (int i = 0; i < 4; ++i) {
                uint8_t a = state[0][i], b = state[1][i], c = state[2][i], d = state[3][i];
                uint8_t a2 = xtime(a), b2 = xtime(b), c2 = xtime(c), d2 = xtime(d);
                state[0][i] = a2 ^ (b ^ b2) ^ c ^ d;
                state[1][i] = a ^ b2 ^ (c ^ c2) ^ d;
                state[2][i] = a ^ b ^ c2 ^ (d ^ d2);
                state[3][i] = (a ^ a2) ^ b ^ c ^ d2;
            }
        }

        // AddRoundKey
        for (int i = 0; i < 4; ++i) {
            uint32_t rk = roundKeys[round * 4 + i];
            state[0][i] ^= (rk >> 24) & 0xFF;
            state[1][i] ^= (rk >> 16) & 0xFF;
            state[2][i] ^= (rk >>  8) & 0xFF;
            state[3][i] ^= (rk      ) & 0xFF;
        }
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = state[j][i];
        }
    }
}

bool SrtpDecryptor::init() {
    m_initialized = true;
    m_hasKeys = false;
    m_packetsDecrypted = 0;
    LOG_INFO("SrtpDecryptor native AES-128-ICM cryptographic engine initialized.");
    return true;
}

void SrtpDecryptor::shutdown() {
    m_hasKeys = false;
    m_packetsDecrypted = 0;
    m_initialized = false;
    memset(m_sessionKey, 0, sizeof(m_sessionKey));
    memset(m_sessionSalt, 0, sizeof(m_sessionSalt));
    memset(m_roundKeys, 0, sizeof(m_roundKeys));
    LOG_INFO("SrtpDecryptor shutdown complete.");
}

bool SrtpDecryptor::setSessionKeys(const uint8_t* masterKey, const uint8_t* masterSalt) {
    if (!masterKey || !masterSalt) return false;

    memcpy(m_sessionKey, masterKey, 16);
    memcpy(m_sessionSalt, masterSalt, 14);

    aes128KeyExpansion(m_sessionKey, m_roundKeys);
    m_hasKeys = true;
    LOG_INFO("SrtpDecryptor: Session master key and salt established for native SRTP.");
    return true;
}

bool SrtpDecryptor::unprotectRtp(uint8_t* packetData, size_t& inOutLength, size_t& outPayloadOffset) {
    if (!packetData || inOutLength < 12) return false;

    // Validate RTP v2 header
    uint8_t version = (packetData[0] >> 6) & 0x03;
    if (version != 2) return false;

    uint8_t cc = packetData[0] & 0x0F;
    size_t headerLen = 12 + cc * 4;

    // Check for extension header (X bit)
    if (packetData[0] & 0x10) {
        if (headerLen + 4 > inOutLength) return false;
        uint16_t extLen = (static_cast<uint16_t>(packetData[headerLen + 2]) << 8) | packetData[headerLen + 3];
        headerLen += 4 + extLen * 4;
    }

    if (headerLen >= inOutLength) return false;
    outPayloadOffset = headerLen;

    // If session keys are not yet negotiated, pass through as unencrypted test stream
    if (!m_hasKeys) {
        return true;
    }

    // Extract SSRC and Sequence Number for RFC 3711 ICM Counter formation
    uint32_t ssrc = (static_cast<uint32_t>(packetData[8]) << 24) |
                    (static_cast<uint32_t>(packetData[9]) << 16) |
                    (static_cast<uint32_t>(packetData[10]) << 8) |
                    (static_cast<uint32_t>(packetData[11]));

    uint16_t seq = (static_cast<uint16_t>(packetData[2]) << 8) | packetData[3];

    // RFC 3711 Section 4.1.1: IV = (salt * 2^16) XOR (SSRC * 2^64) XOR (packet_index * 2^16)
    uint8_t iv[16] = {0};
    memcpy(iv, m_sessionSalt, 14);

    iv[4] ^= (ssrc >> 24) & 0xFF;
    iv[5] ^= (ssrc >> 16) & 0xFF;
    iv[6] ^= (ssrc >>  8) & 0xFF;
    iv[7] ^= (ssrc      ) & 0xFF;

    iv[12] ^= (seq >> 8) & 0xFF;
    iv[13] ^= (seq     ) & 0xFF;

    // In-place AES-128-CTR Decryption of payload
    size_t payloadLen = inOutLength - headerLen;
    // Strip standard 10-byte HMAC-SHA1 auth tag if present
    if (payloadLen > 10) {
        payloadLen -= 10;
        inOutLength -= 10;
    }

    uint8_t* payload = packetData + headerLen;
    uint8_t keystream[16];
    uint16_t counter = 0;

    for (size_t i = 0; i < payloadLen; i += 16) {
        iv[14] = (counter >> 8) & 0xFF;
        iv[15] = (counter     ) & 0xFF;
        counter++;

        aes128EncryptBlock(m_roundKeys, iv, keystream);

        size_t blockLen = (payloadLen - i < 16) ? (payloadLen - i) : 16;
        for (size_t j = 0; j < blockLen; ++j) {
            payload[i + j] ^= keystream[j];
        }
    }

    m_packetsDecrypted++;
    return true;
}

bool SrtpDecryptor::isKeyConfigured() {
    return m_hasKeys;
}

uint64_t SrtpDecryptor::getPacketsDecryptedCount() {
    return m_packetsDecrypted;
}
