/*
 * AES-256-GCM 实现
 * 基于 AES 核心 + GF(2^128) 乘法实现 GCM 模式
 */
#include "litepak.h"
#include <string.h>
#include <stdlib.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define LITEPAK_AESNI_AVAILABLE 1
#include <intrin.h>
#include <immintrin.h>
#else
#define LITEPAK_AESNI_AVAILABLE 0
#endif

/* ============================================================================
 * AES-256 核心
 * ============================================================================ */

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t aes_rsbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static const uint8_t Rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

typedef struct {
    uint8_t round_key[240];
    int nk;
    int nr;
    int use_aesni;
#if LITEPAK_AESNI_AVAILABLE
    __m128i round_key_ni[15];
#endif
} aes_ctx_t;

#if LITEPAK_AESNI_AVAILABLE
static int aes_cpu_supports_aesni(void) {
    static int cached = -1;
    if (cached < 0) {
        int cpu_info[4] = {0};
        __cpuid(cpu_info, 1);
        cached = (cpu_info[2] & (1 << 25)) ? 1 : 0;
    }
    return cached;
}
#endif

static void aes_key_expansion(aes_ctx_t* ctx, const uint8_t* key) {
    ctx->nk = 8;
    ctx->nr = 14;
    ctx->use_aesni = 0;
    int i, j, k;
    uint8_t tempa[4];

    for (i = 0; i < ctx->nk; i++) {
        ctx->round_key[i*4+0] = key[i*4+0];
        ctx->round_key[i*4+1] = key[i*4+1];
        ctx->round_key[i*4+2] = key[i*4+2];
        ctx->round_key[i*4+3] = key[i*4+3];
    }

    for (i = ctx->nk; i < 4 * (ctx->nr + 1); i++) {
        k = (i - 1) * 4;
        tempa[0] = ctx->round_key[k+0];
        tempa[1] = ctx->round_key[k+1];
        tempa[2] = ctx->round_key[k+2];
        tempa[3] = ctx->round_key[k+3];

        if (i % ctx->nk == 0) {
            uint8_t u = tempa[0];
            tempa[0] = tempa[1];
            tempa[1] = tempa[2];
            tempa[2] = tempa[3];
            tempa[3] = u;
            tempa[0] = aes_sbox[tempa[0]];
            tempa[1] = aes_sbox[tempa[1]];
            tempa[2] = aes_sbox[tempa[2]];
            tempa[3] = aes_sbox[tempa[3]];
            tempa[0] ^= Rcon[i / ctx->nk];
        }
        if (i % ctx->nk == 4) {
            tempa[0] = aes_sbox[tempa[0]];
            tempa[1] = aes_sbox[tempa[1]];
            tempa[2] = aes_sbox[tempa[2]];
            tempa[3] = aes_sbox[tempa[3]];
        }

        j = i * 4; k = (i - ctx->nk) * 4;
        ctx->round_key[j+0] = ctx->round_key[k+0] ^ tempa[0];
        ctx->round_key[j+1] = ctx->round_key[k+1] ^ tempa[1];
        ctx->round_key[j+2] = ctx->round_key[k+2] ^ tempa[2];
        ctx->round_key[j+3] = ctx->round_key[k+3] ^ tempa[3];
    }

#if LITEPAK_AESNI_AVAILABLE
    ctx->use_aesni = aes_cpu_supports_aesni();
    if (ctx->use_aesni) {
        for (i = 0; i <= ctx->nr; i++)
            ctx->round_key_ni[i] = _mm_loadu_si128((const __m128i*)(ctx->round_key + i * 16));
    }
#endif
}

