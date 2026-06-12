/*
 * vorbis_mini.c — 最小 Vorbis 解码器
 * 用于 CialloHook WebM 闪屏音频解码，输出 PCM
 * 
 * 参考: Vorbis I Specification (https://xiph.org/vorbis/doc/Vorbis_I_spec.html)
 * 仅实现 Floor1, Residue 0/1/2, 标准 iMDCT
 */

#include "vorbis_mini.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VB_MAX_CHANNELS    8
#define VB_MAX_CODEBOOKS   256
#define VB_MAX_FLOORS      64
#define VB_MAX_RESIDUES    64
#define VB_MAX_MAPPINGS    64
#define VB_MAX_MODES       64
#define VB_MAX_BLOCKSIZE   8192
#define VB_MAX_FLOOR1_X    256
#define VB_MAX_PARTITIONS  32
#define VB_MAX_CLASSES     16

/* ================================================================
 * Section 1: Bit Reader (LSB first)
 * ================================================================ */

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t byte_pos;
    int bit_pos;  /* 0..7 within current byte */
} BitReader;

static void br_init(BitReader* br, const uint8_t* data, size_t size) {
    br->data = data; br->size = size;
    br->byte_pos = 0; br->bit_pos = 0;
}

static int br_eof(const BitReader* br) {
    return br->byte_pos >= br->size;
}

static uint32_t br_read(BitReader* br, int n) {
    if (n <= 0 || n > 32) return 0;
    uint32_t val = 0;
    int shift = 0;
    while (n > 0 && br->byte_pos < br->size) {
        int avail = 8 - br->bit_pos;
        int take = n < avail ? n : avail;
        uint32_t bits = (br->data[br->byte_pos] >> br->bit_pos) & ((1u << take) - 1);
        val |= bits << shift;
        shift += take;
        n -= take;
        br->bit_pos += take;
        if (br->bit_pos >= 8) { br->bit_pos = 0; br->byte_pos++; }
    }
    return val;
}

static uint32_t br_read1(BitReader* br) { return br_read(br, 1); }

/* ilog: floor(log2(v)) + 1 for v>0, 0 for v==0 */
static int ilog(uint32_t v) {
    int r = 0;
    while (v > 0) { r++; v >>= 1; }
    return r;
}

/* ================================================================
 * Section 2: Codebook
 * ================================================================ */

typedef struct {
    int entries;
    int dimensions;
    /* Huffman decode table: for each entry, codeword length */
    uint8_t* lengths;      /* [entries] */
    /* Sorted decode: fast lookup */
    uint32_t* sorted_codewords;  /* sorted codeword values */
    int*      sorted_values;     /* original entry index */
    int       sorted_count;
    /* VQ vectors */
    float*    vq_table;    /* [entries * dimensions] or NULL */
    float     vq_min;
    float     vq_delta;
    int       lookup_type;
    int       sequence_p;
} VBCodebook;

static uint32_t bit_reverse(uint32_t n, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (n & 1); n >>= 1;
    }
    return r;
}

