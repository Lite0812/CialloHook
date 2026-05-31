/*
 * LitePAK 基础工具函数
 * CRC32C, CRC8, 格式化输出, 进度条, Buffer, 随机数, 路径处理
 */
#include "litepak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <conio.h>
#include <direct.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* ============================================================================
 * CRC32C (Castagnoli)
 * ============================================================================ */

static uint32_t crc32c_table[256];
static int crc32c_table_init = 0;

static void make_crc32c_table(void) {
    if (crc32c_table_init) return;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0x1EDC6F41;
            else
                crc >>= 1;
        }
        crc32c_table[i] = crc;
    }
    crc32c_table_init = 1;
}

uint32_t litepak_crc32c(const uint8_t* data, size_t len, uint32_t seed) {
    make_crc32c_table();
    uint32_t crc = seed ^ 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * CRC8
 * ============================================================================ */

uint8_t litepak_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = ((crc << 1) ^ 0x07) & 0xFF;
            else
                crc = (crc << 1) & 0xFF;
        }
    }
    return crc;
}

/* ============================================================================
 * 格式化
 * ============================================================================ */

void litepak_format_size(uint64_t n, char* buf, size_t buf_sz) {
    double f = (double)n;
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int idx = 0;
    while (f >= 1024.0 && idx < 4) {
        f /= 1024.0;
        idx++;
    }
    snprintf(buf, buf_sz, "%.2f%s", f, units[idx]);
}

void litepak_format_duration(double seconds, char* buf, size_t buf_sz) {
    if (seconds < 60.0) {
        snprintf(buf, buf_sz, "%.3fs", seconds);
    } else if (seconds < 3600.0) {
        int m = (int)(seconds / 60.0);
        double s = seconds - m * 60.0;
        snprintf(buf, buf_sz, "%dm %.3fs", m, s);
    } else {
        int h = (int)(seconds / 3600.0);
        int m = (int)((seconds - h * 3600.0) / 60.0);
        double s = seconds - h * 3600.0 - m * 60.0;
        snprintf(buf, buf_sz, "%dh %dm %.3fs", h, m, s);
    }
}

/* ============================================================================
 * 进度条和日志
 * ============================================================================ */

static char progress_line[512] = {0};
static double progress_last_render_ts = 0.0;

static double get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

void litepak_emit_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (progress_line[0]) {
        printf("\r\x1b[2K");
        vprintf(fmt, args);
        printf("\n");
        printf("\r%s", progress_line);
        fflush(stdout);
    } else {
        vprintf(fmt, args);
        printf("\n");
        fflush(stdout);
    }
    va_end(args);
}

void litepak_print_progress(const char* prefix, int current, int total) {
    if (total <= 0) total = 1;
    if (current < 0) current = 0;
    if (current > total) current = total;

    double ratio = (double)current / (double)total;
    int bar_width = 28;
    int filled = (int)(bar_width * ratio);
    int percent = (int)(ratio * 100.0);

    char bar[128];
#ifdef _WIN32
    int unicode_bar = GetConsoleOutputCP() != 936;
#else
    int unicode_bar = 1;
#endif
    if (unicode_bar) {
        char* p = bar;
        for (int i = 0; i < bar_width; i++) {
            const char* ch = (i < filled) ? "█" : "░";
            memcpy(p, ch, strlen(ch));
            p += strlen(ch);
        }
        *p = '\0';
    } else {
        for (int i = 0; i < bar_width; i++)
            bar[i] = (i < filled) ? '#' : '-';
        bar[bar_width] = '\0';
    }

    snprintf(progress_line, sizeof(progress_line),
             "%s [%s] %3d%% (%d/%d)", prefix, bar, percent, current, total);

    double now = get_time_sec();
    int should_render = (current >= total) || (now - progress_last_render_ts >= 0.05);
    if (!should_render) return;

    printf("\r%s", progress_line);
    fflush(stdout);
    progress_last_render_ts = now;

    if (current >= total) {
        printf("\n");
        progress_line[0] = '\0';
        progress_last_render_ts = 0.0;
    }
}

