#include "litepak_internal.h"

#if defined(_WIN32) && defined(LITEPAK_CODECRYPT)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string.h>

#pragma section(".lpksc$m", execute, read)

volatile unsigned char litepak_codecrypt_begin_marker[16] = {
    'L','P','K','C','B','E','G','I','N','v','1',0,0,0,0,0
};

volatile unsigned char litepak_codecrypt_end_marker[16] = {
    'L','P','K','C','E','N','D','v','1',0,0,0,0,0,0,0
};

volatile unsigned char litepak_codecrypt_range_descriptor[24] = {
    'L','P','K','C','R','A','N','G','E','v','1',0,0,0,0,0,
    0,0,0,0,0,0,0,0
};

static volatile LONG g_litepak_codecrypt_state = 0;

static unsigned int codecrypt_rotl32(unsigned int v, unsigned int r) {
    return (v << r) | (v >> (32u - r));
}

static unsigned int codecrypt_mix32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static unsigned int codecrypt_seed(unsigned int rva, unsigned int size) {
#if defined(_M_X64) || defined(__x86_64__)
    const unsigned int arch = 0xC0DEC064u;
#else
    const unsigned int arch = 0xC0DEC032u;
#endif
    return codecrypt_mix32(0x4C504B43u ^ rva ^ codecrypt_rotl32(size, 7) ^ arch);
}

static unsigned char codecrypt_stream_byte(unsigned int* state, unsigned int index) {
    unsigned int x = *state + 0x9E3779B9u + index;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    *state = codecrypt_mix32(*state ^ x ^ index);
    return (unsigned char)(x >> ((index & 3u) * 8u));
}

static void codecrypt_xor_bytes(unsigned char* data, unsigned int size, unsigned int rva) {
    unsigned int state = codecrypt_seed(rva, size);
    for (unsigned int i = 0; i < size; ++i)
        data[i] ^= codecrypt_stream_byte(&state, i);
}

static int codecrypt_apply_section_relocs(HMODULE module, unsigned int range_rva,
                                          unsigned int range_size, int undo) {
    unsigned char* base = (unsigned char*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt;
    IMAGE_DATA_DIRECTORY dir;
    unsigned char* cursor;
    unsigned char* end;
    LONG_PTR delta;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return -1;
    nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return -1;

    delta = (LONG_PTR)((ULONG_PTR)base - (ULONG_PTR)nt->OptionalHeader.ImageBase);
    if (delta == 0)
        return 0;

    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!dir.VirtualAddress || !dir.Size)
        return 0;

    cursor = base + dir.VirtualAddress;
    end = cursor + dir.Size;
    while (cursor + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        IMAGE_BASE_RELOCATION* block = (IMAGE_BASE_RELOCATION*)cursor;
        unsigned int count;
        WORD* entries;

        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) || cursor + block->SizeOfBlock > end)
            return -1;
        if (block->SizeOfBlock == 0)
            break;

        count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        entries = (WORD*)(block + 1);
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int type = entries[i] >> 12;
            unsigned int off = entries[i] & 0x0FFFu;
            unsigned int target_rva = block->VirtualAddress + off;
            unsigned char* target;

            if (type == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            if (target_rva < range_rva || target_rva >= range_rva + range_size)
                continue;

            target = base + target_rva;
            if (type == IMAGE_REL_BASED_HIGHLOW) {
                DWORD value;
                if (target_rva + sizeof(value) > range_rva + range_size)
                    return -1;
                memcpy(&value, target, sizeof(value));
                value = undo ? value - (DWORD)delta : value + (DWORD)delta;
                memcpy(target, &value, sizeof(value));
                continue;
            }
#if defined(_WIN64) || defined(_M_X64)
            if (type == IMAGE_REL_BASED_DIR64) {
                ULONGLONG value;
                if (target_rva + sizeof(value) > range_rva + range_size)
                    return -1;
                memcpy(&value, target, sizeof(value));
                value = undo ? value - (ULONGLONG)delta : value + (ULONGLONG)delta;
                memcpy(target, &value, sizeof(value));
                continue;
            }
#endif
            return -1;
        }
        cursor += block->SizeOfBlock;
    }
    return 0;
}