static uint8_t xtime(uint8_t x) {
    return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void aes_encrypt_block_generic(const aes_ctx_t* ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[4][4];
    int i, j, round;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            state[j][i] = in[i*4+j];

    /* AddRoundKey(0) */
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            state[j][i] ^= ctx->round_key[i*4+j];

    for (round = 1; round <= ctx->nr; round++) {
        /* SubBytes */
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                state[i][j] = aes_sbox[state[i][j]];

        /* ShiftRows */
        uint8_t tmp;
        tmp = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2];
        state[1][2] = state[1][3]; state[1][3] = tmp;
        tmp = state[2][0]; state[2][0] = state[2][2]; state[2][2] = tmp;
        tmp = state[2][1]; state[2][1] = state[2][3]; state[2][3] = tmp;
        tmp = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1];
        state[3][1] = state[3][0]; state[3][0] = tmp;

        /* MixColumns (skip in last round) */
        if (round < ctx->nr) {
            for (i = 0; i < 4; i++) {
                uint8_t a = state[0][i], b = state[1][i], c = state[2][i], d = state[3][i];
                uint8_t e = a ^ b ^ c ^ d;
                state[0][i] ^= e ^ xtime(a ^ b);
                state[1][i] ^= e ^ xtime(b ^ c);
                state[2][i] ^= e ^ xtime(c ^ d);
                state[3][i] ^= e ^ xtime(d ^ a);
            }
        }

        /* AddRoundKey */
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                state[j][i] ^= ctx->round_key[round*16 + i*4 + j];
    }

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            out[i*4+j] = state[j][i];
}

#if LITEPAK_AESNI_AVAILABLE
static void aes_encrypt_block_aesni(const aes_ctx_t* ctx, const uint8_t in[16], uint8_t out[16]) {
    __m128i state = _mm_loadu_si128((const __m128i*)in);
    state = _mm_xor_si128(state, ctx->round_key_ni[0]);
    for (int round = 1; round < ctx->nr; round++)
        state = _mm_aesenc_si128(state, ctx->round_key_ni[round]);
    state = _mm_aesenclast_si128(state, ctx->round_key_ni[ctx->nr]);
    _mm_storeu_si128((__m128i*)out, state);
}
#endif

static void aes_encrypt_block(const aes_ctx_t* ctx, const uint8_t in[16], uint8_t out[16]) {
#if LITEPAK_AESNI_AVAILABLE
    if (ctx->use_aesni) {
        aes_encrypt_block_aesni(ctx, in, out);
        return;
    }
#endif
    aes_encrypt_block_generic(ctx, in, out);
}

/* ============================================================================
 * GCM 模式
 * ============================================================================ */

static void ghash_mult(const uint8_t X[16], const uint8_t H[16], uint8_t out[16]) {
    uint8_t V[16];
    uint8_t Z[16];
    memcpy(V, H, 16);
    memset(Z, 0, 16);

    for (int i = 0; i < 128; i++) {
        if ((X[i/8] >> (7 - (i%8))) & 1) {
            for (int j = 0; j < 16; j++)
                Z[j] ^= V[j];
        }
        uint8_t lsb = V[15] & 1;
        for (int j = 15; j > 0; j--)
            V[j] = (V[j] >> 1) | (V[j-1] << 7);
        V[0] >>= 1;
        if (lsb)
            V[0] ^= 0xe1;
    }
    memcpy(out, Z, 16);
}

static void ghash(const uint8_t H[16], const uint8_t* data, size_t len, uint8_t out[16]) {
    uint8_t Y[16];
    memset(Y, 0, 16);

    size_t blocks = len / 16;
    for (size_t i = 0; i < blocks; i++) {
        for (int j = 0; j < 16; j++)
            Y[j] ^= data[i*16 + j];
        uint8_t tmp[16];
        ghash_mult(Y, H, tmp);
        memcpy(Y, tmp, 16);
    }

    size_t rem = len % 16;
    if (rem > 0) {
        for (size_t j = 0; j < rem; j++)
            Y[j] ^= data[blocks*16 + j];
        uint8_t tmp[16];
        ghash_mult(Y, H, tmp);
        memcpy(Y, tmp, 16);
    }

    memcpy(out, Y, 16);
}

static void inc32(uint8_t counter[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++counter[i] != 0) break;
    }
}

static void gcm_ctr_encrypt(const aes_ctx_t* ctx, const uint8_t* in, size_t len,
                            uint8_t* out, uint8_t J0[16]) {
    uint8_t counter[16];
    uint8_t keystream[16];
    memcpy(counter, J0, 16);
    inc32(counter);

    size_t blocks = len / 16;
    size_t rem = len % 16;

    for (size_t i = 0; i < blocks; i++) {
        aes_encrypt_block(ctx, counter, keystream);
        for (int j = 0; j < 16; j++)
            out[i*16+j] = in[i*16+j] ^ keystream[j];
        inc32(counter);
    }

    if (rem > 0) {
        aes_encrypt_block(ctx, counter, keystream);
        for (size_t j = 0; j < rem; j++)
            out[blocks*16+j] = in[blocks*16+j] ^ keystream[j];
    }
}

