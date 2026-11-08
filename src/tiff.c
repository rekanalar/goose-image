// The TIFF codec of the goose-image package.
//
// The reader gives the first page of a file as RGBA pixels. It reads the
// little endian and the big endian form, the strips and the tiles, the
// gray, the RGB, the palette and the alpha forms, and the none, the
// PackBits, the LZW and the Deflate compressions.
// The writer gives a TIFF file of RGBA pixels. It writes the little endian
// form. It writes the file uncompressed, with LZW or with Deflate.
//
// This file does not include goose.h on purpose. The types of the compiler
// and the types of this file stay apart. The functions take plain C types.
// The caller frees the buffers that come back with free().

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// The largest count of pixels that a decode may give. A hostile file can
// claim a huge size, so the limit keeps the memory in reach.
#define TIFF_MAX_PIXELS ((int64_t)1 << 26)
// The largest count of directories that the walk of the pages may find.
#define TIFF_MAX_PAGES 4096
// The largest count of tags in one directory.
#define TIFF_MAX_TAGS 512
// The largest count of blocks (strips or tiles) in one page.
#define TIFF_MAX_BLOCKS 65536

// ---- The numbers of the file ----

typedef struct {
    const uint8_t *b;
    int64_t size;
    int be; // 1 for the big endian form
} tif;

typedef struct {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint8_t raw[4];
} tif_tag;

static uint32_t tif_u16(const tif *t, const uint8_t *p) {
    if (t->be) return ((uint32_t)p[0] << 8) | p[1];
    return ((uint32_t)p[1] << 8) | p[0];
}

static uint32_t tif_u32(const tif *t, const uint8_t *p) {
    if (t->be)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static void tif_p16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void tif_p32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// The size of one value of a type.
static int tif_type_size(int type) {
    switch (type) {
    case 1: case 2: case 6: case 7: return 1; // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8: return 2;                  // SHORT SSHORT
    case 4: case 9: case 11: return 4;         // LONG SLONG FLOAT
    case 5: case 10: case 12: return 8;        // RATIONAL SRATIONAL DOUBLE
    default: return 0;
    }
}

// The bytes of the value of a tag and the count of them. A small value sits
// in the tag itself. A big value sits elsewhere in the file.
static int tif_tag_data(const tif *t, const tif_tag *g, const uint8_t **data, int64_t *count) {
    int ts = tif_type_size(g->type);
    if (!ts) return 0;
    int64_t n = (int64_t)ts * g->count;
    if (n < 0) return 0;
    if (n <= 4) {
        *data = g->raw;
        *count = g->count;
        return 1;
    }
    uint32_t off = tif_u32(t, g->raw);
    if ((int64_t)off + n > t->size) return 0;
    *data = t->b + off;
    *count = g->count;
    return 1;
}

// The first number of a tag, as an unsigned value.
static int tif_tag_uint(const tif *t, const tif_tag *tags, int ntags, uint16_t tag, uint32_t *out) {
    for (int i = 0; i < ntags; i++) {
        if (tags[i].tag != tag) continue;
        const uint8_t *d;
        int64_t c;
        if (!tif_tag_data(t, &tags[i], &d, &c)) return 0;
        if (c < 1) return 0;
        switch (tags[i].type) {
        case 1: case 6: case 7: *out = d[0]; return 1;
        case 3: case 8: *out = tif_u16(t, d); return 1;
        case 4: case 9: *out = tif_u32(t, d); return 1;
        default: return 0;
        }
    }
    return 0;
}

// The numbers of a tag. It gives the count of the numbers.
static int tif_tag_array(const tif *t, const tif_tag *tags, int ntags, uint16_t tag, uint32_t *out,
                         int max) {
    for (int i = 0; i < ntags; i++) {
        if (tags[i].tag != tag) continue;
        const uint8_t *d;
        int64_t c;
        if (!tif_tag_data(t, &tags[i], &d, &c)) return 0;
        if (c > max) return 0;
        for (int64_t k = 0; k < c; k++) {
            switch (tags[i].type) {
            case 1: case 6: case 7: out[k] = d[k]; break;
            case 3: case 8: out[k] = tif_u16(t, d + k * 2); break;
            case 4: case 9: out[k] = tif_u32(t, d + k * 4); break;
            default: return 0;
            }
        }
        return (int)c;
    }
    return 0;
}

// The count of the values of a tag, without the values.
static int tif_tag_count(const tif *t, const tif_tag *tags, int ntags, uint16_t tag) {
    for (int i = 0; i < ntags; i++) {
        if (tags[i].tag != tag) continue;
        const uint8_t *d;
        int64_t c;
        if (!tif_tag_data(t, &tags[i], &d, &c)) return 0;
        return (int)c;
    }
    return 0;
}

// The directory of one page. It gives the tags and the offset of the next
// directory.
static int tif_read_ifd(const tif *t, int64_t off, tif_tag *tags, int *ntags, int64_t *next) {
    if (off < 0 || off + 2 > t->size) return 0;
    uint32_t n = tif_u16(t, t->b + off);
    if (n > TIFF_MAX_TAGS) return 0;
    if (off + 2 + (int64_t)n * 12 + 4 > t->size) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *p = t->b + off + 2 + i * 12;
        tags[i].tag = (uint16_t)tif_u16(t, p);
        tags[i].type = (uint16_t)tif_u16(t, p + 2);
        tags[i].count = tif_u32(t, p + 4);
        memcpy(tags[i].raw, p + 8, 4);
    }
    *ntags = (int)n;
    *next = tif_u32(t, t->b + off + 2 + (int64_t)n * 12);
    return 1;
}

// ---- The compressions ----

