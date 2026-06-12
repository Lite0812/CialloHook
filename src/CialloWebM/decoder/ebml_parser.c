/*
 * ebml_parser.c — EBML 变长整数与 Element 解析实现
 *
 * EBML 使用变长编码：第一个字节的前导 1 位的位置决定整个整数的字节数。
 * 例如：
 *   1xxx xxxx                        → 1 字节，7 位数据
 *   01xx xxxx  xxxx xxxx             → 2 字节，14 位数据
 *   001x xxxx  xxxx xxxx  xxxx xxxx  → 3 字节，21 位数据
 *   ...以此类推，最多 8 字节
 */

#include "ebml_parser.h"
#include <string.h>

/* ---- 流操作 ---- */

void ebml_stream_init(EbmlStream* s, const uint8_t* data, size_t size)
{
    s->data = data;
    s->size = size;
    s->pos = 0;
}

size_t ebml_stream_remaining(const EbmlStream* s)
{
    return (s->pos < s->size) ? (s->size - s->pos) : 0;
}

/* ---- VINT（Variable-size Integer）读取 ---- */

/*
 * 计算 VINT 字节长度：从第一个字节的前导零位数+1 得出。
 * 0b1xxxxxxx → 1 字节
 * 0b01xxxxxx → 2 字节
 * ...
 */
static int vint_length(uint8_t first_byte)
{
    if (first_byte & 0x80) return 1;
    if (first_byte & 0x40) return 2;
    if (first_byte & 0x20) return 3;
    if (first_byte & 0x10) return 4;
    if (first_byte & 0x08) return 5;
    if (first_byte & 0x04) return 6;
    if (first_byte & 0x02) return 7;
    if (first_byte & 0x01) return 8;
    return -1; /* 0x00 无效 */
}

int64_t ebml_read_vint(EbmlStream* s, int* out_bytes)
{
    if (ebml_stream_remaining(s) < 1)
    {
        if (out_bytes) *out_bytes = 0;
        return -1;
    }

    uint8_t first = s->data[s->pos];
    int len = vint_length(first);
    if (len < 0 || (size_t)len > ebml_stream_remaining(s))
    {
        if (out_bytes) *out_bytes = 0;
        return -1;
    }

    /* 去掉长度标记位，读取数据位 */
    uint8_t mask = (uint8_t)(0xFF >> len); /* 去掉前导 1 和前导 0 */
    int64_t value = first & mask;
    for (int i = 1; i < len; ++i)
    {
        value = (value << 8) | s->data[s->pos + i];
    }

    s->pos += len;
    if (out_bytes) *out_bytes = len;
    return value;
}

/* ---- Element ID 读取 ----
 * ID 的编码与 VINT 类似，但保留前导标记位作为 ID 的一部分。
 */
uint32_t ebml_read_id(EbmlStream* s, int* out_bytes)
{
    if (ebml_stream_remaining(s) < 1)
    {
        if (out_bytes) *out_bytes = 0;
        return 0;
    }

    uint8_t first = s->data[s->pos];
    int len = vint_length(first);
    if (len < 0 || len > 4 || (size_t)len > ebml_stream_remaining(s))
    {
        if (out_bytes) *out_bytes = 0;
        return 0;
    }

    /* ID 保留标记位（不做 mask） */
    uint32_t id = first;
    for (int i = 1; i < len; ++i)
    {
        id = (id << 8) | s->data[s->pos + i];
    }

    s->pos += len;
    if (out_bytes) *out_bytes = len;
    return id;
}

/* ---- Element 读取（ID + Size） ---- */

int ebml_read_element(EbmlStream* s, EbmlElement* elem)
{
    if (ebml_stream_remaining(s) < 2)
    {
        return 0;
    }

    size_t start_pos = s->pos;

    int id_bytes = 0;
    uint32_t id = ebml_read_id(s, &id_bytes);
    if (id == 0)
    {
        s->pos = start_pos;
        return 0;
    }

    int size_bytes = 0;
    int64_t size = ebml_read_vint(s, &size_bytes);
    if (size < 0 && size_bytes == 0)
    {
        s->pos = start_pos;
        return 0;
    }

    /* 检测"不确定长度"：所有数据位都为 1 */
    /* 1 字节: 0x7F, 2 字节: 0x3FFF, ... */
    {
        static const int64_t unknown_sizes[] = {
            0x7F, 0x3FFF, 0x1FFFFF, 0x0FFFFFFF,
            0x07FFFFFFFFLL, 0x03FFFFFFFFFFLL,
            0x01FFFFFFFFFFFFLL, 0x00FFFFFFFFFFFFFFLL
        };
        if (size_bytes >= 1 && size_bytes <= 8 &&
            size == unknown_sizes[size_bytes - 1])
        {
            size = -1; /* 不确定长度 */
        }
    }

    elem->id = id;
    elem->size = size;
    elem->data_offset = s->pos;
    elem->header_size = (size_t)(s->pos - start_pos);

    return 1;
}

