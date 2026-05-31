/*
 * LitePRNG (修改版 MT19937) + 索引混淆 + 条目字段混淆
 */
#include "litepak.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * LitePRNG - 修改版 MT19937
 * ============================================================================ */

#define MT_N 624
#define MT_M 397
#define MT_MATRIX_A   0x9908B0DF
#define MT_UPPER_MASK 0x80000000
#define MT_LOWER_MASK 0x7FFFFFFF
#define MT_INIT_MULT  (0x6C078965 ^ 0x4A7E3B19)
#define MT_TEMPER_A   (0x9D2C5681 ^ 0x1F3A7C05)
#define MT_TEMPER_B   (0xEFC60000 ^ 0x3B8E1A47)

void lite_prng_init(lite_prng_t* rng, uint32_t seed) {
    rng->mt[0] = seed;
    for (int i = 1; i < MT_N; i++) {
        rng->mt[i] = (uint32_t)(MT_INIT_MULT * (rng->mt[i-1] ^ (rng->mt[i-1] >> 30)) + (uint32_t)i);
    }
    rng->mti = MT_N;
}

static void lite_prng_generate(lite_prng_t* rng) {
    uint32_t mag01[2] = {0, MT_MATRIX_A};
    uint32_t y;
    int kk;

    for (kk = 0; kk < MT_N - MT_M; kk++) {
        y = (rng->mt[kk] & MT_UPPER_MASK) | (rng->mt[kk+1] & MT_LOWER_MASK);
        rng->mt[kk] = rng->mt[kk + MT_M] ^ (y >> 1) ^ mag01[y & 1];
    }
    for (; kk < MT_N - 1; kk++) {
        y = (rng->mt[kk] & MT_UPPER_MASK) | (rng->mt[kk+1] & MT_LOWER_MASK);
        rng->mt[kk] = rng->mt[kk + (MT_M - MT_N)] ^ (y >> 1) ^ mag01[y & 1];
    }
    y = (rng->mt[MT_N-1] & MT_UPPER_MASK) | (rng->mt[0] & MT_LOWER_MASK);
    rng->mt[MT_N-1] = rng->mt[MT_M-1] ^ (y >> 1) ^ mag01[y & 1];
    rng->mti = 0;
}

uint32_t lite_prng_next_u32(lite_prng_t* rng) {
    if (rng->mti >= MT_N)
        lite_prng_generate(rng);

    uint32_t y = rng->mt[rng->mti++];
    y ^= (y >> 11);
    y ^= (y << 7) & MT_TEMPER_A;
    y ^= (y << 15) & MT_TEMPER_B;
    y ^= (y >> 18);
    y ^= (y >> 5);
    return y;
}

void lite_prng_permutation(lite_prng_t* rng, uint32_t* perm, int n) {
    for (int i = 0; i < n; i++)
        perm[i] = (uint32_t)i;
    for (int i = n - 1; i > 0; i--) {
        uint32_t j = lite_prng_next_u32(rng) % ((uint32_t)i + 1);
        uint32_t tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }
}

void lite_prng_inverse_permutation(const uint32_t* perm, uint32_t* inv, int n) {
    for (int i = 0; i < n; i++)
        inv[perm[i]] = (uint32_t)i;
}

/* ============================================================================
 * 替换表生成
 * ============================================================================ */

static void generate_substitution_table(uint32_t seed, uint8_t table[256]) {
    lite_prng_t rng;
    lite_prng_init(&rng, seed ^ 0xA5A5C3C3);
    for (int i = 0; i < 256; i++)
        table[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--) {
        uint32_t j = lite_prng_next_u32(&rng) % ((uint32_t)i + 1);
        uint8_t tmp = table[i];
        table[i] = table[j];
        table[j] = tmp;
    }
}

static void inverse_substitution_table(const uint8_t table[256], uint8_t inv[256]) {
    for (int i = 0; i < 256; i++)
        inv[table[i]] = (uint8_t)i;
}

/* ============================================================================
 * 索引混淆 (Block Shuffle + Byte Substitution)
 * ============================================================================ */

