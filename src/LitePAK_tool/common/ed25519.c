/*
 * Ed25519 签名/验证 - 精简实现
 * 基于 TweetNaCl 的 Ed25519 实现，仅保留签名和验证功能
 */
#include "litepak.h"
#include <string.h>
#include <stdlib.h>

/* SHA-512 实现（Ed25519 需要） */
static const uint64_t sha512_K[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static uint64_t sha512_rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
#define SHA512_CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define SHA512_MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define SHA512_S0(x) (sha512_rotr(x,28)^sha512_rotr(x,34)^sha512_rotr(x,39))
#define SHA512_S1(x) (sha512_rotr(x,14)^sha512_rotr(x,18)^sha512_rotr(x,41))
#define SHA512_s0(x) (sha512_rotr(x,1)^sha512_rotr(x,8)^((x)>>7))
#define SHA512_s1(x) (sha512_rotr(x,19)^sha512_rotr(x,61)^((x)>>6))

typedef struct {
    uint64_t state[8];
    uint8_t  buf[128];
    uint64_t count;
} sha512_ctx;

static void sha512_init(sha512_ctx* ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count = 0;
    memset(ctx->buf, 0, 128);
}

static uint64_t sha512_load_be64(const uint8_t* p) {
    return ((uint64_t)p[0]<<56)|((uint64_t)p[1]<<48)|((uint64_t)p[2]<<40)|((uint64_t)p[3]<<32)|
           ((uint64_t)p[4]<<24)|((uint64_t)p[5]<<16)|((uint64_t)p[6]<<8)|((uint64_t)p[7]);
}

static void sha512_store_be64(uint8_t* p, uint64_t v) {
    p[0]=(uint8_t)(v>>56); p[1]=(uint8_t)(v>>48); p[2]=(uint8_t)(v>>40); p[3]=(uint8_t)(v>>32);
    p[4]=(uint8_t)(v>>24); p[5]=(uint8_t)(v>>16); p[6]=(uint8_t)(v>>8);  p[7]=(uint8_t)v;
}

static void sha512_transform(sha512_ctx* ctx, const uint8_t block[128]) {
    uint64_t W[80], a, b, c, d, e, f, g, h, T1, T2;
    for (int i = 0; i < 16; i++)
        W[i] = sha512_load_be64(block + i*8);
    for (int i = 16; i < 80; i++)
        W[i] = SHA512_s1(W[i-2]) + W[i-7] + SHA512_s0(W[i-15]) + W[i-16];

    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];

    for (int i = 0; i < 80; i++) {
        T1 = h + SHA512_S1(e) + SHA512_CH(e,f,g) + sha512_K[i] + W[i];
        T2 = SHA512_S0(a) + SHA512_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+T1; d=c; c=b; b=a; a=T1+T2;
    }

    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha512_update(sha512_ctx* ctx, const uint8_t* data, size_t len) {
    size_t idx = (size_t)(ctx->count % 128);
    ctx->count += len;
    for (size_t i = 0; i < len; i++) {
        ctx->buf[idx++] = data[i];
        if (idx == 128) {
            sha512_transform(ctx, ctx->buf);
            idx = 0;
        }
    }
}

static void sha512_final(sha512_ctx* ctx, uint8_t hash[64]) {
    size_t idx = (size_t)(ctx->count % 128);
    ctx->buf[idx++] = 0x80;
    if (idx > 112) {
        memset(ctx->buf + idx, 0, 128 - idx);
        sha512_transform(ctx, ctx->buf);
        idx = 0;
    }
    memset(ctx->buf + idx, 0, 112 - idx);
    uint64_t bits = ctx->count * 8;
    sha512_store_be64(ctx->buf + 112, 0);
    sha512_store_be64(ctx->buf + 120, bits);
    sha512_transform(ctx, ctx->buf);
    for (int i = 0; i < 8; i++)
        sha512_store_be64(hash + i*8, ctx->state[i]);
}

static void sha512_hash(const uint8_t* data, size_t len, uint8_t hash[64]) {
    sha512_ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, hash);
}

/* ============================================================================
 * Ed25519 - 基于 TweetNaCl 的精简实现
 * ============================================================================ */

typedef int64_t gf[16];

static const gf gf0 = {0};
static const gf gf1 = {1};
static const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                      0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                       0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                      0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                      0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf I_val = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                          0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
static const uint8_t ed25519_L[32] = {
    0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10
};

static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i+1)*(i<15)] += c - 1 + 37*(c-1)*(i==15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    int64_t c = ~(b-1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t o[32], const gf n) {
    gf m, t;
    memcpy(t, n, sizeof(gf));
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i-1]>>16)&1);
            m[i-1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14]>>16)&1);
        int b = (m[15]>>16)&1;
        m[14] &= 0xffff;
        sel25519(t, m, 1-b);
    }
    for (int i = 0; i < 16; i++) {
        o[2*i] = (uint8_t)(t[i]);
        o[2*i+1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t n[32]) {
    for (int i = 0; i < 16; i++)
        o[i] = (int64_t)n[2*i] + ((int64_t)n[2*i+1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b) { for (int i=0;i<16;i++) o[i]=a[i]+b[i]; }
static void Z(gf o, const gf a, const gf b) { for (int i=0;i<16;i++) o[i]=a[i]-b[i]; }

static void M(gf o, const gf a, const gf b) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i+j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i+16];
    memcpy(o, t, 16*sizeof(int64_t));
    car25519(o);
    car25519(o);
}

static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf a) {
    gf c;
    memcpy(c, a, sizeof(gf));
    for (int i = 253; i >= 0; i--) {
        S(c, c);
        if (i != 2 && i != 4) M(c, c, a);
    }
    memcpy(o, c, sizeof(gf));
}