static int codecrypt_marker_ok(void) {
    static const unsigned char begin_encrypted[16] = {
        'L','P','K','C','E','N','C','E','D','v','1',0,0,0,0,0
    };
    static const unsigned char end_plain[16] = {
        'L','P','K','C','E','N','D','v','1',0,0,0,0,0,0,0
    };
    return memcmp((const void*)litepak_codecrypt_begin_marker, begin_encrypted, sizeof(begin_encrypted)) == 0 &&
           memcmp((const void*)litepak_codecrypt_end_marker, end_plain, sizeof(end_plain)) == 0;
}

static unsigned int codecrypt_read_desc_u32(unsigned int offset) {
    const volatile unsigned char* p = litepak_codecrypt_range_descriptor + offset;
    return ((unsigned int)p[0]) |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static int codecrypt_find_range(HMODULE module, unsigned char** out_base,
                                unsigned int* out_size, unsigned int* out_rva) {
    static const unsigned char desc_marker[16] = {
        'L','P','K','C','R','A','N','G','E','v','1',0,0,0,0,0
    };
    unsigned char* image_base = (unsigned char*)module;
    unsigned int rva;
    unsigned int size;

    if (!image_base)
        return -1;
    if (memcmp((const void*)litepak_codecrypt_range_descriptor, desc_marker, sizeof(desc_marker)) != 0)
        return -1;
    rva = codecrypt_read_desc_u32(16);
    size = codecrypt_read_desc_u32(20);
    if (!rva || size <= 64 || size > 0x7FFFFFFFu)
        return -1;
    *out_base = image_base + rva;
    *out_size = size;
    *out_rva = rva;
    return 0;
}

static int codecrypt_decrypt_section(void) {
    HMODULE module = NULL;
    unsigned char* range_base = NULL;
    unsigned int range_size = 0;
    unsigned int range_rva = 0;
    DWORD old_protect = 0;
    DWORD restore_protect = 0;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&litepak_codecrypt_ensure_decrypted, &module))
        return -1;
    if (codecrypt_find_range(module, &range_base, &range_size, &range_rva) != 0)
        return -1;
    if (!codecrypt_marker_ok())
        return -1;
    if (!VirtualProtect(range_base, range_size, PAGE_EXECUTE_READWRITE, &old_protect))
        return -1;
    restore_protect = old_protect;

    if (codecrypt_apply_section_relocs(module, range_rva, range_size, 1) != 0) {
        VirtualProtect(range_base, range_size, restore_protect, &old_protect);
        return -1;
    }
    codecrypt_xor_bytes(range_base, range_size, range_rva);
    if (codecrypt_apply_section_relocs(module, range_rva, range_size, 0) != 0) {
        codecrypt_xor_bytes(range_base, range_size, range_rva);
        codecrypt_apply_section_relocs(module, range_rva, range_size, 0);
        VirtualProtect(range_base, range_size, restore_protect, &old_protect);
        return -1;
    }

    VirtualProtect(range_base, range_size, restore_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), range_base, range_size);
    return 0;
}

int litepak_codecrypt_is_enabled(void) {
    return 1;
}

int litepak_codecrypt_ensure_decrypted(void) {
    LONG state = InterlockedCompareExchange(&g_litepak_codecrypt_state, 1, 0);
    if (state == 2)
        return 0;
    if (state == 1) {
        while ((state = g_litepak_codecrypt_state) == 1)
            Sleep(0);
        return state == 2 ? 0 : -1;
    }
    if (state == -1)
        return -1;

    if (codecrypt_decrypt_section() == 0) {
        InterlockedExchange(&g_litepak_codecrypt_state, 2);
        return 0;
    }
    InterlockedExchange(&g_litepak_codecrypt_state, -1);
    return -1;
}

#else
int litepak_codecrypt_is_enabled(void) {
    return 0;
}

int litepak_codecrypt_ensure_decrypted(void) {
    return 0;
}
#endif