// PackBits fills one row. The count of the bytes that it used goes out.
static int packbits_row(const uint8_t *src, int64_t n, uint8_t *dst, int64_t dn, int64_t *used) {
    int64_t i = 0, o = 0;
    while (o < dn) {
        if (i >= n) return 0;
        int c = (int)(int8_t)src[i++];
        if (c >= 0) {
            int cnt = c + 1;
            if (i + cnt > n || o + cnt > dn) return 0;
            memcpy(dst + o, src + i, (size_t)cnt);
            i += cnt;
            o += cnt;
        } else if (c != -128) {
            int cnt = 1 - c;
            if (i + 1 > n || o + cnt > dn) return 0;
            memset(dst + o, src[i], (size_t)cnt);
            i += 1; // the run holds one byte in the stream
            o += cnt;
        }
    }
    *used = i;
    return 1;
}

// ---- LZW, the form of TIFF ----
//
// The codes grow one code early. The rule of TIFF differs from the rule of
// GIF. The encoder switches when the next code reaches 511. The decoder
// runs one code behind the encoder, so it switches when its next code
// reaches 510.

#define LZW_CLEAR 256
#define LZW_EOI 257
#define LZW_FIRST 258

static int lzw_decode(const uint8_t *src, int64_t n, uint8_t *dst, int64_t dn) {
    int16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];
    int next = LZW_FIRST;
    int width = 9;
    int64_t bit = 0;
    int64_t out = 0;
    int oldcode = -1;
    uint8_t first = 0;
    int64_t total = n * 8;
    for (;;) {
        if (bit + width > total) break;
        uint32_t code = 0;
        for (int i = 0; i < width; i++) {
            // The codes of TIFF sit in the stream with the top bit first.
            int64_t b = bit + i;
            code = (code << 1) | (uint32_t)((src[b >> 3] >> (7 - (b & 7))) & 1);
        }
        bit += width;
        if (code == LZW_EOI) break;
        if (code == LZW_CLEAR) {
            next = LZW_FIRST;
            width = 9;
            oldcode = -1;
            continue;
        }
        int incode = (int)code;
        int walk = incode;
        int extra = 0;
        if (incode >= next) {
            if (oldcode < 0 || incode > next) return 0;
            walk = oldcode; // the code that the encoder did not send
            extra = 1;      // its first byte comes once more
        }
        int sp = 0;
        int c = walk;
        while (c >= LZW_FIRST) {
            if (sp >= 4096) return 0;
            stack[sp++] = suffix[c];
            c = prefix[c];
        }
        if (sp >= 4096) return 0;
        stack[sp++] = (uint8_t)c;
        first = (uint8_t)c;
        for (int i = sp - 1; i >= 0; i--) {
            if (out >= dn) return 1; // the row is full
            dst[out++] = stack[i];
        }
        if (extra) {
            if (out >= dn) return 1;
            dst[out++] = first;
        }
        if (oldcode >= 0 && next < 4096) {
            prefix[next] = (int16_t)oldcode;
            suffix[next] = first;
            next++;
        }
        oldcode = (int)code;
        if (width < 12 && next == (1 << width) - 1) width++;
    }
    return out == dn;
}

#define LZW_HSIZE 5003

// lzw_encode gives the LZW form of the bytes. It gives 0 when the buffer
// is too small.
static int lzw_encode(const uint8_t *src, int64_t n, uint8_t *dst, int64_t cap, int64_t *out_len) {
    int32_t bucket[LZW_HSIZE];
    int32_t chain[4096];
    int16_t eprefix[4096];
    uint8_t echar[4096];
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    int64_t o = 0;
    int next = LZW_FIRST;
    int width = 9;
    for (int i = 0; i < LZW_HSIZE; i++) bucket[i] = -1;

#define LZW_PUT(code, bits)                                                          \
    do {                                                                             \
        for (int i = (bits) - 1; i >= 0; i--) {                                      \
            bitbuf = (bitbuf << 1) | ((uint32_t)(code) >> i & 1);                     \
            bitcnt++;                                                                \
            if (bitcnt == 8) {                                                       \
                if (o >= cap) return 0;                                              \
                dst[o++] = (uint8_t)(bitbuf & 0xff);                                 \
                bitbuf = 0;                                                          \
                bitcnt = 0;                                                          \
            }                                                                        \
        }                                                                            \
    } while (0)

#define LZW_HASH(p, c) (((((p) << 8) ^ (c)) % LZW_HSIZE))

    LZW_PUT(LZW_CLEAR, 9);
    if (n == 0) {
        LZW_PUT(LZW_EOI, 9);
    } else {
        int omega = src[0];
        for (int64_t i = 1; i < n; i++) {
            int ch = src[i];
            int found = -1;
            for (int e = bucket[LZW_HASH(omega, ch)]; e >= 0; e = chain[e]) {
                if (eprefix[e] == omega && echar[e] == ch) {
                    found = e;
                    break;
                }
            }
            if (found >= 0) {
                omega = found;
                continue;
            }
            LZW_PUT(omega, width);
            if (next < 4096) {
                eprefix[next] = (int16_t)omega;
                echar[next] = (uint8_t)ch;
                int h = LZW_HASH(omega, ch);
                chain[next] = bucket[h];
                bucket[h] = next;
                next++;
                // The decoder runs one code behind, so the encoder switches
                // one code later than the decoder.
                if (width < 12 && next == (1 << width)) width++;
            } else {
                LZW_PUT(LZW_CLEAR, width);
                for (int i = 0; i < LZW_HSIZE; i++) bucket[i] = -1;
                next = LZW_FIRST;
                width = 9;
            }
            omega = ch;
        }
        LZW_PUT(omega, width);
        LZW_PUT(LZW_EOI, width);
    }
    if (bitcnt > 0) {
        if (o >= cap) return 0;
        dst[o++] = (uint8_t)((bitbuf << (8 - bitcnt)) & 0xff);
    }
#undef LZW_PUT
#undef LZW_HASH
    *out_len = o;
    return 1;
}

// ---- Deflate ----
//
// The reader is the algorithm of the book of the compression of data. The
// writer gives a block with the fixed codes and the matches of a small
// table.

typedef struct {
    const uint8_t *in;
    int64_t inlen;
    int64_t pos;
    uint32_t bitbuf;
    int bitcnt;
} inflater;