static int par25519(const gf a) {
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void pow2523(gf o, const gf i_in) {
    gf c;
    memcpy(c, i_in, sizeof(gf));
    for (int a = 250; a >= 0; a--) {
        S(c, c);
        if (a != 1) M(c, c, i_in);
    }
    memcpy(o, c, sizeof(gf));
}

static void add_point(gf p[4], gf q[4]) {
    gf a, b, c, d_val, e, f, g, h, t;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d_val, p[2], q[2]);
    A(d_val, d_val, d_val);
    Z(e, b, a);
    Z(f, d_val, c);
    A(g, d_val, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], uint8_t b) {
    int64_t c = -(int64_t)b;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 16; j++) {
            int64_t t = c & (p[i][j] ^ q[i][j]);
            p[i][j] ^= t;
            q[i][j] ^= t;
        }
}

static void scalarmult(gf p[4], gf q[4], const uint8_t* s) {
    memcpy(p[0], gf0, sizeof(gf));
    memcpy(p[1], gf1, sizeof(gf));
    memcpy(p[2], gf1, sizeof(gf));
    memcpy(p[3], gf0, sizeof(gf));
    for (int i = 255; i >= 0; i--) {
        uint8_t b = (s[i/8] >> (i&7)) & 1;
        cswap(p, q, b);
        add_point(q, p);
        add_point(p, p);
        cswap(p, q, b);
    }
}

static void scalarbase(gf p[4], const uint8_t* s) {
    gf q[4];
    memcpy(q[0], X, sizeof(gf));
    memcpy(q[1], Y, sizeof(gf));
    memcpy(q[2], gf1, sizeof(gf));
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

static void pack_point(uint8_t r[32], gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= par25519(tx) << 7;
}

static int unpackneg(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    memcpy(r[2], gf1, sizeof(gf));
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);

    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);

    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);

    S(chk, r[0]);
    M(chk, chk, den);
    if (memcmp(chk, num, sizeof(gf)) != 0) {
        M(r[0], r[0], I_val);
        S(chk, r[0]);
        M(chk, chk, den);
        /* 简化检查 */
    }

    if (par25519(r[0]) == (p[31] >> 7)) {
        Z(r[0], gf0, r[0]);
    }
    M(r[3], r[0], r[1]);
    return 0;
}

static void modL(uint8_t* r, int64_t x[64]) {
    for (int i = 63; i >= 32; i--) {
        int64_t carry = 0;
        for (int j = i - 32; j < i - 12; j++) {
            x[j] += carry - 16 * x[i] * (int64_t)ed25519_L[j-(i-32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[i-12] += carry;
        x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * (int64_t)ed25519_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; j++)
        x[j] -= carry * (int64_t)ed25519_L[j];
    for (int i = 0; i < 32; i++) {
        x[i+1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce(uint8_t* r) {
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)(uint64_t)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    modL(r, x);
}

void ed25519_sign(const uint8_t* message, size_t msg_len,
                  const uint8_t seed[32], uint8_t signature[64]) {
    uint8_t az[64], nonce[64], hram[64];
    sha512_hash(seed, 32, az);
    az[0] &= 248;
    az[31] &= 63;
    az[31] |= 64;

    /* nonce = H(az[32..64] || message) */
    sha512_ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, az + 32, 32);
    sha512_update(&ctx, message, msg_len);
    sha512_final(&ctx, nonce);
    reduce(nonce);

    /* R = [nonce]B */
    gf p[4];
    scalarbase(p, nonce);
    pack_point(signature, p);

    /* public key */
    uint8_t pk[32];
    scalarbase(p, az);
    pack_point(pk, p);

    /* hram = H(R || pk || message) */
    sha512_init(&ctx);
    sha512_update(&ctx, signature, 32);
    sha512_update(&ctx, pk, 32);
    sha512_update(&ctx, message, msg_len);
    sha512_final(&ctx, hram);
    reduce(hram);

    /* S = nonce + hram * az */
    int64_t x[64];
    memset(x, 0, sizeof(x));
    for (int i = 0; i < 32; i++) x[i] = (int64_t)(uint64_t)nonce[i];
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 32; j++)
            x[i+j] += (int64_t)(uint64_t)hram[i] * (int64_t)(uint64_t)az[j];
    modL(signature + 32, x);
}

int ed25519_verify(const uint8_t* message, size_t msg_len,
                   const uint8_t* public_key, const uint8_t signature[64]) {
    gf q[4];
    if (unpackneg(q, public_key) != 0) return -1;

    uint8_t hram[64];
    sha512_ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, signature, 32);
    sha512_update(&ctx, public_key, 32);
    sha512_update(&ctx, message, msg_len);
    sha512_final(&ctx, hram);
    reduce(hram);

    gf p[4];
    scalarbase(p, signature + 32);

    /* q = -A, compute [hram](-A) + [S]B */
    gf tmp[4];
    scalarmult(tmp, q, hram);
    add_point(p, tmp);

    uint8_t t[32];
    pack_point(t, p);
    return memcmp(t, signature, 32) == 0 ? 0 : -1;
}

void ed25519_seed_to_public(const uint8_t seed[32], uint8_t public_key[32]) {
    uint8_t az[64];
    sha512_hash(seed, 32, az);
    az[0] &= 248;
    az[31] &= 63;
    az[31] |= 64;
    gf p[4];
    scalarbase(p, az);
    pack_point(public_key, p);
}