/* Build sorted table for Huffman decode */
static void codebook_build_sorted(VBCodebook* cb) {
    int n = cb->entries;
    /* Count used entries */
    cb->sorted_count = 0;
    for (int i = 0; i < n; i++)
        if (cb->lengths[i] > 0 && cb->lengths[i] < 255) cb->sorted_count++;
    if (cb->sorted_count == 0) return;

    cb->sorted_codewords = (uint32_t*)calloc(cb->sorted_count, sizeof(uint32_t));
    cb->sorted_values = (int*)calloc(cb->sorted_count, sizeof(int));

    /* Assign codewords using canonical Huffman */
    uint32_t next_code[33] = {0};
    /* Count lengths */
    int len_count[33] = {0};
    for (int i = 0; i < n; i++)
        if (cb->lengths[i] > 0 && cb->lengths[i] <= 32) len_count[cb->lengths[i]]++;

    /* Assign starting codes per length */
    uint32_t code = 0;
    for (int bits = 1; bits <= 32; bits++) {
        code = (code + len_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    /* Fix: start from length 1 */
    next_code[0] = 0;
    code = 0;
    for (int bits = 1; bits <= 32; bits++) {
        next_code[bits] = code;
        code = (code + len_count[bits]) << 1;
    }

    int idx = 0;
    for (int i = 0; i < n; i++) {
        int len = cb->lengths[i];
        if (len > 0 && len <= 32) {
            uint32_t cw = next_code[len]++;
            /* Store bit-reversed for LSB-first matching */
            cb->sorted_codewords[idx] = bit_reverse(cw, len);
            cb->sorted_values[idx] = i;
            idx++;
        }
    }
}

/* Decode one codeword from bitstream */
static int codebook_decode(const VBCodebook* cb, BitReader* br) {
    if (cb->sorted_count == 0) return -1;

    /* Brute-force search (simple but correct) */
    uint32_t bits_read = 0;
    int bits_n = 0;

    for (bits_n = 0; bits_n < 32 && !br_eof(br); ) {
        uint32_t b = br_read1(br);
        bits_read |= (b << bits_n);
        bits_n++;

        /* Check all codewords of this length */
        for (int i = 0; i < cb->sorted_count; i++) {
            int len = cb->lengths[cb->sorted_values[i]];
            if (len == bits_n && cb->sorted_codewords[i] == bits_read) {
                return cb->sorted_values[i];
            }
        }
    }
    return -1;
}

/* Decode VQ vector */
static void codebook_decode_vq(const VBCodebook* cb, int entry, float* out) {
    if (!cb->vq_table || entry < 0 || entry >= cb->entries) {
        for (int i = 0; i < cb->dimensions; i++) out[i] = 0.0f;
        return;
    }
    memcpy(out, cb->vq_table + entry * cb->dimensions, cb->dimensions * sizeof(float));
}

/* ================================================================
 * Section 3: Floor Type 1
 * ================================================================ */

typedef struct {
    int partitions;
    int partition_class[VB_MAX_PARTITIONS];
    int class_dim[VB_MAX_CLASSES];
    int class_sub[VB_MAX_CLASSES];
    int class_masterbook[VB_MAX_CLASSES];
    int class_subbook[VB_MAX_CLASSES][8];
    int multiplier;
    int x_list[VB_MAX_FLOOR1_X];
    int x_count;
    int* sorted_order; /* indices sorted by x value */
} VBFloor1;

static int floor1_x_compare(const void* a, const void* b, void* ctx) {
    const int* x_list = (const int*)ctx;
    return x_list[*(const int*)a] - x_list[*(const int*)b];
}

/* Simple sort since qsort_r/qsort_s is not portable */
static void floor1_sort_x(VBFloor1* f) {
    int n = f->x_count;
    f->sorted_order = (int*)malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) f->sorted_order[i] = i;
    /* Insertion sort (n is small, typically < 100) */
    for (int i = 1; i < n; i++) {
        int key = f->sorted_order[i];
        int key_x = f->x_list[key];
        int j = i - 1;
        while (j >= 0 && f->x_list[f->sorted_order[j]] > key_x) {
            f->sorted_order[j + 1] = f->sorted_order[j];
            j--;
        }
        f->sorted_order[j + 1] = key;
    }
}

/* ================================================================
 * Section 4: Residue
 * ================================================================ */

typedef struct {
    int type;  /* 0, 1, or 2 */
    int begin, end, part_size;
    int classifications;
    int classbook;
    int cascade[64];        /* max 64 classifications */
    int books[64][8];       /* [classification][pass] */
} VBResidue;

/* ================================================================
 * Section 5: Mapping & Mode
 * ================================================================ */

typedef struct {
    int coupling_steps;
    int magnitude[256];
    int angle[256];
    int mux[VB_MAX_CHANNELS];
    int submap_floor[16];
    int submap_residue[16];
    int submaps;
} VBMapping;

typedef struct {
    int blockflag;   /* 0 = short, 1 = long */
    int mapping;     /* index into mappings */
} VBMode;

/* ================================================================
 * Section 6: FFT & iMDCT
 * ================================================================ */

static void fft_recursive(float* re, float* im, int n, int sign) {
    if (n <= 1) return;
    int h = n / 2;
    /* Separate even/odd */
    float* er = (float*)malloc(h * sizeof(float));
    float* ei = (float*)malloc(h * sizeof(float));
    float* or_ = (float*)malloc(h * sizeof(float));
    float* oi = (float*)malloc(h * sizeof(float));
    for (int i = 0; i < h; i++) {
        er[i] = re[2*i]; ei[i] = im[2*i];
        or_[i] = re[2*i+1]; oi[i] = im[2*i+1];
    }
    fft_recursive(er, ei, h, sign);
    fft_recursive(or_, oi, h, sign);
    for (int k = 0; k < h; k++) {
        float angle = (float)(sign * 2.0 * M_PI * k / n);
        float wr = cosf(angle), wi = sinf(angle);
        float tr = wr * or_[k] - wi * oi[k];
        float ti = wr * oi[k] + wi * or_[k];
        re[k] = er[k] + tr;     im[k] = ei[k] + ti;
        re[k+h] = er[k] - tr;   im[k+h] = ei[k] - ti;
    }
    free(er); free(ei); free(or_); free(oi);
}

static void imdct(const float* input, float* output, int n) {
    /* n = block size, input has n/2 elements, output has n elements */
    int n2 = n / 2, n4 = n / 4;
    float* re = (float*)calloc(n4, sizeof(float));
    float* im = (float*)calloc(n4, sizeof(float));

    /* Pre-twiddle */
    for (int k = 0; k < n4; k++) {
        float xr = input[2*k];
        float xi = (2*k+1 < n2) ? input[2*k+1] : 0.0f;
        float angle = (float)(2.0 * M_PI / n * (4*k + 1) * 0.25);
        float c = cosf(angle), s = sinf(angle);
        re[k] =  xr * c + xi * s;
        im[k] = -xr * s + xi * c;
    }

    /* Inverse FFT (sign = +1) */
    fft_recursive(re, im, n4, 1);
    /* Scale */
    float scale = 1.0f / (float)n4;
    for (int k = 0; k < n4; k++) { re[k] *= scale; im[k] *= scale; }

    /* Post-twiddle */
    float* pr = (float*)calloc(n4, sizeof(float));
    float* pi = (float*)calloc(n4, sizeof(float));
    for (int k = 0; k < n4; k++) {
        float angle = (float)(2.0 * M_PI / n * (4*k + 1) * 0.25);
        float c = cosf(angle), s = sinf(angle);
        pr[k] =  re[k] * c + im[k] * s;
        pi[k] = -re[k] * s + im[k] * c;
    }

    /* Rearrange to output (windowed later) */
    for (int k = 0; k < n4; k++) {
        output[2*k]          =  pi[n4 - 1 - k] * 0.5f;
        output[n2 - 1 - 2*k] = -pi[n4 - 1 - k] * 0.5f;
    }
    for (int k = 0; k < n4; k++) {
        output[n2 + 2*k]          = -pr[k] * 0.5f;
        output[n - 1 - 2*k]       = -pr[k] * 0.5f;
    }

    free(re); free(im); free(pr); free(pi);
}

/* Vorbis window function */
static float vorbis_window(int position, int block_size) {
    float x = ((float)position + 0.5f) / (float)block_size;
    float s = sinf((float)(M_PI * 0.5) * x);
    return sinf((float)(M_PI * 0.5) * s * s);
}

/* ================================================================
 * Section 7: Decoder State
 * ================================================================ */

struct VorbisDecoder {
    int channels;
    int sample_rate;
    int blocksize[2];  /* short, long */

    VBCodebook  codebooks[VB_MAX_CODEBOOKS];
    int         codebook_count;

    VBFloor1    floors[VB_MAX_FLOORS];
    int         floor_count;

    VBResidue   residues[VB_MAX_RESIDUES];
    int         residue_count;

    VBMapping   mappings[VB_MAX_MAPPINGS];
    int         mapping_count;

    VBMode      modes[VB_MAX_MODES];
    int         mode_count;

    /* Overlap-add state */
    float*      prev_block[VB_MAX_CHANNELS]; /* previous block for overlap */
    int         prev_blocksize;
    int         prev_valid;

    /* Output buffer */
    float*      pcm_buf;      /* interleaved output */
    int         pcm_buf_cap;  /* capacity in samples per channel */
};

/* ================================================================
 * Section 8: Setup Header Parsing
 * ================================================================ */

/* Parse Xiph lacing for Matroska CodecPrivate */
static int parse_xiph_size(const uint8_t** p, const uint8_t* end) {
    int size = 0;
    while (*p < end) {
        uint8_t b = **p; (*p)++;
        size += b;
        if (b < 255) return size;
    }
    return size;
}

static int parse_identification(VorbisDecoder* dec, const uint8_t* data, size_t len) {
    if (len < 30) return 0;
    /* packet_type=1, "vorbis" */
    if (data[0] != 1 || memcmp(data+1, "vorbis", 6) != 0) return 0;

    BitReader br;
    br_init(&br, data + 7, len - 7);

    uint32_t version = br_read(&br, 32);
    if (version != 0) return 0;

    dec->channels = br_read(&br, 8);
    dec->sample_rate = br_read(&br, 32);
    br_read(&br, 32); /* bitrate_max */
    br_read(&br, 32); /* bitrate_nominal */
    br_read(&br, 32); /* bitrate_min */

    int bs0 = br_read(&br, 4);
    int bs1 = br_read(&br, 4);
    dec->blocksize[0] = 1 << bs0;
    dec->blocksize[1] = 1 << bs1;

    if (dec->channels < 1 || dec->channels > VB_MAX_CHANNELS) return 0;
    if (dec->blocksize[0] < 64 || dec->blocksize[1] > VB_MAX_BLOCKSIZE) return 0;
    if (dec->blocksize[0] > dec->blocksize[1]) return 0;

    return 1;
}

static float float32_unpack(uint32_t val) {
    int mantissa = val & 0x1fffff;
    int sign = val & 0x80000000;
    int exponent = (val & 0x7fe00000) >> 21;
    if (sign) mantissa = -mantissa;
    return (float)ldexp((double)mantissa, exponent - 788);
}

static int lookup1_values(int entries, int dim) {
    /* floor(entries^(1/dim)) */
    int r = (int)floor(pow((double)entries, 1.0 / (double)dim));
    /* Verify and adjust */
    while ((int)pow((double)(r + 1), (double)dim) <= entries) r++;
    return r;
}

static int parse_codebook(VBCodebook* cb, BitReader* br) {
    uint32_t sync = br_read(br, 24);
    if (sync != 0x564342) return 0; /* "BCV" */

    cb->dimensions = br_read(br, 16);
    cb->entries = br_read(br, 24);

    cb->lengths = (uint8_t*)calloc(cb->entries, 1);

    int ordered = br_read1(br);
    if (!ordered) {
        int sparse = br_read1(br);
        for (int i = 0; i < cb->entries; i++) {
            if (sparse) {
                int flag = br_read1(br);
                if (flag) {
                    cb->lengths[i] = br_read(br, 5) + 1;
                } else {
                    cb->lengths[i] = 255; /* unused */
                }
            } else {
                cb->lengths[i] = br_read(br, 5) + 1;
            }
        }
    } else {
        int cur_len = br_read(br, 5) + 1;
        int i = 0;
        while (i < cb->entries) {
            int count = br_read(br, ilog(cb->entries - i));
            for (int j = 0; j < count && i < cb->entries; j++, i++) {
                cb->lengths[i] = cur_len;
            }
            cur_len++;
        }
    }

    codebook_build_sorted(cb);

    /* VQ lookup */
    cb->lookup_type = br_read(br, 4);
    if (cb->lookup_type == 1 || cb->lookup_type == 2) {
        cb->vq_min = float32_unpack(br_read(br, 32));
        cb->vq_delta = float32_unpack(br_read(br, 32));
        int value_bits = br_read(br, 4) + 1;
        cb->sequence_p = br_read1(br);

        int lookup_values;
        if (cb->lookup_type == 1) {
            lookup_values = lookup1_values(cb->entries, cb->dimensions);
        } else {
            lookup_values = cb->entries * cb->dimensions;
        }

        uint32_t* mult = (uint32_t*)calloc(lookup_values, sizeof(uint32_t));
        for (int i = 0; i < lookup_values; i++) {
            mult[i] = br_read(br, value_bits);
        }

        /* Build VQ table */
        cb->vq_table = (float*)calloc(cb->entries * cb->dimensions, sizeof(float));
        for (int j = 0; j < cb->entries; j++) {
            float last = 0.0f;
            int index_divisor = 1;
            for (int k = 0; k < cb->dimensions; k++) {
                int offset;
                if (cb->lookup_type == 1) {
                    offset = (j / index_divisor) % lookup_values;
                } else {
                    offset = j * cb->dimensions + k;
                }
                float val = cb->vq_min + (float)mult[offset] * cb->vq_delta;
                if (cb->sequence_p) { val += last; last = val; }
                cb->vq_table[j * cb->dimensions + k] = val;
                if (cb->lookup_type == 1) index_divisor *= lookup_values;
            }
        }
        free(mult);
    }

    return 1;
}

static int parse_floor1(VBFloor1* f, BitReader* br, int codebook_count) {
    f->partitions = br_read(br, 5);
    int max_class = -1;
    for (int i = 0; i < f->partitions; i++) {
        f->partition_class[i] = br_read(br, 4);
        if (f->partition_class[i] > max_class) max_class = f->partition_class[i];
    }

    for (int i = 0; i <= max_class; i++) {
        f->class_dim[i] = br_read(br, 3) + 1;
        f->class_sub[i] = br_read(br, 2);
        if (f->class_sub[i]) {
            f->class_masterbook[i] = br_read(br, 8);
        }
        int subcount = 1 << f->class_sub[i];
        for (int j = 0; j < subcount; j++) {
            f->class_subbook[i][j] = (int)br_read(br, 8) - 1;
        }
    }

    f->multiplier = br_read(br, 2) + 1;
    int rangebits = br_read(br, 4);

    f->x_count = 2;
    f->x_list[0] = 0;
    f->x_list[1] = 1 << rangebits;

    for (int i = 0; i < f->partitions; i++) {
        int cls = f->partition_class[i];
        for (int j = 0; j < f->class_dim[cls]; j++) {
            if (f->x_count >= VB_MAX_FLOOR1_X) return 0;
            f->x_list[f->x_count++] = br_read(br, rangebits);
        }
    }

    floor1_sort_x(f);
    return 1;
}

static int parse_residue(VBResidue* r, BitReader* br) {
    r->begin = br_read(br, 24);
    r->end = br_read(br, 24);
    r->part_size = br_read(br, 24) + 1;
    r->classifications = br_read(br, 6) + 1;
    r->classbook = br_read(br, 8);

    for (int i = 0; i < r->classifications; i++) {
        int high = 0;
        int low = br_read(br, 3);
        if (br_read1(br)) high = br_read(br, 5);
        r->cascade[i] = high * 8 + low;
    }

    for (int i = 0; i < r->classifications; i++) {
        for (int j = 0; j < 8; j++) {
            if (r->cascade[i] & (1 << j)) {
                r->books[i][j] = br_read(br, 8);
            } else {
                r->books[i][j] = -1;
            }
        }
    }
    return 1;
}

static int parse_setup(VorbisDecoder* dec, const uint8_t* data, size_t len) {
    if (len < 7) return 0;
    if (data[0] != 5 || memcmp(data+1, "vorbis", 6) != 0) return 0;

    BitReader br;
    br_init(&br, data + 7, len - 7);

    /* Codebooks */
    dec->codebook_count = br_read(&br, 8) + 1;
    for (int i = 0; i < dec->codebook_count; i++) {
        if (!parse_codebook(&dec->codebooks[i], &br)) return 0;
    }

    /* Time domain transforms (placeholder, skip) */
    int time_count = br_read(&br, 6) + 1;
    for (int i = 0; i < time_count; i++) br_read(&br, 16);

    /* Floors */
    dec->floor_count = br_read(&br, 6) + 1;
    for (int i = 0; i < dec->floor_count; i++) {
        int floor_type = br_read(&br, 16);
        if (floor_type != 1) return 0; /* Only floor type 1 */
        if (!parse_floor1(&dec->floors[i], &br, dec->codebook_count)) return 0;
    }

    /* Residues */
    dec->residue_count = br_read(&br, 6) + 1;
    for (int i = 0; i < dec->residue_count; i++) {
        dec->residues[i].type = br_read(&br, 16);
        if (dec->residues[i].type > 2) return 0;
        if (!parse_residue(&dec->residues[i], &br)) return 0;
    }

    /* Mappings */
    dec->mapping_count = br_read(&br, 6) + 1;
    for (int i = 0; i < dec->mapping_count; i++) {
        VBMapping* m = &dec->mappings[i];
        int mapping_type = br_read(&br, 16);
        (void)mapping_type; /* always 0 */

        m->submaps = 1;
        if (br_read1(&br)) m->submaps = br_read(&br, 4) + 1;

        m->coupling_steps = 0;
        if (br_read1(&br)) {
            m->coupling_steps = br_read(&br, 8) + 1;
            int ch_bits = ilog(dec->channels - 1);
            for (int j = 0; j < m->coupling_steps; j++) {
                m->magnitude[j] = br_read(&br, ch_bits);
                m->angle[j] = br_read(&br, ch_bits);
            }
        }

        br_read(&br, 2); /* reserved */

        if (m->submaps > 1) {
            for (int j = 0; j < dec->channels; j++) {
                m->mux[j] = br_read(&br, 4);
            }
        } else {
            for (int j = 0; j < dec->channels; j++) m->mux[j] = 0;
        }

        for (int j = 0; j < m->submaps; j++) {
            br_read(&br, 8); /* unused time config */
            m->submap_floor[j] = br_read(&br, 8);
            m->submap_residue[j] = br_read(&br, 8);
        }
    }

    /* Modes */
    dec->mode_count = br_read(&br, 6) + 1;
    for (int i = 0; i < dec->mode_count; i++) {
        dec->modes[i].blockflag = br_read1(&br);
        br_read(&br, 16); /* window_type = 0 */
        br_read(&br, 16); /* transform_type = 0 */
        dec->modes[i].mapping = br_read(&br, 8);
    }

    /* Framing bit */
    br_read1(&br);

    return 1;
}

/* ================================================================
 * Section 9: Floor1 Decode
 * ================================================================ */

static int render_point(int x0, int y0, int x1, int y1, int x) {
    int dy = y1 - y0;
    int adx = x1 - x0;
    int ady = dy < 0 ? -dy : dy;
    int err = ady * (x - x0);
    int off = err / adx;
    return dy < 0 ? y0 - off : y0 + off;
}

static int inverse_db_table_inited = 0;
static float inverse_db_table[256];

static void init_inverse_db_table(void) {
    if (inverse_db_table_inited) return;
    for (int i = 0; i < 256; i++) {
        inverse_db_table[i] = (float)pow(10.0, ((double)i / 2.0 - 63.5) * 0.05);
    }
    inverse_db_table_inited = 1;
}

static int floor1_decode(VorbisDecoder* dec, VBFloor1* f, BitReader* br,
                          float* floor_out, int n) {
    init_inverse_db_table();

    int amp = br_read1(br);
    if (!amp) {
        memset(floor_out, 0, n * sizeof(float));
        return 0; /* this channel is unused */
    }

    int range_v;
    switch (f->multiplier) {
        case 1: range_v = 256; break;
        case 2: range_v = 128; break;
        case 3: range_v = 86; break;
        case 4: range_v = 64; break;
        default: range_v = 256; break;
    }

    int y_list[VB_MAX_FLOOR1_X];
    int final_y[VB_MAX_FLOOR1_X];
    int step2_flag[VB_MAX_FLOOR1_X];

    y_list[0] = br_read(br, ilog(range_v - 1));
    y_list[1] = br_read(br, ilog(range_v - 1));
    step2_flag[0] = 1;
    step2_flag[1] = 1;

    int offset = 2;
    for (int i = 0; i < f->partitions; i++) {
        int cls = f->partition_class[i];
        int cdim = f->class_dim[cls];
        int cbits = f->class_sub[cls];
        int csub = (1 << cbits) - 1;
        int cval = 0;
        if (cbits > 0) {
            int book_idx = f->class_masterbook[cls];
            cval = codebook_decode(&dec->codebooks[book_idx], br);
            if (cval < 0) cval = 0;
        }
        for (int j = 0; j < cdim; j++) {
            int book = f->class_subbook[cls][cval & csub];
            cval >>= cbits;
            if (book >= 0) {
                y_list[offset + j] = codebook_decode(&dec->codebooks[book], br);
                if (y_list[offset + j] < 0) y_list[offset + j] = 0;
            } else {
                y_list[offset + j] = 0;
            }
        }
        offset += cdim;
    }

    /* Amplitude value interpretation (Vorbis spec section 7.2.3) */
    final_y[0] = y_list[0];
    final_y[1] = y_list[1];

    for (int i = 2; i < f->x_count; i++) {
        int low_idx = 0, high_idx = 0;
        int cur_x = f->x_list[i];
        int best_lo = -1, best_hi = 99999;

        /* Find low/high neighbors */
        for (int j = 0; j < i; j++) {
            int jx = f->x_list[j];
            if (jx < cur_x && jx > best_lo) { best_lo = jx; low_idx = j; }
            if (jx > cur_x && jx < best_hi) { best_hi = jx; high_idx = j; }
        }

        int predicted = render_point(f->x_list[low_idx], final_y[low_idx],
                                      f->x_list[high_idx], final_y[high_idx],
                                      cur_x);

        int val = y_list[i];
        int highroom = range_v - predicted;
        int lowroom = predicted;
        int room = (highroom < lowroom ? highroom : lowroom) * 2;

        if (val != 0) {
            step2_flag[low_idx] = 1;
            step2_flag[high_idx] = 1;
            step2_flag[i] = 1;
            if (val >= room) {
                if (highroom > lowroom) {
                    final_y[i] = val - lowroom + predicted;
                } else {
                    final_y[i] = predicted - val + highroom - 1;
                }
            } else {
                if (val & 1) {
                    final_y[i] = predicted - ((val + 1) >> 1);
                } else {
                    final_y[i] = predicted + (val >> 1);
                }
            }
        } else {
            step2_flag[i] = 0;
            final_y[i] = predicted;
        }
    }

    /* Apply multiplier */
    for (int i = 0; i < f->x_count; i++) {
        final_y[i] *= f->multiplier;
    }

    /* Synthesize floor curve */
    /* Use sorted order to render line segments */
    int hx = 0;
    int lx = 0, ly = final_y[f->sorted_order[0]] * step2_flag[f->sorted_order[0]];

    for (int i = 1; i < f->x_count; i++) {
        int idx = f->sorted_order[i];
        if (!step2_flag[idx]) continue;
        hx = f->x_list[idx];
        int hy = final_y[idx];
        if (hx > n) hx = n;
        for (int x = lx; x < hx && x < n; x++) {
            int val = render_point(lx, ly, hx, hy, x);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            floor_out[x] = inverse_db_table[val];
        }
        lx = hx;
        ly = hy;
    }
    /* Fill remainder */
    for (int x = lx; x < n; x++) {
        int val = ly;
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        floor_out[x] = inverse_db_table[val];
    }

    return 1; /* floor decoded */
}

/* ================================================================
 * Section 10: Residue Decode
 * ================================================================ */

static void decode_residue(VorbisDecoder* dec, VBResidue* r, BitReader* br,
                           float** ch_residue, int ch_count, int* do_not_decode,
                           int n) {
    int actual_size = n;
    int limit_begin = r->begin < actual_size ? r->begin : actual_size;
    int limit_end = r->end < actual_size ? r->end : actual_size;
    int residue_size = limit_end - limit_begin;
    if (residue_size <= 0) return;

    int classwords_per_codeword = dec->codebooks[r->classbook].dimensions;
    int n_to_read = residue_size / r->part_size;
    if (n_to_read <= 0) return;

    int pass_limit = 8;

    /* Allocate classifications */
    int total_parts = n_to_read;
    int ch_used = (r->type == 2) ? 1 : ch_count;

    int** classifications = (int**)calloc(ch_used, sizeof(int*));
    for (int i = 0; i < ch_used; i++)
        classifications[i] = (int*)calloc(total_parts, sizeof(int));

    for (int pass = 0; pass < pass_limit; pass++) {
        int partition_count = 0;
        while (partition_count < n_to_read) {
            if (pass == 0) {
                for (int j = 0; j < ch_used; j++) {
                    if (r->type == 2 || !do_not_decode[j]) {
                        int temp = codebook_decode(&dec->codebooks[r->classbook], br);
                        if (temp < 0) temp = 0;
                        /* Decompose temp into classifications */
                        for (int i = classwords_per_codeword - 1; i >= 0; i--) {
                            if (partition_count + i < n_to_read) {
                                classifications[j][partition_count + i] = temp % r->classifications;
                            }
                            temp /= r->classifications;
                        }
                    }
                }
            }

            for (int i = 0; i < classwords_per_codeword && partition_count < n_to_read; i++, partition_count++) {
                for (int j = 0; j < ch_used; j++) {
                    if (r->type == 2 || !do_not_decode[j]) {
                        int vqclass = classifications[j][partition_count];
                        if ((r->cascade[vqclass] & (1 << pass)) != 0) {
                            int book_idx = r->books[vqclass][pass];
                            if (book_idx >= 0 && book_idx < dec->codebook_count) {
                                VBCodebook* book = &dec->codebooks[book_idx];
                                int offset_base;
                                if (r->type == 2) {
                                    offset_base = limit_begin + partition_count * r->part_size;
                                } else {
                                    offset_base = limit_begin + partition_count * r->part_size;
                                }
                                /* Decode vectors */
                                int step = r->part_size / book->dimensions;
                                float vec[256]; /* max dimensions */
                                if (r->type == 0) {
                                    for (int k = 0; k < step; k++) {
                                        int entry = codebook_decode(book, br);
                                        if (entry >= 0 && book->vq_table) {
                                            codebook_decode_vq(book, entry, vec);
                                            for (int l = 0; l < book->dimensions; l++) {
                                                int idx = offset_base + k + l * step;
                                                if (r->type == 2) {
                                                    int real_ch = idx % ch_count;
                                                    int real_idx = idx / ch_count;
                                                    if (real_idx < n)
                                                        ch_residue[real_ch][real_idx] += vec[l];
                                                } else {
                                                    if (idx < n)
                                                        ch_residue[j][idx] += vec[l];
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    /* Type 1 and 2: interleaved */
                                    int sample_offset = offset_base;
                                    int limit = offset_base + r->part_size;
                                    while (sample_offset < limit) {
                                        int entry = codebook_decode(book, br);
                                        if (entry >= 0 && book->vq_table) {
                                            codebook_decode_vq(book, entry, vec);
                                            for (int l = 0; l < book->dimensions && sample_offset < limit; l++, sample_offset++) {
                                                if (r->type == 2) {
                                                    int real_ch = sample_offset % ch_count;
                                                    int real_idx = sample_offset / ch_count;
                                                    if (real_idx < n)
                                                        ch_residue[real_ch][real_idx] += vec[l];
                                                } else {
                                                    if (sample_offset < n)
                                                        ch_residue[j][sample_offset] += vec[l];
                                                }
                                            }
                                        } else {
                                            sample_offset += book->dimensions;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < ch_used; i++) free(classifications[i]);
    free(classifications);
}

/* ================================================================
 * Section 11: Audio Packet Decode
 * ================================================================ */

int vorbis_decode_packet(VorbisDecoder* dec,
    const uint8_t* packet, size_t packet_len,
    float** out_pcm)
{
    if (!dec || !packet || packet_len == 0) return -1;

    BitReader br;
    br_init(&br, packet, packet_len);

    /* Packet type (should be 0 for audio) */
    if (br_read1(&br) != 0) return -1;

    /* Mode number */
    int mode_bits = ilog(dec->mode_count - 1);
    int mode_num = br_read(&br, mode_bits);
    if (mode_num >= dec->mode_count) return -1;

    VBMode* mode = &dec->modes[mode_num];
    int blocksize = dec->blocksize[mode->blockflag];
    int n = blocksize;
    int n2 = n / 2;

    /* Window flags for long blocks */
    int prev_flag = 0, next_flag = 0;
    if (mode->blockflag) {
        prev_flag = br_read1(&br);
        next_flag = br_read1(&br);
    }

    VBMapping* mapping = &dec->mappings[mode->mapping];

    /* Decode floors */
    int no_residue[VB_MAX_CHANNELS] = {0};
    float* floor_buf[VB_MAX_CHANNELS];
    for (int ch = 0; ch < dec->channels; ch++) {
        floor_buf[ch] = (float*)calloc(n2, sizeof(float));
        int submap = mapping->mux[ch];
        int floor_idx = mapping->submap_floor[submap];
        int result = floor1_decode(dec, &dec->floors[floor_idx], &br, floor_buf[ch], n2);
        if (!result) no_residue[ch] = 1;
    }

    /* Decode residue */
    float* residue_buf[VB_MAX_CHANNELS];
    for (int ch = 0; ch < dec->channels; ch++)
        residue_buf[ch] = (float*)calloc(n2, sizeof(float));

    for (int i = 0; i < mapping->submaps; i++) {
        int res_idx = mapping->submap_residue[i];
        /* Collect channels for this submap */
        int sub_ch = 0;
        float* sub_residue[VB_MAX_CHANNELS];
        int sub_no_decode[VB_MAX_CHANNELS];
        for (int ch = 0; ch < dec->channels; ch++) {
            if (mapping->mux[ch] == i) {
                sub_residue[sub_ch] = residue_buf[ch];
                sub_no_decode[sub_ch] = no_residue[ch];
                sub_ch++;
            }
        }
        if (sub_ch > 0) {
            decode_residue(dec, &dec->residues[res_idx], &br,
                          sub_residue, sub_ch, sub_no_decode, n2);
        }
    }

    /* Channel coupling (inverse square polar mapping) */
    for (int step = mapping->coupling_steps - 1; step >= 0; step--) {
        int mag_ch = mapping->magnitude[step];
        int ang_ch = mapping->angle[step];
        float* M = residue_buf[mag_ch];
        float* A = residue_buf[ang_ch];
        for (int j = 0; j < n2; j++) {
            float m = M[j], a = A[j];
            if (m > 0.0f) {
                if (a > 0.0f) { A[j] = m - a; }
                else { A[j] = m; M[j] = m + a; }
            } else {
                if (a > 0.0f) { A[j] = m + a; }
                else { A[j] = m; M[j] = m - a; }
            }
        }
    }

    /* Apply floor to residue (dot product) */
    for (int ch = 0; ch < dec->channels; ch++) {
        if (!no_residue[ch]) {
            for (int j = 0; j < n2; j++) {
                residue_buf[ch][j] *= floor_buf[ch][j];
            }
        }
    }

    /* iMDCT */
    float* mdct_out[VB_MAX_CHANNELS];
    for (int ch = 0; ch < dec->channels; ch++) {
        mdct_out[ch] = (float*)calloc(n, sizeof(float));
        imdct(residue_buf[ch], mdct_out[ch], n);
    }

    /* Windowing */
    int window_left = mode->blockflag && !prev_flag ? dec->blocksize[0] : n;
    int window_right = mode->blockflag && !next_flag ? dec->blocksize[0] : n;

    for (int ch = 0; ch < dec->channels; ch++) {
        /* Left half window */
        int wl2 = window_left / 2;
        int center = n / 2;
        int left_start = center - wl2;
        for (int i = 0; i < wl2; i++) {
            mdct_out[ch][left_start + i] *= vorbis_window(i, window_left);
        }
        for (int i = 0; i < left_start; i++) mdct_out[ch][i] = 0.0f;

        /* Right half window */
        int wr2 = window_right / 2;
        int right_start = center;
        for (int i = 0; i < wr2; i++) {
            mdct_out[ch][right_start + i] *= vorbis_window(wr2 - 1 - i, window_right);
        }
        for (int i = right_start + wr2; i < n; i++) mdct_out[ch][i] = 0.0f;
    }

    /* Overlap-add with previous block */
    int output_samples = 0;

    if (dec->prev_valid) {
        int prev_n = dec->prev_blocksize;
        int prev_n2 = prev_n / 2;
        int cur_n2 = n / 2;
        int overlap = (prev_n2 < cur_n2) ? prev_n2 : cur_n2;

        output_samples = prev_n2;

        /* Ensure PCM buffer is large enough */
        if (output_samples > dec->pcm_buf_cap) {
            dec->pcm_buf_cap = output_samples + 1024;
            dec->pcm_buf = (float*)realloc(dec->pcm_buf,
                dec->pcm_buf_cap * dec->channels * sizeof(float));
        }

        for (int ch = 0; ch < dec->channels; ch++) {
            /* The overlap region */
            int prev_center = prev_n / 2;
            int cur_center = n / 2;
            int prev_start = prev_center;
            int cur_start = cur_center - overlap;

            for (int i = 0; i < output_samples; i++) {
                float sample;
                if (i < output_samples - overlap) {
                    /* Before overlap: only previous block */
                    sample = dec->prev_block[ch][prev_start + i];
                } else {
                    /* In overlap: sum */
                    int ov_idx = i - (output_samples - overlap);
                    float prev_val = (prev_start + i < prev_n) ?
                        dec->prev_block[ch][prev_start + i] : 0.0f;
                    float cur_val = mdct_out[ch][cur_start + ov_idx];
                    sample = prev_val + cur_val;
                }
                /* Clamp */
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                dec->pcm_buf[i * dec->channels + ch] = sample;
            }
        }
    }

    /* Save current block for next overlap */
    for (int ch = 0; ch < dec->channels; ch++) {
        if (!dec->prev_block[ch]) {
            dec->prev_block[ch] = (float*)calloc(VB_MAX_BLOCKSIZE, sizeof(float));
        }
        memcpy(dec->prev_block[ch], mdct_out[ch], n * sizeof(float));
    }
    dec->prev_blocksize = n;
    dec->prev_valid = 1;

    /* Cleanup */
    for (int ch = 0; ch < dec->channels; ch++) {
        free(floor_buf[ch]);
        free(residue_buf[ch]);
        free(mdct_out[ch]);
    }

    if (out_pcm) *out_pcm = dec->pcm_buf;
    return output_samples;
}

/* ================================================================
 * Section 12: Public API
 * ================================================================ */

VorbisDecoder* vorbis_create(const uint8_t* codec_private, size_t len) {
    if (!codec_private || len < 3) return NULL;

    /* Parse Matroska CodecPrivate for Vorbis:
     * [0x02] [size1_xiph] [size2_xiph] [hdr1] [hdr2] [hdr3] */
    const uint8_t* p = codec_private;
    const uint8_t* end = codec_private + len;

    if (*p != 0x02) return NULL;
    p++;

    int size1 = parse_xiph_size(&p, end);
    int size2 = parse_xiph_size(&p, end);
    int size3 = (int)(end - p) - size1 - size2;
    if (size1 <= 0 || size2 <= 0 || size3 <= 0) return NULL;
    if (p + size1 + size2 + size3 > end) return NULL;

    const uint8_t* hdr1 = p;
    const uint8_t* hdr2 = p + size1;
    const uint8_t* hdr3 = p + size1 + size2;

    VorbisDecoder* dec = (VorbisDecoder*)calloc(1, sizeof(VorbisDecoder));
    if (!dec) return NULL;

    if (!parse_identification(dec, hdr1, size1)) { vorbis_destroy(dec); return NULL; }
    /* Skip comment header (hdr2) */
    if (!parse_setup(dec, hdr3, size3)) { vorbis_destroy(dec); return NULL; }

    return dec;
}

int vorbis_get_channels(const VorbisDecoder* dec) {
    return dec ? dec->channels : 0;
}

int vorbis_get_sample_rate(const VorbisDecoder* dec) {
    return dec ? dec->sample_rate : 0;
}

void vorbis_destroy(VorbisDecoder* dec) {
    if (!dec) return;
    for (int i = 0; i < dec->codebook_count; i++) {
        free(dec->codebooks[i].lengths);
        free(dec->codebooks[i].sorted_codewords);
        free(dec->codebooks[i].sorted_values);
        free(dec->codebooks[i].vq_table);
    }
    for (int i = 0; i < dec->floor_count; i++) {
        free(dec->floors[i].sorted_order);
    }
    for (int ch = 0; ch < VB_MAX_CHANNELS; ch++) {
        free(dec->prev_block[ch]);
    }
    free(dec->pcm_buf);
    free(dec);
}
