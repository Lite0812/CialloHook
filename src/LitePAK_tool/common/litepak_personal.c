#include "litepak_internal.h"
#include <string.h>

static const uint8_t k_lite_person_wb_a[8] = {
    0x3e,0x0a,0x70,0x3e,0xd1,0x20,0xf1,0xab
};
static const uint8_t k_lite_person_wb_b[8] = {
    0x01,0x56,0xa8,0x0f,0x43,0xef,0x84,0xaa
};
static const uint8_t k_lite_person_wb_tbl[9] = {
    0x85,0x1e,0x29,0x6c,0xc7,0xb5,0x94,0xae,0x28
};
static const uint8_t k_lite_person_wb_round[8] = {
    0xc4,0xec,0xa2,0x9e,0xfe,0xa9,0x7a,0x55
};
static const uint8_t k_lite_person_wb_out[8] = {
    0x7f,0x00,0x86,0xd0,0xbf,0x38,0xc1,0x25
};
static const uint8_t k_lite_person_pre[9] = {
    0x7d,0x40,0x86,0x6b,0x36,0x97,0x38,0x1c,0x18
};
static const uint8_t k_lite_person_full[8] = {
    0x7c,0x9c,0x1f,0xe7,0xd2,0x6c,0x25,0x2d
};
static const uint8_t k_lite_person_kpre[9] = {
    0x59,0x3e,0x90,0x49,0xe7,0xe0,0x55,0x9d,0xa3
};
static const uint8_t k_lite_person_kfull[9] = {
    0xa1,0x84,0xd6,0x94,0xad,0x80,0xfb,0x5a,0xeb
};
static const uint8_t k_lite_person_kms[9] = {
    0x49,0x4d,0x59,0xcb,0x53,0x84,0x4e,0x7c,0x1f
};
static const uint8_t k_lite_person_idk[9] = {
    0xb7,0x51,0x28,0x11,0x9c,0x6e,0x82,0x14,0xb0
};
static const uint8_t k_lite_person_k9[8] = {
    0xf3,0xee,0x4f,0x5e,0x78,0x95,0x23,0xc3
};
static const uint8_t k_lite_person_data[8] = {
    0xad,0x86,0x66,0xb9,0x94,0x4b,0xab,0x8c
};
static const uint8_t k_lite_person_open[8] = {
    0x43,0xf4,0x88,0x05,0x57,0xec,0x83,0x1b
};
static const uint8_t k_lite_person_read[8] = {
    0x08,0x9a,0x64,0xba,0xf2,0xf6,0x60,0x06
};
static const uint8_t k_lite_person_n2[8] = {
    0x9a,0x15,0xc4,0x9e,0x07,0x71,0xc7,0xbc
};
static const uint8_t k_lite_person_arx[9] = {
    0x94,0xa8,0x95,0x31,0x94,0xb2,0xde,0x68,0xff
};
static const uint8_t k_lite_person_fst[9] = {
    0x3f,0xec,0x2a,0x15,0x14,0x36,0xee,0x54,0xa2
};
static size_t litepak_personal_decode(const uint8_t* enc, size_t len, uint32_t domain, uint8_t out[16]) {
    memset(out, 0, 16);
    if (len > 16 || litepak_data_decrypt_block(enc, len, domain, out) != 0)
        return 0;
    return len;
}

size_t litepak_personal_wb_a(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_wb_a, sizeof(k_lite_person_wb_a), 0x50450000u, out);
}

size_t litepak_personal_wb_b(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_wb_b, sizeof(k_lite_person_wb_b), 0x50450001u, out);
}

size_t litepak_personal_wb_tbl(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_wb_tbl, sizeof(k_lite_person_wb_tbl), 0x50450002u, out);
}

size_t litepak_personal_wb_round(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_wb_round, sizeof(k_lite_person_wb_round), 0x50450003u, out);
}

size_t litepak_personal_wb_out(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_wb_out, sizeof(k_lite_person_wb_out), 0x50450004u, out);
}

size_t litepak_personal_pre(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_pre, sizeof(k_lite_person_pre), 0x50450005u, out);
}

size_t litepak_personal_full(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_full, sizeof(k_lite_person_full), 0x50450006u, out);
}

size_t litepak_personal_kpre(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_kpre, sizeof(k_lite_person_kpre), 0x50450007u, out);
}

size_t litepak_personal_kfull(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_kfull, sizeof(k_lite_person_kfull), 0x50450008u, out);
}

size_t litepak_personal_kms(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_kms, sizeof(k_lite_person_kms), 0x50450009u, out);
}

size_t litepak_personal_idk(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_idk, sizeof(k_lite_person_idk), 0x5045000au, out);
}

size_t litepak_personal_k9(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_k9, sizeof(k_lite_person_k9), 0x5045000bu, out);
}

size_t litepak_personal_data(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_data, sizeof(k_lite_person_data), 0x5045000cu, out);
}

size_t litepak_personal_open(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_open, sizeof(k_lite_person_open), 0x5045000du, out);
}

size_t litepak_personal_read(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_read, sizeof(k_lite_person_read), 0x5045000eu, out);
}

size_t litepak_personal_n2(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_n2, sizeof(k_lite_person_n2), 0x5045000fu, out);
}

size_t litepak_personal_arx(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_arx, sizeof(k_lite_person_arx), 0x50450010u, out);
}

size_t litepak_personal_fst(uint8_t out[16]) {
    return litepak_personal_decode(k_lite_person_fst, sizeof(k_lite_person_fst), 0x50450011u, out);
}

