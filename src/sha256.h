#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t  block[64];
    size_t   block_used;
} sha256_t;

void sha256_init(sha256_t *context);
void sha256_update(sha256_t *context, const void *data, size_t size);
void sha256_final(sha256_t *context, uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif
