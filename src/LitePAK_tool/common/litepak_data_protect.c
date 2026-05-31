#include "litepak_internal.h"
#include <string.h>

static uint32_t litepak_rotl32_local(uint32_t v, unsigned r) {
    return (uint32_t)((v << r) | (v >> (32u - r)));
}

static uint32_t litepak_mix32_local(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint8_t litepak_stream_byte(const uint8_t key[32], uint32_t domain, size_t i) {
    uint32_t k = (uint32_t)key[i & 31u] |
                 ((uint32_t)key[(i + 7u) & 31u] << 8) |
                 ((uint32_t)key[(i + 13u) & 31u] << 16) |
                 ((uint32_t)key[(i + 21u) & 31u] << 24);
    uint32_t x = domain ^ (uint32_t)(i * 0x9e3779b9u) ^ k ^
                 litepak_rotl32_local((uint32_t)(i + 0x6d2b79f5u), (unsigned)((i & 15u) + 1u));
    x = litepak_mix32_local(x);
    return (uint8_t)(((x >> ((i & 3u) * 8u)) & 0xffu) ^ key[(i * 5u + 11u) & 31u]);
}

static int litepak_decrypt_with_key(const uint8_t* enc, size_t len, uint32_t domain,
                                    const uint8_t key[32], uint8_t* out) {
    if ((!enc && len) || (!out && len))
        return -1;
    for (size_t i = 0; i < len; ++i)
        out[i] = (uint8_t)(enc[i] ^ litepak_stream_byte(key, domain, i));
    return 0;
}

int litepak_data_decrypt_block(const uint8_t* enc, size_t len, uint32_t domain, uint8_t* out) {
    uint8_t key[32];
    int ret;
    litepak_materialize_data_key(key);
    ret = litepak_decrypt_with_key(enc, len, domain, key, out);
    litepak_secure_bzero(key, sizeof(key));
    return ret;
}

int litepak_vm_decrypt_block(const uint8_t* enc, size_t len, uint32_t domain, uint8_t* out) {
    uint8_t key[32];
    int ret;
    litepak_materialize_vm_key(key);
    ret = litepak_decrypt_with_key(enc, len, domain, key, out);
    litepak_secure_bzero(key, sizeof(key));
    return ret;
}