static int inf_bits(inflater *z, int need) {
    uint32_t val = z->bitbuf;
    while (z->bitcnt < need) {
        if (z->pos >= z->inlen) return -1;
        val |= (uint32_t)z->in[z->pos++] << z->bitcnt;
        z->bitcnt += 8;
    }
    z->bitbuf = val >> need;
    z->bitcnt -= need;
    if (need == 0) return 0;
    return (int)(val & (((uint32_t)1 << need) - 1));
}

typedef struct {
    int16_t count[16];
    int16_t symbol[288];
} huffman;

// huff_build makes the table of a code. It gives 0 when the code is whole,
// a positive number when the code lacks codes and a negative number when
// the code has too many.
static int huff_build(huffman *h, const int16_t *lengths, int n) {
    int offs[16];
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (int16_t)i;
    return left;
}

static int huff_decode(inflater *z, const huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        int b = inf_bits(z, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const int16_t inf_len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19, 23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int8_t inf_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int16_t inf_dist_base[30] = {1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
                                          33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
                                          1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int8_t inf_dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                          6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// inf_codes reads the symbols of one block of a deflate stream.
static int inf_codes(inflater *z, const huffman *lencode, const huffman *distcode, uint8_t *out,
                     int64_t *pos, int64_t cap) {
    for (;;) {
        int sym = huff_decode(z, lencode);
        if (sym < 0) return 0;
        if (sym < 256) {
            if (*pos >= cap) return 1;
            out[(*pos)++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) return 1;
        sym -= 257;
        if (sym >= 29) return 0;
        int extra = inf_bits(z, inf_len_extra[sym]);
        if (extra < 0) return 0;
        int length = inf_len_base[sym] + extra;
        int dsym = huff_decode(z, distcode);
        if (dsym < 0 || dsym >= 30) return 0;
        int dextra = inf_bits(z, inf_dist_extra[dsym]);
        if (dextra < 0) return 0;
        int64_t dist = inf_dist_base[dsym] + dextra;
        if (dist > *pos) return 0;
        if (*pos + length > cap) length = (int)(cap - *pos);
        for (int i = 0; i < length; i++) {
            out[*pos] = out[*pos - dist];
            (*pos)++;
        }
    }
}

// inflate_raw reads a deflate stream without the head of zlib.
static int inflate_raw(const uint8_t *in, int64_t inlen, uint8_t *out, int64_t cap, int64_t *outlen) {
    inflater z;
    z.in = in;
    z.inlen = inlen;
    z.pos = 0;
    z.bitbuf = 0;
    z.bitcnt = 0;
    int64_t pos = 0;
    int last, type;
    do {
        last = inf_bits(&z, 1);
        type = inf_bits(&z, 2);
        if (last < 0 || type < 0) return 0;
        if (type == 0) {
            int skip = z.bitcnt & 7;
            if (skip) inf_bits(&z, skip);
            int len = inf_bits(&z, 16);
            int nlen = inf_bits(&z, 16);
            if (len < 0 || nlen < 0) return 0;
            if ((len ^ 0xffff) != nlen) return 0;
            for (int i = 0; i < len; i++) {
                int b = inf_bits(&z, 8);
                if (b < 0) return 0;
                if (pos >= cap) {
                    *outlen = cap;
                    return 1;
                }
                out[pos++] = (uint8_t)b;
            }
        } else if (type == 1) {
            int16_t lengths[288];
            for (int i = 0; i < 144; i++) lengths[i] = 8;
            for (int i = 144; i < 256; i++) lengths[i] = 9;
            for (int i = 256; i < 280; i++) lengths[i] = 7;
            for (int i = 280; i < 288; i++) lengths[i] = 8;
            huffman lencode, distcode;
            huff_build(&lencode, lengths, 288);
            int16_t dl[30];
            for (int i = 0; i < 30; i++) dl[i] = 5;
            huff_build(&distcode, dl, 30);
            if (!inf_codes(&z, &lencode, &distcode, out, &pos, cap)) return 0;
        } else if (type == 2) {
            int nlen = inf_bits(&z, 5) + 257;
            int ndist = inf_bits(&z, 5) + 1;
            int ncode = inf_bits(&z, 4) + 4;
            if (nlen < 0 || ndist < 0 || ncode < 0) return 0;
            static const int8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int16_t lengths[320];
            memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < ncode; i++) {
                int v = inf_bits(&z, 3);
                if (v < 0) return 0;
                lengths[order[i]] = (int16_t)v;
            }
            for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
            huffman lencode;
            int err = huff_build(&lencode, lengths, 19);
            if (err != 0) return 0;
            int index = 0;
            while (index < nlen + ndist) {
                int sym = huff_decode(&z, &lencode);
                if (sym < 0) return 0;
                if (sym < 16) {
                    lengths[index++] = (int16_t)sym;
                    continue;
                }
                int len = 0, rep = 0;
                if (sym == 16) {
                    if (index == 0) return 0;
                    len = lengths[index - 1];
                    int e = inf_bits(&z, 2);
                    if (e < 0) return 0;
                    rep = 3 + e;
                } else if (sym == 17) {
                    int e = inf_bits(&z, 3);
                    if (e < 0) return 0;
                    rep = 3 + e;
                } else {
                    int e = inf_bits(&z, 7);
                    if (e < 0) return 0;
                    rep = 11 + e;
                }
                if (index + rep > nlen + ndist) return 0;
                while (rep--) lengths[index++] = (int16_t)len;
            }
            if (lengths[256] == 0) return 0;
            huffman litcode, distcode;
            err = huff_build(&litcode, lengths, nlen);
            if (err < 0) return 0;
            if (err > 0 && !(litcode.count[0] + litcode.count[1] == 1)) return 0;
            err = huff_build(&distcode, lengths + nlen, ndist);
            if (err < 0) return 0;
            if (err > 0 && !(distcode.count[0] + distcode.count[1] == 1)) return 0;
            if (!inf_codes(&z, &litcode, &distcode, out, &pos, cap)) return 0;
        } else {
            return 0;
        }
    } while (!last);
    *outlen = pos;
    return 1;
}

// inflate_data reads a deflate stream. The stream may hold the head of
// zlib.
static int inflate_data(const uint8_t *in, int64_t inlen, uint8_t *out, int64_t cap, int64_t *outlen) {
    int64_t start = 0;
    if (inlen >= 2) {
        uint32_t cmf = in[0], flg = in[1];
        if ((cmf & 0x0f) == 8 && (((cmf << 8) | flg) % 31) == 0) start = 2;
    }
    return inflate_raw(in + start, inlen - start, out, cap, outlen);
}

// The writer of deflate. It gives a block with the fixed codes.

typedef struct {
    uint8_t *b;
    int64_t cap;
    int64_t len;
    uint32_t buf;
    int cnt;
} bitout;

static int bo_bits(bitout *o, uint32_t val, int n) {
    o->buf |= val << o->cnt;
    o->cnt += n;
    while (o->cnt >= 8) {
        if (o->len >= o->cap) return 0;
        o->b[o->len++] = (uint8_t)(o->buf & 0xff);
        o->buf >>= 8;
        o->cnt -= 8;
    }
    return 1;
}

// bo_code writes a code of the fixed table. The bits of the code go from
// the top down.
static int bo_code(bitout *o, uint32_t code, int n) {
    for (int i = n - 1; i >= 0; i--)
        if (!bo_bits(o, (code >> i) & 1, 1)) return 0;
    return 1;
}

static void def_lit_code(int sym, uint32_t *code, int *n) {
    if (sym < 144) {
        *code = 0x30 + (uint32_t)sym;
        *n = 8;
    } else if (sym < 256) {
        *code = 0x190 + (uint32_t)(sym - 144);
        *n = 9;
    } else if (sym < 280) {
        *code = (uint32_t)(sym - 256);
        *n = 7;
    } else {
        *code = 0xc0 + (uint32_t)(sym - 280);
        *n = 8;
    }
}

#define DEF_HASH_BITS 15
#define DEF_HASH_SIZE (1 << DEF_HASH_BITS)
#define DEF_WINDOW 32768

static uint32_t def_hash(const uint8_t *p) {
    uint32_t h = (uint32_t)p[0] * 2654435761u + (uint32_t)p[1] * 2246822519u + (uint32_t)p[2] * 3266489917u;
    return (h >> (32 - DEF_HASH_BITS));
}

static int deflate_data(const uint8_t *src, int64_t n, uint8_t *out, int64_t cap, int64_t *outlen) {
    bitout o;
    o.b = out;
    o.cap = cap;
    o.len = 0;
    o.buf = 0;
    o.cnt = 0;
    int32_t *head = (int32_t *)malloc(sizeof(int32_t) * DEF_HASH_SIZE);
    int32_t *prev = (int32_t *)malloc(sizeof(int32_t) * DEF_WINDOW);
    if (!head || !prev) {
        free(head);
        free(prev);
        return 0;
    }
    for (int i = 0; i < DEF_HASH_SIZE; i++) head[i] = -1;
    for (int i = 0; i < DEF_WINDOW; i++) prev[i] = -1;
    int ok = 1;
    // The last block, with the fixed codes.
    if (!bo_bits(&o, 1, 1) || !bo_bits(&o, 1, 2)) ok = 0;
    int64_t i = 0;
    while (ok && i < n) {
        int best_len = 0, best_dist = 0;
        if (i + 3 <= n) {
            uint32_t h = def_hash(src + i);
            int32_t cand = head[h];
            int steps = 64;
            int64_t max = n - i;
            if (max > 258) max = 258;
            while (cand >= 0 && steps-- > 0) {
                int64_t d = i - cand;
                if (d > DEF_WINDOW) break;
                int64_t l = 0;
                while (l < max && src[cand + l] == src[i + l]) l++;
                if (l > best_len) {
                    best_len = (int)l;
                    best_dist = (int)d;
                    if (l >= max) break;
                }
                cand = prev[cand & (DEF_WINDOW - 1)];
            }
        }
        if (best_len >= 3) {
            // The match.
            int lc = 28;
            while (lc > 0 && inf_len_base[lc] > best_len) lc--;
            uint32_t code;
            int bits;
            def_lit_code(257 + lc, &code, &bits);
            if (!bo_code(&o, code, bits)) {
                ok = 0;
                break;
            }
            if (inf_len_extra[lc] && !bo_bits(&o, (uint32_t)(best_len - inf_len_base[lc]), inf_len_extra[lc])) {
                ok = 0;
                break;
            }
            int dc = 29;
            while (dc > 0 && inf_dist_base[dc] > best_dist) dc--;
            if (!bo_code(&o, (uint32_t)dc, 5)) {
                ok = 0;
                break;
            }
            if (inf_dist_extra[dc] && !bo_bits(&o, (uint32_t)(best_dist - inf_dist_base[dc]), inf_dist_extra[dc])) {
                ok = 0;
                break;
            }
            // The bytes of the match go into the table.
            for (int k = 0; k < best_len && i + k + 3 <= n; k++) {
                uint32_t h = def_hash(src + i + k);
                prev[(i + k) & (DEF_WINDOW - 1)] = head[h];
                head[h] = (int32_t)(i + k);
            }
            i += best_len;
        } else {
            uint32_t code;
            int bits;
            def_lit_code(src[i], &code, &bits);
            if (!bo_code(&o, code, bits)) {
                ok = 0;
                break;
            }
            if (i + 3 <= n) {
                uint32_t h = def_hash(src + i);
                prev[i & (DEF_WINDOW - 1)] = head[h];
                head[h] = (int32_t)i;
            }
            i++;
        }
    }
    if (ok) {
        uint32_t code;
        int bits;
        def_lit_code(256, &code, &bits);
        ok = bo_code(&o, code, bits);
    }
    if (ok && o.cnt > 0) {
        if (o.len >= o.cap) ok = 0;
        else o.b[o.len++] = (uint8_t)(o.buf & 0xff);
    }
    free(head);
    free(prev);
    if (!ok) return 0;
    *outlen = o.len;
    return 1;
}

static uint32_t adler32_of(const uint8_t *p, int64_t n) {
    uint32_t a = 1, b = 0;
    for (int64_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static int zlib_compress(const uint8_t *src, int64_t n, uint8_t *out, int64_t cap, int64_t *outlen) {
    if (cap < 16) return 0;
    out[0] = 0x78;
    out[1] = 0x01;
    int64_t len = 0;
    if (!deflate_data(src, n, out + 2, cap - 6, &len)) return 0;
    uint32_t a = adler32_of(src, n);
    // The check of zlib stands with the big bytes first.
    out[2 + len] = (uint8_t)(a >> 24);
    out[3 + len] = (uint8_t)(a >> 16);
    out[4 + len] = (uint8_t)(a >> 8);
    out[5 + len] = (uint8_t)a;
    *outlen = 2 + len + 4;
    return 1;
}

// ---- The reader ----

// The count of the bytes of one row of a block.
static int64_t tif_row_bytes(int64_t width, int spp, int bps) {
    return (width * spp * bps + 7) / 8;
}

// The value of one sample of a row.
static uint32_t tif_sample(const tif *t, const uint8_t *row, int64_t x, int k, int bps, int spp) {
    int64_t bitpos = (x * spp + k) * bps;
    if (bps == 8) return row[bitpos >> 3];
    if (bps == 16) return tif_u16(t, row + (bitpos >> 3));
    int64_t byte = bitpos >> 3;
    int shift = 8 - bps - (int)(bitpos & 7);
    uint32_t v = row[byte];
    if (shift < 0) {
        v = (v << 8) | row[byte + 1];
        shift += 8;
    }
    return (v >> shift) & (((uint32_t)1 << bps) - 1);
}

// The value of one sample as eight bits.
static uint8_t tif_scale8(uint32_t v, int bps) {
    switch (bps) {
    case 1: return v ? 255 : 0;
    case 2: return (uint8_t)(v * 85);
    case 4: return (uint8_t)(v * 17);
    case 8: return (uint8_t)v;
    case 16: return (uint8_t)(v >> 8);
    }
    return 0;
}

// The predictor 2 is the difference to the left. The undo adds the value of
// the pixel to the left back.
static void tif_undo_predictor(const tif *t, uint8_t *blk, int64_t rb, int rows, int bps, int spp) {
    if (bps == 8) {
        for (int r = 0; r < rows; r++) {
            uint8_t *row = blk + (int64_t)r * rb;
            for (int64_t j = spp; j < rb; j++) row[j] = (uint8_t)(row[j] + row[j - spp]);
        }
    } else if (bps == 16) {
        for (int r = 0; r < rows; r++) {
            uint8_t *row = blk + (int64_t)r * rb;
            int64_t n = rb / 2;
            for (int64_t j = spp; j < n; j++) {
                uint32_t v = tif_u16(t, row + 2 * j) + tif_u16(t, row + 2 * (j - spp));
                tif_p16(row + 2 * j, v & 0xffff);
            }
        }
    }
}

// tif_decode_block fills the buffer of a block. It gives 0 when the data
// does not fit.
static int tif_decode_block(const tif *t, int comp, int bps, int spp, int64_t width, int rows,
                             const uint8_t *src, int64_t src_len, uint8_t *dst, int64_t dst_len) {
    switch (comp) {
    case 1: // none
        if (src_len < dst_len) return 0;
        memcpy(dst, src, (size_t)dst_len);
        return 1;
    case 32773: { // PackBits
        // The rows of a strip may sit in one stream. A file that packs each
        // row on its own gives the same bytes, so one walk serves both.
        int64_t used = 0;
        return packbits_row(src, src_len, dst, dst_len, &used);
    }
    case 5: // LZW
        return lzw_decode(src, src_len, dst, dst_len);
    case 8:   // Deflate
    case 32946: { // Deflate, the old tag
        int64_t got = 0;
        if (!inflate_data(src, src_len, dst, dst_len, &got)) return 0;
        return got >= dst_len;
    }
    }
    return 0;
}

// The page of the file.
typedef struct {
    int32_t w, h;
    int spp, bps, photo, comp, pred;
    int64_t rps;
    int has_tiles;
    int32_t tw, tl;
    uint32_t *offs;
    uint32_t *cnts;
    int nblocks;
    uint16_t *cmap;
} tif_page;

// tif_read_page reads the tags of one page into the page.
static int tif_read_page(const tif *t, tif_tag *tags, int ntags, tif_page *pg) {
    uint32_t v;
    if (!tif_tag_uint(t, tags, ntags, 256, &v)) return 0;
    pg->w = (int32_t)v;
    if (!tif_tag_uint(t, tags, ntags, 257, &v)) return 0;
    pg->h = (int32_t)v;
    if (pg->w <= 0 || pg->h <= 0) return 0;
    if ((int64_t)pg->w * pg->h > TIFF_MAX_PIXELS) return 0;
    pg->spp = 1;
    if (tif_tag_uint(t, tags, ntags, 277, &v)) pg->spp = (int)v;
    if (pg->spp < 1 || pg->spp > 4) return 0;
    uint32_t bps[4] = {1, 1, 1, 1};
    int nb = tif_tag_array(t, tags, ntags, 258, bps, 4);
    pg->bps = nb > 0 ? (int)bps[0] : 1;
    if (nb > 1) {
        for (int i = 1; i < nb; i++)
            if (bps[i] != (uint32_t)pg->bps) return 0;
    }
    if (pg->bps != 1 && pg->bps != 2 && pg->bps != 4 && pg->bps != 8 && pg->bps != 16) return 0;
    pg->photo = pg->spp >= 3 ? 2 : 1;
    if (tif_tag_uint(t, tags, ntags, 262, &v)) pg->photo = (int)v;
    pg->comp = 1;
    if (tif_tag_uint(t, tags, ntags, 259, &v)) pg->comp = (int)v;
    pg->pred = 1;
    if (tif_tag_uint(t, tags, ntags, 317, &v)) pg->pred = (int)v;
    if (pg->pred != 1 && pg->pred != 2) return 0;
    uint32_t planar = 1;
    if (tif_tag_uint(t, tags, ntags, 284, &v)) planar = v;
    if (planar != 1) return 0; // only the form with the parts together
    pg->rps = pg->h;
    if (tif_tag_uint(t, tags, ntags, 278, &v) && v != 0xffffffffu) pg->rps = (int64_t)v;
    if (pg->rps <= 0) pg->rps = pg->h;
    pg->has_tiles = 0;
    pg->tw = pg->tl = 0;
    if (tif_tag_uint(t, tags, ntags, 322, &v) && v > 0) {
        pg->has_tiles = 1;
        pg->tw = (int32_t)v;
    }
    if (tif_tag_uint(t, tags, ntags, 323, &v) && v > 0) pg->tl = (int32_t)v;
    if (pg->has_tiles && (pg->tl <= 0 || pg->tw <= 0)) return 0;
    // A block of a hostile file must not ask for a huge buffer.
    if (pg->has_tiles && (int64_t)pg->tw * pg->tl > TIFF_MAX_PIXELS) return 0;
    uint16_t off_tag = pg->has_tiles ? 324 : 273;
    uint16_t cnt_tag = pg->has_tiles ? 325 : 279;
    int n = tif_tag_count(t, tags, ntags, off_tag);
    if (n <= 0 || n > TIFF_MAX_BLOCKS) return 0;
    pg->nblocks = n;
    pg->offs = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)n);
    pg->cnts = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)n);
    if (!pg->offs || !pg->cnts) return 0;
    if (tif_tag_array(t, tags, ntags, off_tag, pg->offs, n) != n) return 0;
    int nc = tif_tag_count(t, tags, ntags, cnt_tag);
    if (nc != n) return 0;
    if (tif_tag_array(t, tags, ntags, cnt_tag, pg->cnts, n) != n) return 0;
    // The table of the colors of a palette.
    pg->cmap = NULL;
    if (pg->photo == 3) {
        int entries = 1 << pg->bps;
        int total = entries * 3;
        uint32_t *cm = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)total);
        if (!cm) return 0;
        if (tif_tag_array(t, tags, ntags, 320, cm, total) != total) {
            free(cm);
            return 0;
        }
        pg->cmap = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)total);
        if (!pg->cmap) {
            free(cm);
            return 0;
        }
        for (int i = 0; i < total; i++) pg->cmap[i] = (uint16_t)cm[i];
        free(cm);
    }
    return 1;
}