static void compute_gcm_tag(const aes_ctx_t* ctx, const uint8_t H[16],
                            const uint8_t* aad, size_t aad_len,
                            const uint8_t* ciphertext, size_t ct_len,
                            const uint8_t J0[16], uint8_t tag[16]) {
    /* GHASH(A || pad || C || pad || len(A) || len(C)) */
    size_t aad_padded = ((aad_len + 15) / 16) * 16;
    size_t ct_padded = ((ct_len + 15) / 16) * 16;
    size_t total = aad_padded + ct_padded + 16;

    uint8_t* buf = (uint8_t*)calloc(1, total);
    if (aad && aad_len > 0)
        memcpy(buf, aad, aad_len);
    if (ciphertext && ct_len > 0)
        memcpy(buf + aad_padded, ciphertext, ct_len);

    /* 长度块: 64-bit bit lengths */
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t ct_bits = (uint64_t)ct_len * 8;
    uint8_t* len_block = buf + aad_padded + ct_padded;
    for (int i = 0; i < 8; i++) {
        len_block[i] = (uint8_t)(aad_bits >> (56 - i*8));
        len_block[8+i] = (uint8_t)(ct_bits >> (56 - i*8));
    }

    uint8_t S[16];
    ghash(H, buf, total, S);
    free(buf);

    /* tag = S XOR E(K, J0) */
    uint8_t E_J0[16];
    aes_encrypt_block(ctx, J0, E_J0);
    for (int i = 0; i < 16; i++)
        tag[i] = S[i] ^ E_J0[i];
}

int aes_gcm_encrypt(const uint8_t* plaintext, size_t pt_len,
                    const uint8_t key[32], const uint8_t nonce[12],
                    uint8_t* ciphertext_and_tag) {
    aes_ctx_t ctx;
    aes_key_expansion(&ctx, key);

    /* H = E(K, 0^128) */
    uint8_t H[16];
    uint8_t zero[16] = {0};
    aes_encrypt_block(&ctx, zero, H);

    /* J0 = nonce || 0x00000001 */
    uint8_t J0[16];
    memcpy(J0, nonce, 12);
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    /* 加密 */
    gcm_ctr_encrypt(&ctx, plaintext, pt_len, ciphertext_and_tag, J0);

    /* 计算 tag (无 AAD) */
    uint8_t tag[16];
    compute_gcm_tag(&ctx, H, NULL, 0, ciphertext_and_tag, pt_len, J0, tag);
    memcpy(ciphertext_and_tag + pt_len, tag, 16);

    return 0;
}

int aes_gcm_decrypt(const uint8_t* ciphertext_and_tag, size_t ct_tag_len,
                    const uint8_t key[32], const uint8_t nonce[12],
                    uint8_t* plaintext) {
    if (ct_tag_len < 16) return -1;
    size_t ct_len = ct_tag_len - 16;
    const uint8_t* tag = ciphertext_and_tag + ct_len;

    aes_ctx_t ctx;
    aes_key_expansion(&ctx, key);

    uint8_t H[16];
    uint8_t zero[16] = {0};
    aes_encrypt_block(&ctx, zero, H);

    uint8_t J0[16];
    memcpy(J0, nonce, 12);
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    /* 验证 tag */
    uint8_t computed_tag[16];
    compute_gcm_tag(&ctx, H, NULL, 0, ciphertext_and_tag, ct_len, J0, computed_tag);

    int tag_ok = 1;
    for (int i = 0; i < 16; i++) {
        if (tag[i] != computed_tag[i]) {
            tag_ok = 0;
            break;
        }
    }
    if (!tag_ok) return -1;

    /* 解密 */
    gcm_ctr_encrypt(&ctx, ciphertext_and_tag, ct_len, plaintext, J0);
    return 0;
}
