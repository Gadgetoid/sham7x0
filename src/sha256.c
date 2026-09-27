#include <string.h>

#include "sha256.h"

static const uint32_t ROUND_CONSTANTS[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate_right(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void compress(sha256_t *context, const uint8_t *block) {
    uint32_t schedule[64];
    for (int i = 0; i < 16; i++) {
        schedule[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t small0 = rotate_right(schedule[i - 15], 7) ^ rotate_right(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
        uint32_t small1 = rotate_right(schedule[i - 2], 17) ^ rotate_right(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + small0 + schedule[i - 7] + small1;
    }
    uint32_t a = context->state[0], b = context->state[1], c = context->state[2], d = context->state[3];
    uint32_t e = context->state[4], f = context->state[5], g = context->state[6], h = context->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t big1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + big1 + choose + ROUND_CONSTANTS[i] + schedule[i];
        uint32_t big0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = big0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void sha256_init(sha256_t *context) {
    static const uint32_t initial[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(context->state, initial, sizeof initial);
    context->length = 0;
    context->block_used = 0;
}

void sha256_update(sha256_t *context, const void *data, size_t size) {
    const uint8_t *bytes = data;
    context->length += size;
    while (size) {
        size_t take = sizeof context->block - context->block_used;
        if (take > size) take = size;
        memcpy(context->block + context->block_used, bytes, take);
        context->block_used += take;
        bytes += take;
        size -= take;
        if (context->block_used == sizeof context->block) {
            compress(context, context->block);
            context->block_used = 0;
        }
    }
}

void sha256_final(sha256_t *context, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint64_t bits = context->length * 8;
    uint8_t padding = 0x80;
    sha256_update(context, &padding, 1);
    padding = 0;
    while (context->block_used != 56) sha256_update(context, &padding, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - i * 8));
    sha256_update(context, length, sizeof length);
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}
