#include "litepak_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void litepak_secure_bzero(void* p, size_t n) {
    if (!p || n == 0)
        return;
#ifdef _WIN32
    SecureZeroMemory(p, n);
#else
    volatile uint8_t* q = (volatile uint8_t*)p;
    while (n--) *q++ = 0;
#endif
}

int litepak_constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    if (!a || !b)
        return 0;
    for (size_t i = 0; i < n; ++i)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}