static uint32_t keyed_round_seed(uint16_t seed, const uint8_t key[32], int round) {
    uint32_t k0 = (uint32_t)key[(round * 7) & 31] |
                  ((uint32_t)key[(round * 7 + 1) & 31] << 8) |
                  ((uint32_t)key[(round * 7 + 2) & 31] << 16) |
                  ((uint32_t)key[(round * 7 + 3) & 31] << 24);
    return ((uint32_t)seed * 0x45D9F3Bu) ^ k0 ^ (uint32_t)(round * 0x9E3779B9u);
}

static void xor_index_stream(uint8_t* data, size_t len, uint16_t seed, const uint8_t key[32], int round) {
    uint8_t stream[32];
    uint8_t input[32 + 2 + 4 + 8];
    size_t pos = 0;
    uint64_t counter = 0;
    memcpy(input, key, 32);
    input[32] = (uint8_t)seed;
    input[33] = (uint8_t)(seed >> 8);
    input[34] = (uint8_t)round;
    input[35] = input[36] = input[37] = 0;
    while (pos < len) {
        for (int i = 0; i < 8; i++) input[38 + i] = (uint8_t)(counter >> (i * 8));
        blake2b_full(input, sizeof(input), stream, 32, NULL, 0, (const uint8_t*)"LiteIdxObV6", 11);
        for (int i = 0; i < 32 && pos < len; i++, pos++)
            data[pos] ^= stream[i];
        counter++;
    }
}

static void obfuscate_index_round(const uint8_t* data, size_t len, uint32_t round_seed, uint8_t* out) {
    int n_blocks = 256;
    size_t block_size = (len + (size_t)n_blocks - 1) / (size_t)n_blocks;
    lite_prng_t rng;
    uint32_t perm[256];
    uint8_t sub_table[256];
    uint8_t* shuffled = (uint8_t*)malloc(len ? len : 1);
    size_t write_pos = 0;
    lite_prng_init(&rng, round_seed ^ 0x5A5A9E9E);
    lite_prng_permutation(&rng, perm, n_blocks);
    for (int i = 0; i < n_blocks; i++) {
        uint32_t idx = perm[i];
        size_t start = (size_t)idx * block_size;
        size_t end = start + block_size;
        if (end > len) end = len;
        if (start >= len) continue;
        memcpy(shuffled + write_pos, data + start, end - start);
        write_pos += end - start;
    }
    generate_substitution_table(round_seed, sub_table);
    for (size_t i = 0; i < len; i++)
        out[i] = sub_table[shuffled[i]];
    free(shuffled);
}

static void deobfuscate_index_round(const uint8_t* data, size_t len, uint32_t round_seed, uint8_t* out) {
    uint8_t sub_table[256], inv_table[256];
    uint8_t* unsubbed = (uint8_t*)malloc(len ? len : 1);
    int n_blocks = 256;
    size_t block_size = (len + (size_t)n_blocks - 1) / (size_t)n_blocks;
    lite_prng_t rng;
    uint32_t perm[256];
    size_t original_sizes[256];
    size_t shuffled_sizes[256];
    size_t pos = 0;
    generate_substitution_table(round_seed, sub_table);
    inverse_substitution_table(sub_table, inv_table);
    for (size_t i = 0; i < len; i++)
        unsubbed[i] = inv_table[data[i]];
    lite_prng_init(&rng, round_seed ^ 0x5A5A9E9E);
    lite_prng_permutation(&rng, perm, n_blocks);
    for (int i = 0; i < n_blocks; i++) {
        size_t start = (size_t)i * block_size;
        size_t end = start + block_size;
        if (end > len) end = len;
        original_sizes[i] = (start < len) ? (end - start) : 0;
    }
    for (int i = 0; i < n_blocks; i++)
        shuffled_sizes[i] = original_sizes[perm[i]];
    for (int i = 0; i < n_blocks; i++) {
        size_t sz = shuffled_sizes[i];
        uint32_t orig_idx = perm[i];
        size_t orig_start = (size_t)orig_idx * block_size;
        if (sz > 0 && orig_start < len)
            memcpy(out + orig_start, unsubbed + pos, sz);
        pos += sz;
    }
    free(unsubbed);
}