// The extra samples tell how the alpha sits with the colors. The kind 1
// holds the alpha in the colors. The kind 2 holds the alpha on its own.
static int tif_extra_alpha(const tif *t, const tif_tag *tags, int ntags) {
    uint32_t v;
    if (!tif_tag_uint(t, tags, ntags, 338, &v)) return 0;
    return (int)v;
}

// tif_block_to_rgba puts the pixels of a block into the image.
static void tif_block_to_rgba(const tif *t, const tif_page *pg, const uint8_t *blk, int64_t rb,
                              int64_t width, int rows, int64_t x0, int64_t y0, int alpha_is_extra,
                              uint8_t *rgba, int32_t iw, int32_t ih) {
    for (int r = 0; r < rows; r++) {
        int64_t py = y0 + r;
        if (py < 0 || py >= ih) continue;
        const uint8_t *row = blk + (int64_t)r * rb;
        for (int64_t x = 0; x < width; x++) {
            int64_t px = x0 + x;
            if (px < 0 || px >= iw) continue;
            uint8_t *d = rgba + (py * iw + px) * 4;
            uint8_t R = 0, G = 0, B = 0, A = 255;
            if (pg->photo == 3) {
                uint32_t idx = tif_sample(t, row, x, 0, pg->bps, pg->spp);
                int entries = 1 << pg->bps;
                if ((int)idx >= entries) continue;
                R = (uint8_t)(pg->cmap[idx] >> 8);
                G = (uint8_t)(pg->cmap[entries + idx] >> 8);
                B = (uint8_t)(pg->cmap[2 * entries + idx] >> 8);
            } else if (pg->photo == 2) {
                R = tif_scale8(tif_sample(t, row, x, 0, pg->bps, pg->spp), pg->bps);
                G = tif_scale8(tif_sample(t, row, x, 1, pg->bps, pg->spp), pg->bps);
                B = tif_scale8(tif_sample(t, row, x, 2, pg->bps, pg->spp), pg->bps);
                if (pg->spp >= 4) A = tif_scale8(tif_sample(t, row, x, 3, pg->bps, pg->spp), pg->bps);
            } else {
                uint8_t v = tif_scale8(tif_sample(t, row, x, 0, pg->bps, pg->spp), pg->bps);
                if (pg->photo == 0) v = (uint8_t)(255 - v);
                R = G = B = v;
                if (pg->spp >= 2) A = tif_scale8(tif_sample(t, row, x, 1, pg->bps, pg->spp), pg->bps);
            }
            // The alpha that rides with the colors is in the colors. The
            // other form holds the alpha on its own.
            if (A != 255 && alpha_is_extra == 1 && A > 0 && pg->photo == 2) {
                uint32_t a = A;
                R = (uint8_t)(((uint32_t)R * 255) / a);
                G = (uint8_t)(((uint32_t)G * 255) / a);
                B = (uint8_t)(((uint32_t)B * 255) / a);
            }
            d[0] = R;
            d[1] = G;
            d[2] = B;
            d[3] = A;
        }
    }
}