/* ============================================================================
 * 路径处理
 * ============================================================================ */

void litepak_normalize_relpath(const char* input, char* output, size_t out_sz) {
    size_t i = 0;
    for (; input[i] && i < out_sz - 1; i++) {
        char c = input[i];
        if (c == '\\') c = '/';
        output[i] = (char)tolower((unsigned char)c);
    }
    output[i] = '\0';
}

void litepak_path_hash_bytes(const char* text, uint8_t out[LITEPAK_PATH_HASH_SIZE]) {
    char normalized[4096];
    litepak_normalize_relpath(text, normalized, sizeof(normalized));
    blake2b_full(normalized, strlen(normalized), out, LITEPAK_PATH_HASH_SIZE,
                 NULL, 0, (const uint8_t*)"LitePathV3", 10);
}

void litepak_chunk_hash_bytes(const uint8_t* data, size_t len, uint8_t out[LITEPAK_CHUNK_HASH_SIZE]) {
    blake2b_full(data, len, out, LITEPAK_CHUNK_HASH_SIZE,
                 NULL, 0, (const uint8_t*)"LiteChkV3", 9);
}

void litepak_hash16_personal(const uint8_t* data, size_t len,
                             const uint8_t* person, size_t person_len,
                             uint8_t out[LITEPAK_INDEX_HASH_SIZE]) {
    blake2b_full(data, len, out, LITEPAK_INDEX_HASH_SIZE,
                 NULL, 0, person, person_len);
}

int litepak_utf8_to_wide_dup(const char* text, wchar_t** out_wide) {
#ifdef _WIN32
    int size;
    wchar_t* buffer;
    if (!out_wide)
        return -1;
    *out_wide = NULL;
    if (!text)
        return -1;
    size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (size <= 0)
        return -1;
    buffer = (wchar_t*)malloc((size_t)size * sizeof(wchar_t));
    if (!buffer)
        return -1;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, size) <= 0) {
        free(buffer);
        return -1;
    }
    *out_wide = buffer;
    return 0;
#else
    (void)text;
    (void)out_wide;
    return -1;
#endif
}

int litepak_wide_to_utf8_dup(const wchar_t* text, char** out_utf8) {
#ifdef _WIN32
    int size;
    char* buffer;
    if (!out_utf8)
        return -1;
    *out_utf8 = NULL;
    if (!text)
        return -1;
    size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (size <= 0)
        return -1;
    buffer = (char*)malloc((size_t)size);
    if (!buffer)
        return -1;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, size, NULL, NULL) <= 0) {
        free(buffer);
        return -1;
    }
    *out_utf8 = buffer;
    return 0;
#else
    (void)text;
    (void)out_utf8;
    return -1;
#endif
}

FILE* litepak_fopen_utf8(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t* wide_path = NULL;
    wchar_t wide_mode[16];
    size_t i = 0;
    FILE* fp;
    if (!path || !mode)
        return NULL;
    if (litepak_utf8_to_wide_dup(path, &wide_path) != 0)
        return NULL;
    for (; mode[i] && i + 1 < sizeof(wide_mode) / sizeof(wide_mode[0]); i++)
        wide_mode[i] = (wchar_t)(unsigned char)mode[i];
    wide_mode[i] = L'\0';
    fp = _wfopen(wide_path, wide_mode);
    free(wide_path);
    return fp;
#else
    return fopen(path, mode);
#endif
}

int litepak_mkdir_utf8(const char* path) {
#ifdef _WIN32
    wchar_t* wide_path = NULL;
    int rc;
    if (!path || !path[0])
        return -1;
    if (litepak_utf8_to_wide_dup(path, &wide_path) != 0)
        return -1;
    rc = _wmkdir(wide_path);
    free(wide_path);
    return rc;
#else
    return mkdir(path, 0755);
#endif
}

/* ============================================================================
 * Buffer 动态缓冲区
 * ============================================================================ */

void buffer_init(buffer_t* buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->failed = false;
}

void buffer_free(buffer_t* buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->len = 0;
    buf->cap = 0;
    buf->failed = false;
}

