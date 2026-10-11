#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("command_key.h")
#include "command_key.h"
#define COMMAND_AUTH_KEY_AVAILABLE 1
#endif
#endif

#ifndef COMMAND_AUTH_KEY_AVAILABLE
#define COMMAND_AUTH_KEY_AVAILABLE 0
#endif

namespace CommandAuth
{
    constexpr uint8_t kTagLength = 16;
    constexpr uint8_t kKeyLength = 32;

    inline uint32_t rotateRight(uint32_t value, uint8_t count)
    {
        return (value >> count) | (value << (32 - count));
    }

    inline void compress(uint32_t state[8], const uint8_t block[64])
    {
        static const uint32_t constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        uint32_t words[64];
        for (uint8_t i = 0; i < 16; ++i)
        {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       block[i * 4 + 3];
        }
        for (uint8_t i = 16; i < 64; ++i)
        {
            const uint32_t s0 = rotateRight(words[i - 15], 7) ^
                                rotateRight(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = rotateRight(words[i - 2], 17) ^
                                rotateRight(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (uint8_t i = 0; i < 64; ++i)
        {
            const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choice + constants[i] + words[i];
            const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    inline void sha256(const uint8_t *input, size_t inputLength, uint8_t output[32])
    {
        uint32_t state[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        size_t offset = 0;
        while (inputLength - offset >= 64)
        {
            compress(state, input + offset);
            offset += 64;
        }

        uint8_t finalBlocks[128] = {0};
        const size_t remaining = inputLength - offset;
        for (size_t i = 0; i < remaining; ++i)
        {
            finalBlocks[i] = input[offset + i];
        }
        finalBlocks[remaining] = 0x80;
        const size_t paddedLength = remaining < 56 ? 64 : 128;
        const uint64_t bitLength = static_cast<uint64_t>(inputLength) * 8;
        for (uint8_t i = 0; i < 8; ++i)
        {
            finalBlocks[paddedLength - 8 + i] = static_cast<uint8_t>(bitLength >> (56 - i * 8));
        }
        compress(state, finalBlocks);
        if (paddedLength == 128)
        {
            compress(state, finalBlocks + 64);
        }

        for (uint8_t i = 0; i < 8; ++i)
        {
            output[i * 4] = static_cast<uint8_t>(state[i] >> 24);
            output[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
            output[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
            output[i * 4 + 3] = static_cast<uint8_t>(state[i]);
        }
    }

    inline void hmacSha256(const uint8_t key[kKeyLength], const uint8_t *message,
                           size_t messageLength, uint8_t output[32])
    {
        uint8_t innerMessage[64 + 48];
        uint8_t outerMessage[96];
        for (uint8_t i = 0; i < 64; ++i)
        {
            const uint8_t keyByte = i < kKeyLength ? key[i] : 0;
            innerMessage[i] = keyByte ^ 0x36;
            outerMessage[i] = keyByte ^ 0x5c;
        }
        for (size_t i = 0; i < messageLength; ++i)
        {
            innerMessage[64 + i] = message[i];
        }

        uint8_t innerHash[32];
        sha256(innerMessage, 64 + messageLength, innerHash);
        for (uint8_t i = 0; i < 32; ++i)
        {
            outerMessage[64 + i] = innerHash[i];
        }
        sha256(outerMessage, sizeof(outerMessage), output);
    }

    inline bool verifyTag(const uint8_t key[kKeyLength], const uint8_t *message,
                          size_t messageLength, const uint8_t *tag)
    {
        if (key == nullptr || message == nullptr || tag == nullptr || messageLength > 48)
        {
            return false;
        }

        uint8_t digest[32];
        hmacSha256(key, message, messageLength, digest);
        uint8_t difference = 0;
        for (uint8_t i = 0; i < kTagLength; ++i)
        {
            difference |= digest[i] ^ tag[i];
        }
        return difference == 0;
    }
}