// tiff_decode gives the pixels of the first page of a file.
int tiff_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len, int32_t *w,
                int32_t *h) {
    *out = NULL;
    *out_len = 0;
    if (size < 8 || size > 0x7fffffff) return 0;
    tif t;
    t.b = bytes;
    t.size = size;
    if (bytes[0] == 'I' && bytes[1] == 'I') t.be = 0;
    else if (bytes[0] == 'M' && bytes[1] == 'M') t.be = 1;
    else return 0;
    if (tif_u16(&t, bytes + 2) != 42) return 0;
    int64_t off = tif_u32(&t, bytes + 4);
    tif_tag tags[TIFF_MAX_TAGS];
    int ntags = 0;
    int64_t next = 0;
    if (!tif_read_ifd(&t, off, tags, &ntags, &next)) return 0;
    tif_page pg;
    memset(&pg, 0, sizeof(pg));
    int ok = 1;
    if (!tif_read_page(&t, tags, ntags, &pg)) { ok = 0; goto done; }
    int64_t np = (int64_t)pg.w * pg.h;
    // Every block must lie inside the file before the big buffer exists. A
    // header with a huge size and a bad place costs no memory this way.
    {
        int64_t need = pg.has_tiles
            ? (((int64_t)pg.w + pg.tw - 1) / pg.tw) * (((int64_t)pg.h + pg.tl - 1) / pg.tl)
            : ((int64_t)pg.h + pg.rps - 1) / pg.rps;
        if (need > pg.nblocks) { ok = 0; goto done; }
        for (int64_t i = 0; i < need; i++) {
            uint64_t o = pg.offs[i], c = pg.cnts[i];
            if (o > (uint64_t)size || c > (uint64_t)size || o + c > (uint64_t)size) { ok = 0; goto done; }
        }
    }
    uint8_t *rgba = (uint8_t *)calloc((size_t)(np * 4), 1);
    if (!rgba) { ok = 0; goto done; }
    int alpha_is_extra = tif_extra_alpha(&t, tags, ntags);
    // The buffer of one block.
    int64_t block_rows = pg.has_tiles ? pg.tl : pg.rps;
    if (block_rows > pg.h) block_rows = pg.h;
    int64_t block_width = pg.has_tiles ? pg.tw : pg.w;
    int64_t rb = tif_row_bytes(block_width, pg.spp, pg.bps);
    uint8_t *blk = (uint8_t *)malloc((size_t)(rb * block_rows + 16));
    if (!blk) {
        free(rgba);
        ok = 0;
        goto done;
    }
    if (pg.has_tiles) {
        int64_t across = ((int64_t)pg.w + pg.tw - 1) / pg.tw;
        int64_t down = ((int64_t)pg.h + pg.tl - 1) / pg.tl;
        for (int64_t ty = 0; ty < down && ok; ty++) {
            for (int64_t tx = 0; tx < across && ok; tx++) {
                int64_t idx = ty * across + tx;
                if (idx >= pg.nblocks) {
                    ok = 0;
                    break;
                }
                uint64_t o = pg.offs[idx];
                uint64_t c = pg.cnts[idx];
                if (o > (uint64_t)size || c > (uint64_t)size || o + c > (uint64_t)size) {
                    ok = 0;
                    break;
                }
                int64_t rows = pg.tl;
                int64_t y0 = ty * pg.tl;
                if (y0 + rows > pg.h) rows = pg.h - y0;
                int64_t want = rb * rows;
                if (!tif_decode_block(&t, pg.comp, pg.bps, pg.spp, pg.tw, (int)rows, bytes + o,
                                      (int64_t)c, blk, want)) {
                    ok = 0;
                    break;
                }
                if (pg.pred == 2) tif_undo_predictor(&t, blk, rb, (int)rows, pg.bps, pg.spp);
                tif_block_to_rgba(&t, &pg, blk, rb, pg.tw, (int)rows, tx * pg.tw, y0, alpha_is_extra,
                                  rgba, pg.w, pg.h);
            }
        }
    } else {
        int64_t nstrips = ((int64_t)pg.h + pg.rps - 1) / pg.rps;
        for (int64_t s = 0; s < nstrips && ok; s++) {
            if (s >= pg.nblocks) {
                ok = 0;
                break;
            }
            uint64_t o = pg.offs[s];
            uint64_t c = pg.cnts[s];
            if (o > (uint64_t)size || c > (uint64_t)size || o + c > (uint64_t)size) {
                ok = 0;
                break;
            }
            int64_t y0 = s * pg.rps;
            int64_t rows = pg.rps;
            if (y0 + rows > pg.h) rows = pg.h - y0;
            int64_t want = rb * rows;
            if (!tif_decode_block(&t, pg.comp, pg.bps, pg.spp, pg.w, (int)rows, bytes + o,
                                  (int64_t)c, blk, want)) {
                ok = 0;
                break;
            }
            if (pg.pred == 2) tif_undo_predictor(&t, blk, rb, (int)rows, pg.bps, pg.spp);
            tif_block_to_rgba(&t, &pg, blk, rb, pg.w, (int)rows, 0, y0, alpha_is_extra, rgba, pg.w,
                              pg.h);
        }
    }
    free(blk);
    if (!ok) {
        free(rgba);
        goto done;
    }
    *out = rgba;
    *out_len = np * 4;
    *w = pg.w;
    *h = pg.h;
    ok = 1;
done:
    free(pg.offs);
    free(pg.cnts);
    free(pg.cmap);
    return ok;
}

