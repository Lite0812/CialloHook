#include "litepak_material.h"
#include <string.h>

extern const uint8_t k_lite_crc_fold_table[LITEPAK_MATERIAL_TABLE_SIZE];
extern const uint8_t k_lite_crc_stride_table[LITEPAK_MATERIAL_TABLE_SIZE];
extern const uint8_t k_lite_crc_window_table[LITEPAK_MATERIAL_TABLE_SIZE];
extern const uint8_t k_lite_crc_delta_table[32];
extern const uint8_t k_lite_crc_salt_table[32];

static uint8_t litepak_rol8_local(uint8_t v, unsigned r) {
    r &= 7u;
    return (uint8_t)((v << r) | (v >> (8u - r)));
}

static void litepak_materialize_indexed(uint32_t id, uint8_t out[32]) {
    uint8_t a[32];
    uint8_t b[32];
    uint8_t c[32];
    memset(out, 0, 32);
    if (id >= LITEPAK_MATERIAL_ENTRY_COUNT)
        return;
    if (litepak_data_decrypt_block(k_lite_crc_fold_table + id * 32u, 32, 0x4d410100u + id, a) != 0 ||
        litepak_data_decrypt_block(k_lite_crc_stride_table + id * 32u, 32, 0x4d410200u + id, b) != 0 ||
        litepak_data_decrypt_block(k_lite_crc_window_table + id * 32u, 32, 0x4d410300u + id, c) != 0) {
        litepak_secure_bzero(a, sizeof(a));
        litepak_secure_bzero(b, sizeof(b));
        litepak_secure_bzero(c, sizeof(c));
        return;
    }
    for (uint32_t i = 0; i < 32u; ++i) {
        uint8_t mask = (uint8_t)(0xD3u + id * 0x29u + i * 0x3Du + ((i * i + id * 17u) & 0xffu));
        uint32_t pos = (i * 13u + 7u) & 31u;
        out[pos] = (uint8_t)(a[i] ^ litepak_rol8_local(b[i], ((i + id) & 7u) + 1u) ^ c[i] ^ mask);
    }
    litepak_secure_bzero(a, sizeof(a));
    litepak_secure_bzero(b, sizeof(b));
    litepak_secure_bzero(c, sizeof(c));
}

void litepak_materialize_root_seed_a(uint8_t out[32]) {
    litepak_materialize_indexed(0u, out);
}

void litepak_materialize_root_seed_b(uint8_t out[32]) {
    litepak_materialize_indexed(1u, out);
}

void litepak_materialize_default_sign_public_key(uint8_t out[32]) {
    static const uint8_t k_default_sign_public_key[32] = {
        0x28,0x9b,0xde,0x4b,0x19,0xeb,0x96,0xc3,
        0x10,0x77,0x19,0x81,0x98,0xdb,0x5e,0x47,
        0x2a,0x3d,0x36,0x29,0x92,0x59,0xc6,0xae,
        0x0e,0x1e,0x84,0x5f,0x8c,0x4b,0x59,0x3a
    };
    memcpy(out, k_default_sign_public_key, 32);
}

#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
void litepak_materialize_default_sign_seed(uint8_t out[32]) {
    litepak_materialize_indexed(2u, out);
}
#endif

void litepak_materialize_data_key(uint8_t out[32]) {
    uint8_t tmp[32];
    for (uint32_t i = 0; i < 32u; ++i)
        tmp[(i * 9u + 5u) & 31u] = (uint8_t)(k_lite_crc_delta_table[i] + (uint8_t)(i * 17u + 0x33u));
    memcpy(out, tmp, 32);
    litepak_secure_bzero(tmp, sizeof(tmp));
}

void litepak_materialize_vm_key(uint8_t out[32]) {
    uint8_t data_key[32];
    uint8_t tmp[32];
    litepak_materialize_data_key(data_key);
    for (uint32_t i = 0; i < 32u; ++i) {
        uint8_t v = (uint8_t)(k_lite_crc_salt_table[i] - 0x19u);
        tmp[(i * 11u + 1u) & 31u] = (uint8_t)((v ^ (uint8_t)(i * 23u + 0x74u)) ^ (data_key[(i * 3u + 9u) & 31u] & 0u));
    }
    memcpy(out, tmp, 32);
    litepak_secure_bzero(data_key, sizeof(data_key));
    litepak_secure_bzero(tmp, sizeof(tmp));
}
