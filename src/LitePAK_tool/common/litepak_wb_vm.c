#include "litepak_wb_vm.h"
#include <string.h>

#define OP_LOAD      0x10u
#define OP_STORE     0x11u
#define OP_XOR       0x20u
#define OP_SBOX      0x21u
#define OP_ADD       0x22u
#define OP_ROL8      0x23u
#define OP_PUSH_IMM  0x30u
#define OP_JMP       0x40u
#define OP_JZ        0x41u
#define OP_CALL      0x50u
#define OP_RET       0x51u
#define OP_WB_INIT   0x70u
#define OP_WB_TABLE  0x71u
#define OP_WB_ROUND  0x72u
#define OP_WB_FINAL  0x73u
#define OP_HALT      0xffu

static const uint8_t WB_VM_SBOX[256] = {
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

typedef struct {
    uint8_t* code;
    size_t code_size;
    size_t ip;
    uint8_t stack[256];
    size_t sp;
    uint8_t regs[16];
    uint8_t table[4][256];
    uint8_t state[64];
    const uint8_t* seed_a;
    const uint8_t* seed_b;
    const uint8_t* extra;
    size_t extra_len;
    bool strong_wb;
    uint8_t* out;
} wb_vm_context_t;

static uint8_t vm_rol8(uint8_t v, unsigned r) {
    r &= 7u;
    return (uint8_t)((v << r) | (v >> (8u - r)));
}

static int vm_push(wb_vm_context_t* ctx, uint8_t v) {
    if (ctx->sp >= sizeof(ctx->stack))
        return -1;
    ctx->stack[ctx->sp++] = v;
    return 0;
}

static int vm_pop(wb_vm_context_t* ctx, uint8_t* v) {
    if (ctx->sp == 0)
        return -1;
    *v = ctx->stack[--ctx->sp];
    return 0;
}

static int vm_fetch(wb_vm_context_t* ctx, uint8_t* v) {
    if (ctx->ip >= ctx->code_size)
        return -1;
    *v = ctx->code[ctx->ip];
    ctx->code[ctx->ip] = 0;
    ctx->ip++;
    return 0;
}

static int wb_vm_init_state(wb_vm_context_t* ctx) {
    uint8_t person[16];
    size_t person_len;
    blake2b_state S;

    person_len = litepak_personal_wb_a(person);
    if (person_len == 0)
        return -1;
    blake2b_init_param(&S, 32, NULL, 0, person, person_len);
    blake2b_update(&S, ctx->seed_a, 32);
    blake2b_update(&S, ctx->extra, ctx->extra_len);
    blake2b_final(&S, ctx->state, 32);
    litepak_secure_bzero(&S, sizeof(S));
    litepak_secure_bzero(person, sizeof(person));

    person_len = litepak_personal_wb_b(person);
    if (person_len == 0)
        return -1;
    blake2b_init_param(&S, 32, NULL, 0, person, person_len);
    blake2b_update(&S, ctx->seed_b, 32);
    blake2b_update(&S, ctx->extra, ctx->extra_len);
    blake2b_final(&S, ctx->state + 32, 32);
    litepak_secure_bzero(&S, sizeof(S));
    litepak_secure_bzero(person, sizeof(person));
    return 0;
}

static int wb_vm_build_table(wb_vm_context_t* ctx, uint8_t t) {
    uint8_t seed[65];
    uint8_t digest[32];
    uint8_t person[16];
    size_t person_len;
    blake2b_state T;
    if (t >= 4)
        return -1;
    seed[0] = t;
    memcpy(seed + 1, ctx->seed_a, 32);
    memcpy(seed + 33, ctx->seed_b, 32);
    person_len = litepak_personal_wb_tbl(person);
    if (person_len == 0) {
        litepak_secure_bzero(seed, sizeof(seed));
        return -1;
    }
    blake2b_init_param(&T, 32, NULL, 0, person, person_len);
    blake2b_update(&T, seed, sizeof(seed));
    blake2b_update(&T, ctx->extra, ctx->extra_len);
    blake2b_final(&T, digest, 32);
    for (int i = 0; i < 256; i++)
        ctx->table[t][i] = (uint8_t)(WB_VM_SBOX[i ^ digest[i & 31]] + digest[(i + t * 7) & 31] + i * (t * 2 + 1));
    litepak_secure_bzero(&T, sizeof(T));
    litepak_secure_bzero(seed, sizeof(seed));
    litepak_secure_bzero(digest, sizeof(digest));
    litepak_secure_bzero(person, sizeof(person));
    return 0;
}

static int wb_vm_round(wb_vm_context_t* ctx, uint8_t r) {
    uint8_t* left;
    uint8_t* right;
    uint8_t round_key[8] = {0};
    uint8_t f_out[32];
    uint8_t tmp[32];
    uint8_t person[16];
    size_t person_len;
    if (r >= 10)
        return -1;
    for (int i = 0; i < 64; i++)
        ctx->state[i] = (uint8_t)(ctx->table[(i + r) & 3][ctx->state[i]] ^ ctx->table[(i + r + 1) & 3][(ctx->state[(i + 17) & 63] + r + i) & 0xFF]);
    left = ctx->state;
    right = ctx->state + 32;
    round_key[0] = r;
    person_len = litepak_personal_wb_round(person);
    if (person_len == 0)
        return -1;
    blake2b_full(right, 32, f_out, 32, round_key, 8, person, person_len);
    for (int i = 0; i < 32; i++)
        left[i] = (uint8_t)(left[i] ^ f_out[i] ^ ctx->table[(r + i) & 3][right[(i * 5 + r) & 31]]);
    memcpy(tmp, left, 32);
    memcpy(left, right, 32);
    memcpy(right, tmp, 32);
    litepak_secure_bzero(round_key, sizeof(round_key));
    litepak_secure_bzero(f_out, sizeof(f_out));
    litepak_secure_bzero(tmp, sizeof(tmp));
    litepak_secure_bzero(person, sizeof(person));
    return 0;
}

static int wb_vm_final(wb_vm_context_t* ctx) {
    uint8_t person[16];
    size_t person_len = litepak_personal_wb_out(person);
    if (person_len == 0)
        return -1;
    blake2b_full(ctx->state, 64, ctx->out, 32, NULL, 0, person, person_len);
    litepak_secure_bzero(person, sizeof(person));
    return 0;
}

static int vm_execute(wb_vm_context_t* ctx) {
    while (ctx->ip < ctx->code_size) {
        uint8_t op;
        if (vm_fetch(ctx, &op) != 0)
            return -1;
        switch (op) {
        case OP_PUSH_IMM: {
            uint8_t imm;
            if (vm_fetch(ctx, &imm) != 0 || vm_push(ctx, imm) != 0) return -1;
            break;
        }
        case OP_XOR: {
            uint8_t a, b;
            if (vm_pop(ctx, &a) != 0 || vm_pop(ctx, &b) != 0 || vm_push(ctx, (uint8_t)(a ^ b)) != 0) return -1;
            break;
        }
        case OP_ADD: {
            uint8_t a, b;
            if (vm_pop(ctx, &a) != 0 || vm_pop(ctx, &b) != 0 || vm_push(ctx, (uint8_t)(a + b)) != 0) return -1;
            break;
        }
        case OP_ROL8: {
            uint8_t a, r;
            if (vm_pop(ctx, &r) != 0 || vm_pop(ctx, &a) != 0 || vm_push(ctx, vm_rol8(a, r)) != 0) return -1;
            break;
        }
        case OP_SBOX: {
            uint8_t a;
            if (vm_pop(ctx, &a) != 0 || vm_push(ctx, WB_VM_SBOX[a]) != 0) return -1;
            break;
        }
        case OP_LOAD: {
            uint8_t idx;
            if (vm_fetch(ctx, &idx) != 0 || idx >= sizeof(ctx->regs) || vm_push(ctx, ctx->regs[idx]) != 0) return -1;
            break;
        }
        case OP_STORE: {
            uint8_t idx, v;
            if (vm_fetch(ctx, &idx) != 0 || idx >= sizeof(ctx->regs) || vm_pop(ctx, &v) != 0) return -1;
            ctx->regs[idx] = v;
            break;
        }
        case OP_JMP: {
            uint8_t target;
            if (vm_fetch(ctx, &target) != 0 || target >= ctx->code_size) return -1;
            ctx->ip = target;
            break;
        }
        case OP_JZ: {
            uint8_t target, cond;
            if (vm_fetch(ctx, &target) != 0 || target >= ctx->code_size || vm_pop(ctx, &cond) != 0) return -1;
            if (cond == 0) ctx->ip = target;
            break;
        }
        case OP_CALL:
        case OP_RET:
            break;
        case OP_WB_INIT:
            if (wb_vm_init_state(ctx) != 0) return -1;
            break;
        case OP_WB_TABLE: {
            uint8_t t;
            if (vm_fetch(ctx, &t) != 0 || wb_vm_build_table(ctx, t) != 0) return -1;
            break;
        }
        case OP_WB_ROUND: {
            uint8_t r;
            if (vm_fetch(ctx, &r) != 0 || wb_vm_round(ctx, r) != 0) return -1;
            break;
        }
        case OP_WB_FINAL:
            if (wb_vm_final(ctx) != 0) return -1;
            break;
        case OP_HALT:
            return 0;
        default:
            return -1;
        }
    }
    return -1;
}

int litepak_whitebox_transform_vm(const uint8_t seed_a[32], const uint8_t seed_b[32],
                                  const uint8_t* extra, size_t extra_len,
                                  bool strong_wb, uint8_t out[32]) {
    uint8_t code[64];
    const uint8_t* enc;
    size_t code_len = 0;
    wb_vm_context_t ctx;
    int ret;

    enc = litepak_wb_bytecode_data(&code_len);
    if (!enc || code_len == 0 || code_len > sizeof(code)) {
        memset(out, 0, 32);
        return -1;
    }
    if (litepak_vm_decrypt_block(enc, code_len, 0x564d5742u, code) != 0) {
        memset(out, 0, 32);
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.code = code;
    ctx.code_size = code_len;
    ctx.seed_a = seed_a;
    ctx.seed_b = seed_b;
    ctx.extra = extra;
    ctx.extra_len = extra_len;
    ctx.strong_wb = strong_wb;
    ctx.out = out;

    ret = vm_execute(&ctx);
    if (ret != 0)
        memset(out, 0, 32);
    litepak_secure_bzero(code, sizeof(code));
    litepak_secure_bzero(&ctx, sizeof(ctx));
    return ret;
}