// tiff_probe reads the header of the first page of a file.
int tiff_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch) {
    if (size < 8 || size > 0x7fffffff) return 0;
    tif t;
    t.b = bytes;
    t.size = size;
    if (bytes[0] == 'I' && bytes[1] == 'I') t.be = 0;
    else if (bytes[0] == 'M' && bytes[1] == 'M') t.be = 1;
    else return 0;
    if (tif_u16(&t, bytes + 2) != 42) return 0;
    int64_t off = tif_u32(&t, bytes + 4);
    tif_tag tags[TIFF_MAX_TAGS];
    int ntags = 0;
    int64_t next = 0;
    if (!tif_read_ifd(&t, off, tags, &ntags, &next)) return 0;
    uint32_t v;
    if (!tif_tag_uint(&t, tags, ntags, 256, &v)) return 0;
    int32_t pw = (int32_t)v;
    if (!tif_tag_uint(&t, tags, ntags, 257, &v)) return 0;
    int32_t ph = (int32_t)v;
    if (pw <= 0 || ph <= 0) return 0;
    if ((int64_t)pw * ph > TIFF_MAX_PIXELS) return 0;
    int spp = 1;
    if (tif_tag_uint(&t, tags, ntags, 277, &v)) spp = (int)v;
    if (spp < 1 || spp > 4) spp = 1;
    *w = pw;
    *h = ph;
    *ch = spp;
    return 1;
}