void ebml_skip_element(EbmlStream* s, const EbmlElement* elem)
{
    if (elem->size >= 0)
    {
        size_t end = elem->data_offset + (size_t)elem->size;
        if (end > s->size) end = s->size;
        s->pos = end;
    }
    /* 不确定长度的 Element 无法跳过，调用方需自行处理 */
}

void ebml_sub_stream(const EbmlStream* parent, const EbmlElement* elem,
                     EbmlStream* sub)
{
    if (elem->size < 0)
    {
        /* 不确定长度：子流延伸到父流末尾 */
        sub->data = parent->data + elem->data_offset;
        sub->size = parent->size - elem->data_offset;
        sub->pos = 0;
    }
    else
    {
        size_t available = parent->size - elem->data_offset;
        size_t esize = (size_t)elem->size;
        if (esize > available) esize = available;
        sub->data = parent->data + elem->data_offset;
        sub->size = esize;
        sub->pos = 0;
    }
}

/* ---- 数据类型读取 ---- */

uint64_t ebml_read_uint(EbmlStream* s, size_t len)
{
    if (len == 0 || len > 8 || len > ebml_stream_remaining(s))
    {
        return 0;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < len; ++i)
    {
        value = (value << 8) | s->data[s->pos + i];
    }
    s->pos += len;
    return value;
}

int64_t ebml_read_sint(EbmlStream* s, size_t len)
{
    if (len == 0 || len > 8 || len > ebml_stream_remaining(s))
    {
        return 0;
    }
    /* 符号扩展：如果最高位是 1，用 0xFF 填充高位 */
    int64_t value = 0;
    if (s->data[s->pos] & 0x80)
    {
        value = -1; /* 全 1 */
    }
    for (size_t i = 0; i < len; ++i)
    {
        value = (value << 8) | s->data[s->pos + i];
    }
    s->pos += len;
    return value;
}

double ebml_read_float(EbmlStream* s, size_t len)
{
    if (len == 4 && ebml_stream_remaining(s) >= 4)
    {
        /* IEEE 754 32-bit float, big-endian */
        uint32_t bits = 0;
        for (int i = 0; i < 4; ++i)
        {
            bits = (bits << 8) | s->data[s->pos + i];
        }
        s->pos += 4;
        float f;
        memcpy(&f, &bits, sizeof(f));
        return (double)f;
    }
    else if (len == 8 && ebml_stream_remaining(s) >= 8)
    {
        /* IEEE 754 64-bit double, big-endian */
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
        {
            bits = (bits << 8) | s->data[s->pos + i];
        }
        s->pos += 8;
        double d;
        memcpy(&d, &bits, sizeof(d));
        return d;
    }
    s->pos += len;
    return 0.0;
}

const char* ebml_read_string(EbmlStream* s, size_t len, size_t* out_len)
{
    if (len > ebml_stream_remaining(s))
    {
        len = ebml_stream_remaining(s);
    }
    const char* str = (const char*)(s->data + s->pos);
    s->pos += len;

    /* 去掉尾部 NUL */
    size_t actual_len = len;
    while (actual_len > 0 && str[actual_len - 1] == '\0')
    {
        --actual_len;
    }
    if (out_len) *out_len = actual_len;
    return str;
}

const uint8_t* ebml_read_binary(EbmlStream* s, size_t len)
{
    if (len > ebml_stream_remaining(s))
    {
        return NULL;
    }
    const uint8_t* ptr = s->data + s->pos;
    s->pos += len;
    return ptr;
}

void ebml_skip(EbmlStream* s, size_t bytes)
{
    if (bytes > ebml_stream_remaining(s))
    {
        s->pos = s->size;
    }
    else
    {
        s->pos += bytes;
    }
}