int buffer_reserve(buffer_t* buf, size_t cap) {
    uint8_t* new_data;
    size_t new_cap;

    if (buf->failed) return -1;
    if (buf->cap >= cap) return 0;

    new_cap = buf->cap ? buf->cap : 256;
    while (new_cap < cap) {
        if (new_cap > ((size_t)-1) / 2) {
            buf->failed = true;
            return -1;
        }
        new_cap *= 2;
    }

    new_data = (uint8_t*)realloc(buf->data, new_cap);
    if (!new_data) {
        buf->failed = true;
        return -1;
    }

    buf->data = new_data;
    buf->cap = new_cap;
    return 0;
}

int buffer_append(buffer_t* buf, const void* data, size_t len) {
    if (buf->failed) return -1;
    if (len == 0) return 0;
    if (len > (size_t)-1 - buf->len) {
        buf->failed = true;
        return -1;
    }
    if (buffer_reserve(buf, buf->len + len) != 0) return -1;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 0;
}

int buffer_append_u8(buffer_t* buf, uint8_t val) {
    return buffer_append(buf, &val, 1);
}

int buffer_append_u16le(buffer_t* buf, uint16_t val) {
    uint8_t tmp[2];
    tmp[0] = (uint8_t)(val);
    tmp[1] = (uint8_t)(val >> 8);
    return buffer_append(buf, tmp, 2);
}

int buffer_append_u32le(buffer_t* buf, uint32_t val) {
    uint8_t tmp[4];
    tmp[0] = (uint8_t)(val);
    tmp[1] = (uint8_t)(val >> 8);
    tmp[2] = (uint8_t)(val >> 16);
    tmp[3] = (uint8_t)(val >> 24);
    return buffer_append(buf, tmp, 4);
}

int buffer_append_u64le(buffer_t* buf, uint64_t val) {
    uint8_t tmp[8];
    tmp[0] = (uint8_t)(val);
    tmp[1] = (uint8_t)(val >> 8);
    tmp[2] = (uint8_t)(val >> 16);
    tmp[3] = (uint8_t)(val >> 24);
    tmp[4] = (uint8_t)(val >> 32);
    tmp[5] = (uint8_t)(val >> 40);
    tmp[6] = (uint8_t)(val >> 48);
    tmp[7] = (uint8_t)(val >> 56);
    return buffer_append(buf, tmp, 8);
}

/* ============================================================================
 * 随机数生成
 * ============================================================================ */

void litepak_random_bytes(uint8_t* out, size_t len) {
#ifdef _WIN32
    BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, out, len);
        close(fd);
    }
#endif
}

/* ============================================================================
 * GEAR 表（CDC 用）- 预计算
 * ============================================================================ */

static uint64_t gear_table_storage[256];
static int gear_table_init = 0;

static void init_gear_table(void) {
    if (gear_table_init) return;
    for (int i = 0; i < 256; i++) {
        uint8_t input = (uint8_t)i;
        uint8_t digest[8];
        blake2b_full(&input, 1, digest, 8, NULL, 0, (const uint8_t*)"LiteCDC3", 8);
        gear_table_storage[i] =
            ((uint64_t)digest[0])       | ((uint64_t)digest[1] << 8)  |
            ((uint64_t)digest[2] << 16) | ((uint64_t)digest[3] << 24) |
            ((uint64_t)digest[4] << 32) | ((uint64_t)digest[5] << 40) |
            ((uint64_t)digest[6] << 48) | ((uint64_t)digest[7] << 56);
    }
    gear_table_init = 1;
}

const uint64_t* litepak_get_gear_table(void) {
    init_gear_table();
    return gear_table_storage;
}

/* Root material is reconstructed on demand by litepak_material_core.c. */

/* ============================================================================
 * 辅助：等待按键
 * ============================================================================ */

void litepak_wait_for_any_key(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout)))
        return;
    printf("\n按任意键退出...");
    fflush(stdout);
    _getch();
    printf("\n");
#else
    printf("\n按回车退出...");
    fflush(stdout);
    getchar();
#endif
}