void litepak_obfuscate_index(const uint8_t* data, size_t len, uint16_t seed,
                             const uint8_t pre_master_key[32], uint8_t* out) {
    if (len == 0) return;
    int rounds = 3 + (seed % 5);
    uint8_t* a = (uint8_t*)malloc(len);
    uint8_t* b = (uint8_t*)malloc(len);
    memcpy(a, data, len);
    for (int r = 0; r < rounds; r++) {
        obfuscate_index_round(a, len, keyed_round_seed(seed, pre_master_key, r), b);
        xor_index_stream(b, len, seed, pre_master_key, r);
        uint8_t* t = a; a = b; b = t;
    }
    memcpy(out, a, len);
    free(a);
    free(b);
}

void litepak_deobfuscate_index(const uint8_t* data, size_t len, uint16_t seed,
                               const uint8_t pre_master_key[32], uint8_t* out) {
    if (len == 0) return;
    int rounds = 3 + (seed % 5);
    uint8_t* a = (uint8_t*)malloc(len);
    uint8_t* b = (uint8_t*)malloc(len);
    memcpy(a, data, len);
    for (int r = rounds - 1; r >= 0; r--) {
        xor_index_stream(a, len, seed, pre_master_key, r);
        deobfuscate_index_round(a, len, keyed_round_seed(seed, pre_master_key, r), b);
        uint8_t* t = a; a = b; b = t;
    }
    memcpy(out, a, len);
    free(a);
    free(b);
}

/* ============================================================================
 * 条目字段混淆
 * ============================================================================ */

static void entry_obfuscation_mask(const uint8_t path_hash[LITEPAK_PATH_HASH_SIZE],
                                    const uint8_t file_id[LITEPAK_FILE_ID_SIZE],
                                    uint16_t seed, uint32_t v6_features, uint8_t mask[20]) {
    uint8_t input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 2 + 4];
    memcpy(input, path_hash, LITEPAK_PATH_HASH_SIZE);
    memcpy(input + LITEPAK_PATH_HASH_SIZE, file_id, LITEPAK_FILE_ID_SIZE);
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE] = (uint8_t)(seed);
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 1] = (uint8_t)(seed >> 8);
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 2] = (uint8_t)v6_features;
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 3] = (uint8_t)(v6_features >> 8);
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 4] = (uint8_t)(v6_features >> 16);
    input[LITEPAK_PATH_HASH_SIZE + LITEPAK_FILE_ID_SIZE + 5] = (uint8_t)(v6_features >> 24);
    blake2b_full(input, sizeof(input), mask, 20, NULL, 0,
                 (const uint8_t*)"LiteEOBv6", 9);
}

void litepak_obfuscate_entry(entry_t* e, uint16_t seed, uint32_t v6_features) {
    uint8_t mask[20];
    entry_obfuscation_mask(e->hash_bytes, e->file_id, seed, v6_features, mask);

    uint32_t ref_start_mask = (uint32_t)mask[0] | ((uint32_t)mask[1] << 8) |
                              ((uint32_t)mask[2] << 16) | ((uint32_t)mask[3] << 24);
    uint32_t count_mask = (uint32_t)mask[4] | ((uint32_t)mask[5] << 8) |
                          ((uint32_t)mask[6] << 16) | ((uint32_t)mask[7] << 24);
    uint64_t size_mask = (uint64_t)mask[8] | ((uint64_t)mask[9] << 8) |
                         ((uint64_t)mask[10] << 16) | ((uint64_t)mask[11] << 24) |
                         ((uint64_t)mask[12] << 32) | ((uint64_t)mask[13] << 40) |
                         ((uint64_t)mask[14] << 48) | ((uint64_t)mask[15] << 56);
    uint32_t crc_mask = (uint32_t)mask[16] | ((uint32_t)mask[17] << 8) |
                        ((uint32_t)mask[18] << 16) | ((uint32_t)mask[19] << 24);

    e->chunk_ref_start ^= ref_start_mask;
    e->chunk_count ^= count_mask;
    e->original_size ^= size_mask;
    e->file_crc32c ^= crc_mask;
}

void litepak_deobfuscate_entry(entry_t* e, uint16_t seed, uint32_t v6_features) {
    litepak_obfuscate_entry(e, seed, v6_features);
}
