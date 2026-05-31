/*
 * BLAKE2b 实现 - 基于 RFC 7693 参考实现
 * 支持 personal 参数（用于 LitePAK 的各种哈希派生）
 */
#include "litepak.h"
#include <string.h>

static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint8_t blake2b_sigma[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0},
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3}
};

static inline uint64_t rotr64(uint64_t x, int n) {
    return (x >> n) | (x << (64 - n));
}

static inline uint64_t load64_le(const void* src) {
    const uint8_t* p = (const uint8_t*)src;
    return ((uint64_t)p[0])       | ((uint64_t)p[1] << 8)  |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static inline void store64_le(void* dst, uint64_t val) {
    uint8_t* p = (uint8_t*)dst;
    p[0] = (uint8_t)(val);       p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16); p[3] = (uint8_t)(val >> 24);
    p[4] = (uint8_t)(val >> 32); p[5] = (uint8_t)(val >> 40);
    p[6] = (uint8_t)(val >> 48); p[7] = (uint8_t)(val >> 56);
}

static inline void store32_le(void* dst, uint32_t val) {
    uint8_t* p = (uint8_t*)dst;
    p[0] = (uint8_t)(val);       p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16); p[3] = (uint8_t)(val >> 24);
}

#define G(r, i, a, b, c, d) do { \
    a = a + b + m[blake2b_sigma[r][2*i+0]]; \
    d = rotr64(d ^ a, 32); \
    c = c + d; \
    b = rotr64(b ^ c, 24); \
    a = a + b + m[blake2b_sigma[r][2*i+1]]; \
    d = rotr64(d ^ a, 16); \
    c = c + d; \
    b = rotr64(b ^ c, 63); \
} while(0)

static void blake2b_compress(blake2b_state* S, const uint8_t block[128]) {
    uint64_t m[16];
    uint64_t v[16];

    for (int i = 0; i < 16; i++)
        m[i] = load64_le(block + i * 8);

    for (int i = 0; i < 8; i++)
        v[i] = S->h[i];
    v[8]  = blake2b_IV[0];
    v[9]  = blake2b_IV[1];
    v[10] = blake2b_IV[2];
    v[11] = blake2b_IV[3];
    v[12] = blake2b_IV[4] ^ S->t[0];
    v[13] = blake2b_IV[5] ^ S->t[1];
    v[14] = blake2b_IV[6] ^ S->f[0];
    v[15] = blake2b_IV[7] ^ S->f[1];

    for (int r = 0; r < 12; r++) {
        G(r, 0, v[0], v[4], v[ 8], v[12]);
        G(r, 1, v[1], v[5], v[ 9], v[13]);
        G(r, 2, v[2], v[6], v[10], v[14]);
        G(r, 3, v[3], v[7], v[11], v[15]);
        G(r, 4, v[0], v[5], v[10], v[15]);
        G(r, 5, v[1], v[6], v[11], v[12]);
        G(r, 6, v[2], v[7], v[ 8], v[13]);
        G(r, 7, v[3], v[4], v[ 9], v[14]);
    }

    for (int i = 0; i < 8; i++)
        S->h[i] ^= v[i] ^ v[i + 8];
}

int blake2b_init_param(blake2b_state* S, size_t outlen, const void* key, size_t keylen,
                       const uint8_t* personal, size_t personal_len) {
    if (outlen == 0 || outlen > 64) return -1;
    if (keylen > 64) return -1;

    memset(S, 0, sizeof(*S));
    S->outlen = outlen;

    /* 构建参数块 */
    uint8_t P[64];
    memset(P, 0, 64);
    P[0] = (uint8_t)outlen;
    P[1] = (uint8_t)keylen;
    P[2] = 1;  /* fanout */
    P[3] = 1;  /* depth */
    /* P[4..7] = leaf length (0) */
    /* P[8..15] = node offset (0) */
    /* P[16] = node depth (0) */
    /* P[17] = inner length (0) */
    /* P[18..31] = reserved (0) */
    /* P[32..47] = salt (0) */
    /* P[48..63] = personal */
    if (personal && personal_len > 0) {
        size_t copy_len = personal_len < 16 ? personal_len : 16;
        memcpy(P + 48, personal, copy_len);
    }

    for (int i = 0; i < 8; i++)
        S->h[i] = blake2b_IV[i] ^ load64_le(P + i * 8);

    if (keylen > 0) {
        uint8_t block[128];
        memset(block, 0, 128);
        memcpy(block, key, keylen);
        S->t[0] = 128;
        blake2b_compress(S, block);
        S->buflen = 0;
    }

    return 0;
}

int blake2b_init(blake2b_state* S, size_t outlen) {
    return blake2b_init_param(S, outlen, NULL, 0, NULL, 0);
}

int blake2b_init_key(blake2b_state* S, size_t outlen, const void* key, size_t keylen) {
    return blake2b_init_param(S, outlen, key, keylen, NULL, 0);
}

int blake2b_update(blake2b_state* S, const void* in, size_t inlen) {
    const uint8_t* pin = (const uint8_t*)in;
    if (inlen == 0) return 0;

    size_t left = S->buflen;
    size_t fill = 128 - left;

    if (inlen > fill) {
        if (left > 0) {
            memcpy(S->buf + left, pin, fill);
            S->t[0] += 128;
            if (S->t[0] < 128) S->t[1]++;
            blake2b_compress(S, S->buf);
            pin += fill;
            inlen -= fill;
            S->buflen = 0;
        }
        while (inlen > 128) {
            S->t[0] += 128;
            if (S->t[0] < 128) S->t[1]++;
            blake2b_compress(S, pin);
            pin += 128;
            inlen -= 128;
        }
    }
    memcpy(S->buf + S->buflen, pin, inlen);
    S->buflen += inlen;
    return 0;
}

int blake2b_final(blake2b_state* S, void* out, size_t outlen) {
    if (outlen > S->outlen) return -1;

    S->t[0] += (uint64_t)S->buflen;
    if (S->t[0] < (uint64_t)S->buflen) S->t[1]++;
    S->f[0] = ~(uint64_t)0;

    memset(S->buf + S->buflen, 0, 128 - S->buflen);
    blake2b_compress(S, S->buf);

    uint8_t buffer[64];
    for (int i = 0; i < 8; i++)
        store64_le(buffer + i * 8, S->h[i]);
    memcpy(out, buffer, outlen);
    return 0;
}

void blake2b_full(const void* in, size_t inlen, void* out, size_t outlen,
                  const void* key, size_t keylen,
                  const uint8_t* personal, size_t personal_len) {
    blake2b_state S;
    blake2b_init_param(&S, outlen, key, keylen, personal, personal_len);
    blake2b_update(&S, in, inlen);
    blake2b_final(&S, out, outlen);
}
