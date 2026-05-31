#ifndef LITEPAK_INTERNAL_H
#define LITEPAK_INTERNAL_H

#include "litepak.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void litepak_secure_bzero(void* p, size_t n);
int litepak_constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n);

#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
#define LITEPAK_MATERIAL_ENTRY_COUNT 3u
#define LITEPAK_MATERIAL_TABLE_SIZE 96u
#else
#define LITEPAK_MATERIAL_ENTRY_COUNT 2u
#define LITEPAK_MATERIAL_TABLE_SIZE 64u
#endif

void litepak_materialize_root_seed_a(uint8_t out[32]);
void litepak_materialize_root_seed_b(uint8_t out[32]);
void litepak_materialize_default_sign_public_key(uint8_t out[32]);
#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
void litepak_materialize_default_sign_seed(uint8_t out[32]);
#endif
void litepak_materialize_data_key(uint8_t out[32]);
void litepak_materialize_vm_key(uint8_t out[32]);

int litepak_data_decrypt_block(const uint8_t* enc, size_t len, uint32_t domain, uint8_t* out);
int litepak_vm_decrypt_block(const uint8_t* enc, size_t len, uint32_t domain, uint8_t* out);

int litepak_codecrypt_ensure_decrypted(void);
int litepak_codecrypt_is_enabled(void);

#if defined(_MSC_VER) && defined(_WIN32) && defined(LITEPAK_CODECRYPT)
#define LITEPAK_PROTECTED_NOINLINE __declspec(noinline)
#define LITEPAK_PROTECTED_BEGIN __pragma(code_seg(push, ".lpksc$m"))
#define LITEPAK_PROTECTED_END __pragma(code_seg(pop))
#else
#define LITEPAK_PROTECTED_NOINLINE
#define LITEPAK_PROTECTED_BEGIN
#define LITEPAK_PROTECTED_END
#endif

size_t litepak_personal_wb_a(uint8_t out[16]);
size_t litepak_personal_wb_b(uint8_t out[16]);
size_t litepak_personal_wb_tbl(uint8_t out[16]);
size_t litepak_personal_wb_round(uint8_t out[16]);
size_t litepak_personal_wb_out(uint8_t out[16]);
size_t litepak_personal_pre(uint8_t out[16]);
size_t litepak_personal_full(uint8_t out[16]);
size_t litepak_personal_kpre(uint8_t out[16]);
size_t litepak_personal_kfull(uint8_t out[16]);
size_t litepak_personal_kms(uint8_t out[16]);
size_t litepak_personal_idk(uint8_t out[16]);
size_t litepak_personal_k9(uint8_t out[16]);
size_t litepak_personal_data(uint8_t out[16]);
size_t litepak_personal_open(uint8_t out[16]);
size_t litepak_personal_read(uint8_t out[16]);
size_t litepak_personal_n2(uint8_t out[16]);
size_t litepak_personal_arx(uint8_t out[16]);
size_t litepak_personal_fst(uint8_t out[16]);

int litepak_whitebox_transform_vm(const uint8_t seed_a[32], const uint8_t seed_b[32],
                                  const uint8_t* extra, size_t extra_len,
                                  bool strong_wb, uint8_t out[32]);
void litepak_whitebox_transform_native_ref(const uint8_t seed_a[32], const uint8_t seed_b[32],
                                           const uint8_t* extra, size_t extra_len,
                                           bool strong_wb, uint8_t out[32]);

const uint8_t* litepak_wb_bytecode_data(size_t* len);

#ifdef __cplusplus
}
#endif

#endif /* LITEPAK_INTERNAL_H */
