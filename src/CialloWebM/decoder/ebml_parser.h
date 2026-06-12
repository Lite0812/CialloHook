/*
 * ebml_parser.h — EBML (Extensible Binary Meta Language) 底层解析器
 *
 * WebM 是 Matroska 的子集，底层格式是 EBML。
 * 本模块处理：变长整数解析、Element ID/Size 读取、数据流导航。
 */

#ifndef CIALLO_EBML_PARSER_H
#define CIALLO_EBML_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 数据流抽象 ---- */
typedef struct EbmlStream
{
    const uint8_t* data;     /* 数据起始指针 */
    size_t         size;     /* 数据总长度 */
    size_t         pos;      /* 当前读取位置 */
} EbmlStream;

/* ---- EBML Element ---- */
typedef struct EbmlElement
{
    uint32_t id;          /* Element ID（最多 4 字节） */
    int64_t  size;        /* 内容长度，-1 表示不确定长度 */
    size_t   data_offset; /* 内容起始位置（在 stream 中的偏移） */
    size_t   header_size; /* ID + Size 字段的总字节数 */
} EbmlElement;

/* 初始化数据流 */
void ebml_stream_init(EbmlStream* s, const uint8_t* data, size_t size);

/* 获取剩余可读字节数 */
size_t ebml_stream_remaining(const EbmlStream* s);

/* 读取一个 EBML 变长整数（VINT），返回解码后的值
 * out_bytes: 输出实际消耗的字节数
 * 返回值: >= 0 成功，-1 失败 */
int64_t ebml_read_vint(EbmlStream* s, int* out_bytes);

/* 读取 EBML Element ID（返回 uint32_t 值）
 * out_bytes: 输出 ID 占用的字节数
 * 返回值: > 0 成功，0 失败 */
uint32_t ebml_read_id(EbmlStream* s, int* out_bytes);

/* 读取下一个 Element（ID + Size），填充 EbmlElement
 * 返回值: 1 成功，0 已到末尾或失败 */
int ebml_read_element(EbmlStream* s, EbmlElement* elem);

/* 跳过当前 Element 的内容（将 pos 移到下一个 Element） */
void ebml_skip_element(EbmlStream* s, const EbmlElement* elem);

/* 在当前 Element 范围内创建子流（用于解析嵌套 Element） */
void ebml_sub_stream(const EbmlStream* parent, const EbmlElement* elem,
                     EbmlStream* sub);

/* ---- 数据读取辅助函数 ---- */

/* 读取无符号整数（1~8 字节大端序） */
uint64_t ebml_read_uint(EbmlStream* s, size_t len);

/* 读取有符号整数（1~8 字节大端序） */
int64_t ebml_read_sint(EbmlStream* s, size_t len);

/* 读取浮点数（4 字节 float 或 8 字节 double） */
double ebml_read_float(EbmlStream* s, size_t len);

/* 读取 UTF-8 字符串（不复制，返回指向流内部的指针） */
const char* ebml_read_string(EbmlStream* s, size_t len, size_t* out_len);

/* 读取原始二进制数据（不复制） */
const uint8_t* ebml_read_binary(EbmlStream* s, size_t len);

/* 跳过指定字节 */
void ebml_skip(EbmlStream* s, size_t bytes);

/* ---- 常用 WebM Element ID 定义 ---- */

/* Level 0 */
#define EBML_ID_HEADER            0x1A45DFA3u
#define EBML_ID_SEGMENT           0x18538067u

/* Segment 子元素 */
#define EBML_ID_SEEKHEAD          0x114D9B74u
#define EBML_ID_INFO              0x1549A966u
#define EBML_ID_TRACKS            0x1654AE6Bu
#define EBML_ID_CUES              0x1C53BB6Bu
#define EBML_ID_CLUSTER           0x1F43B675u

/* Info */
#define EBML_ID_TIMESTAMPSCALE    0x2AD7B1u
#define EBML_ID_DURATION          0x4489u

/* Tracks */
#define EBML_ID_TRACKENTRY        0xAEu
#define EBML_ID_TRACKNUMBER       0xD7u
#define EBML_ID_TRACKUID          0x73C5u
#define EBML_ID_TRACKTYPE         0x83u
#define EBML_ID_CODECID           0x86u
#define EBML_ID_CODECPRIVATE      0x63A2u
#define EBML_ID_DEFAULTDURATION   0x23E383u

/* Video */
#define EBML_ID_VIDEO             0xE0u
#define EBML_ID_PIXELWIDTH        0xB0u
#define EBML_ID_PIXELHEIGHT       0xBAu
#define EBML_ID_ALPHAMODE         0x53C0u

/* Audio */
#define EBML_ID_AUDIO             0xE1u

/* Cluster */
#define EBML_ID_TIMESTAMP         0xE7u
#define EBML_ID_SIMPLEBLOCK       0xA3u
#define EBML_ID_BLOCKGROUP        0xA0u
#define EBML_ID_BLOCK             0xA1u
#define EBML_ID_BLOCKDURATION     0x9Bu
#define EBML_ID_BLOCKADDITIONS    0x75A1u
#define EBML_ID_BLOCKMORE         0xA6u
#define EBML_ID_BLOCKADDID        0xEEu
#define EBML_ID_BLOCKADDITIONAL   0xA5u

/* Track 类型常量 */
#define EBML_TRACK_TYPE_VIDEO     1
#define EBML_TRACK_TYPE_AUDIO     2

/* EBML Header 内部 */
#define EBML_ID_EBMLVERSION       0x4286u
#define EBML_ID_DOCTYPE           0x4282u
#define EBML_ID_DOCTYPEVERSION    0x4287u

#ifdef __cplusplus
}
#endif

#endif /* CIALLO_EBML_PARSER_H */