// tiff_pages counts the pages of a file.
int tiff_pages(const uint8_t *bytes, int64_t size) {
    if (size < 8 || size > 0x7fffffff) return 0;
    tif t;
    t.b = bytes;
    t.size = size;
    if (bytes[0] == 'I' && bytes[1] == 'I') t.be = 0;
    else if (bytes[0] == 'M' && bytes[1] == 'M') t.be = 1;
    else return 0;
    if (tif_u16(&t, bytes + 2) != 42) return 0;
    int64_t *seen = (int64_t *)malloc(sizeof(int64_t) * TIFF_MAX_PAGES);
    if (!seen) return 0;
    int64_t off = tif_u32(&t, bytes + 4);
    int pages = 0;
    while (off != 0 && pages < TIFF_MAX_PAGES) {
        int loop = 0;
        for (int i = 0; i < pages; i++)
            if (seen[i] == off) loop = 1;
        if (loop) break;
        seen[pages] = off;
        tif_tag tags[TIFF_MAX_TAGS];
        int ntags = 0;
        int64_t next = 0;
        if (!tif_read_ifd(&t, off, tags, &ntags, &next)) break;
        pages++;
        off = next;
    }
    free(seen);
    return pages;
}

// ---- The writer ----

// The count of the tags of the file that the writer makes.
#define TIFF_WRITE_TAGS 11

// tiff_encode gives the bytes of a TIFF file of RGBA pixels. The
// compression is 1 for none, 5 for LZW and 8 for Deflate.
int tiff_encode(const uint8_t *pixels, int32_t w, int32_t h, int32_t compression, uint8_t **out,
                int64_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (w <= 0 || h <= 0) return 0;
    if (compression != 1 && compression != 5 && compression != 8) return 0;
    int64_t np = (int64_t)w * h;
    if (np > TIFF_MAX_PIXELS) return 0;
    int64_t raw_len = np * 4;
    uint8_t *data = NULL;
    int64_t data_len = 0;
    if (compression == 1) {
        data = (uint8_t *)pixels;
        data_len = raw_len;
    } else if (compression == 5) {
        data = (uint8_t *)malloc((size_t)(raw_len * 2 + 1024));
        if (!data) return 0;
        if (!lzw_encode(pixels, raw_len, data, raw_len * 2 + 1024, &data_len)) {
            free(data);
            return 0;
        }
    } else {
        data = (uint8_t *)malloc((size_t)(raw_len + raw_len / 8 + 1024));
        if (!data) return 0;
        if (!zlib_compress(pixels, raw_len, data, raw_len + raw_len / 8 + 1024, &data_len)) {
            free(data);
            return 0;
        }
    }
    // The layout: the head, the directory, the table of the bits and the
    // data of the strip.
    int64_t ifd_off = 8;
    int64_t ifd_size = 2 + TIFF_WRITE_TAGS * 12 + 4;
    int64_t bps_off = ifd_off + ifd_size;
    int64_t data_off = bps_off + 8;
    int64_t total = data_off + data_len;
    uint8_t *f = (uint8_t *)malloc((size_t)total);
    if (!f) {
        if (compression != 1) free(data);
        return 0;
    }
    f[0] = 'I';
    f[1] = 'I';
    f[2] = 42;
    f[3] = 0;
    tif_p32(f + 4, (uint32_t)ifd_off);
    tif_p16(f + 8, TIFF_WRITE_TAGS);
    uint8_t *p = f + 10;
#define TIFF_PUT_TAG(tag, type, count, value, is_offset)                                  \
    do {                                                                                  \
        tif_p16(p, (tag));                                                                \
        tif_p16(p + 2, (type));                                                           \
        tif_p32(p + 4, (count));                                                          \
        if (is_offset)                                                                    \
            tif_p32(p + 8, (value));                                                      \
        else if ((type) == 3) {                                                           \
            tif_p16(p + 8, (value));                                                      \
            tif_p16(p + 10, 0);                                                           \
        } else {                                                                          \
            tif_p32(p + 8, (value));                                                      \
        }                                                                                 \
        p += 12;                                                                          \
    } while (0)
    TIFF_PUT_TAG(256, 4, 1, (uint32_t)w, 0);         // the width
    TIFF_PUT_TAG(257, 4, 1, (uint32_t)h, 0);         // the height
    TIFF_PUT_TAG(258, 3, 4, (uint32_t)bps_off, 1);   // the bits of the samples
    TIFF_PUT_TAG(259, 3, 1, (uint32_t)compression, 0); // the compression
    TIFF_PUT_TAG(262, 3, 1, 2, 0);                   // the colors are red, green and blue
    TIFF_PUT_TAG(273, 4, 1, (uint32_t)data_off, 0);  // the place of the strip
    TIFF_PUT_TAG(277, 3, 1, 4, 0);                   // the count of the samples
    TIFF_PUT_TAG(278, 4, 1, (uint32_t)h, 0);         // the rows of the strip
    TIFF_PUT_TAG(279, 4, 1, (uint32_t)data_len, 0);  // the size of the strip
    TIFF_PUT_TAG(284, 3, 1, 1, 0);                   // the parts stay together
    TIFF_PUT_TAG(338, 3, 1, 2, 0);                   // the alpha is on its own
#undef TIFF_PUT_TAG
    tif_p32(p, 0); // no next page
    for (int i = 0; i < 4; i++) tif_p16(f + bps_off + i * 2, 8);
    memcpy(f + data_off, data, (size_t)data_len);
    if (compression != 1) free(data);
    *out = f;
    *out_len = total;
    return 1;
}
