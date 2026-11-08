// The C part of the goose-image package.
//
// It wraps stb_image, stb_image_write and stb_image_resize2. The
// headers are vendored in the vendor directory. The package needs no
// system library.
//
// The functions here take the types that the Goose compiler generates: a
// slice of bytes for the path, a builder for the result, and a pointer for
// each number that comes back. The definitions of the types are in
// goose.h, which the compiler puts on the include path.

#include "goose.h"

// The bytes slice. The compiler gives the layout { data, len } for a
// u8 slice (docs/goose_spec.md, section 7.10). goose.h does not declare
// it, so the package declares it here.
#ifndef IMG_SL_U8
#define IMG_SL_U8 1
typedef struct { const uint8_t *data; int64_t len; } img_slice;
#endif

// The WebP part sits in webp.c. That file does not include goose.h, so the
// types of the library and the types of the compiler stay apart. The
// buffers that it gives back are freed with free().
int webp_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch);
int webp_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                int32_t *w, int32_t *h);
int webp_encode(const uint8_t *pixels, int32_t w, int32_t h, int32_t quality,
                int32_t lossless, uint8_t **out, int64_t *out_len);
void webp_free(void *p);
static int is_webp(const uint8_t *b, int64_t n) {
    return n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4);
}

// The TIFF part sits in tiff.c. That file does not include goose.h either.
// It gives the pixels of the first page of a file and the bytes of a file
// of its own.
int tiff_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch);
int tiff_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                int32_t *w, int32_t *h);
int tiff_encode(const uint8_t *pixels, int32_t w, int32_t h, int32_t compression,
                uint8_t **out, int64_t *out_len);
int tiff_pages(const uint8_t *bytes, int64_t size);
static int is_tiff(const uint8_t *b, int64_t n) {
    // The head of a TIFF holds the order of the bytes and the number 42.
    return n >= 4 && (!memcmp(b, "II\x2a\x00", 4) || !memcmp(b, "MM\x00\x2a", 4));
}
// The AVIF decoder sits in avif.c, apart from goose.h for the same reason.
int avif_sniff(const uint8_t *bytes, int64_t size);
int avif_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch);
int avif_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                int32_t *w, int32_t *h);
int avif_decode16(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                  int32_t *w, int32_t *h);

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "vendor/stb_image_resize2.h"

// read_file_bytes reads the whole file into a buffer that the caller
// frees. It gives the size through the size pointer. It gives NULL when
// the file cannot be read. The path holds bytes, not a C string, so the
// package reads the file itself.
static uint8_t *read_file_bytes(img_slice path, int64_t *size) {
    // The path may hold any byte. A NUL byte ends the name for fopen.
    char name[4096];
    if (path.len <= 0 || path.len >= (int64_t)sizeof(name)) return NULL;
    memcpy(name, path.data, (size_t)path.len);
    name[path.len] = 0;
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 0x7fffffff) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    *size = n;
    return buf;
}

typedef struct { const int32_t *data; int64_t len; } img_islice;

// decode_core turns encoded bytes into RGBA pixels and appends them to out.
static uint8_t decode_core(const uint8_t *bytes, int64_t size, gs_rref out,
                           int32_t *w, int32_t *h) {
    if (size <= 0 || size > 0x7fffffff) return 0;
    if (is_webp(bytes, size)) {
        uint8_t *px = NULL;
        int64_t len = 0;
        if (!webp_decode(bytes, size, &px, &len, w, h)) return 0;
        gs_bld_append(out, px, len);
        webp_free(px);
        return 1;
    }
    if (is_tiff(bytes, size)) {
        uint8_t *px = NULL;
        int64_t len = 0;
        if (!tiff_decode(bytes, size, &px, &len, w, h)) return 0;
        gs_bld_append(out, px, len);
        free(px);
        return 1;
    }
    if (avif_sniff(bytes, size)) {
        uint8_t *px = NULL;
        int64_t len = 0;
        if (!avif_decode(bytes, size, &px, &len, w, h)) return 0;
        gs_bld_append(out, px, len);
        free(px);
        return 1;
    }
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(bytes, (int)size, &width, &height, &channels, 4);
    if (!pixels) return 0;
    gs_bld_append(out, pixels, (int64_t)width * height * 4);
    stbi_image_free(pixels);
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
}

// decode16 turns encoded bytes into RGBA pixels of 16 bits. The pixels
// go out as pairs of bytes, low byte first. WebP and the other 8-bit sources widen by
// the factor 257 inside stb_image.
static uint8_t decode16_core(const uint8_t *bytes, int64_t size, gs_rref out,
                             int32_t *w, int32_t *h) {
    if (size <= 0 || size > 0x7fffffff || is_webp(bytes, size)) return 0;
    if (avif_sniff(bytes, size)) {
        uint8_t *px = NULL;
        int64_t len = 0;
        if (!avif_decode16(bytes, size, &px, &len, w, h)) return 0;
        gs_bld_append(out, px, len);
        free(px);
        return 1;
    }
    int width = 0, height = 0, channels = 0;
    stbi_us *pixels = stbi_load_16_from_memory(bytes, (int)size, &width, &height, &channels, 4);
    if (!pixels) return 0;
    // Low byte first, so the Goose side needs no byte order test.
    int64_t n = (int64_t)width * height * 4;
    uint8_t *le = (uint8_t *)malloc((size_t)n * 2);
    if (!le) { stbi_image_free(pixels); return 0; }
    for (int64_t i = 0; i < n; i++) { le[2 * i] = (uint8_t)pixels[i]; le[2 * i + 1] = (uint8_t)(pixels[i] >> 8); }
    gs_bld_append(out, le, n * 2);
    free(le);
    stbi_image_free(pixels);
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
}

uint8_t img_decode16(img_slice data, gs_rref out, int32_t *w, int32_t *h) {
    return decode16_core(data.data, data.len, out, w, h);
}

uint8_t img_load16(img_slice path, gs_rref out, int32_t *w, int32_t *h) {
    int64_t size = 0;
    uint8_t *bytes = read_file_bytes(path, &size);
    if (!bytes) return 0;
    uint8_t ok = decode16_core(bytes, size, out, w, h);
    free(bytes);
    return ok;
}

// img_load reads the image at the path. Pixels are RGBA, row by row.
uint8_t img_load(img_slice path, gs_rref out, int32_t *w, int32_t *h) {
    int64_t size = 0;
    uint8_t *bytes = read_file_bytes(path, &size);
    if (!bytes) return 0;
    uint8_t ok = decode_core(bytes, size, out, w, h);
    free(bytes);
    return ok;
}

// img_decode decodes an image that sits in memory.
uint8_t img_decode(img_slice data, gs_rref out, int32_t *w, int32_t *h) {
    return decode_core(data.data, data.len, out, w, h);
}

// sniff names the format from the magic bytes. 0 is unknown.
static int32_t sniff(const uint8_t *b, int64_t n) {
    if (n >= 4 && !memcmp(b, "\x89PNG", 4)) return 1;
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xD8) return 2;
    if (n >= 2 && !memcmp(b, "BM", 2)) return 3;
    if (n >= 4 && !memcmp(b, "GIF8", 4)) return 4;
    if (is_webp(b, n)) return 10;
    if (is_tiff(b, n)) return 11;
    if (avif_sniff(b, n)) return 12;
    if (n >= 4 && !memcmp(b, "8BPS", 4)) return 6;
    if ((n >= 10 && !memcmp(b, "#?RADIANCE", 10)) || (n >= 6 && !memcmp(b, "#?RGBE", 6))) return 7;
    if (n >= 3 && b[0] == 'P' && b[1] >= '1' && b[1] <= '6' &&
        (b[2] == ' ' || b[2] == '\t' || b[2] == '\n' || b[2] == '\r')) return 8;
    if (n >= 4 && b[0] == 0x53 && b[1] == 0x80 && b[2] == 0xF6 && b[3] == 0x34) return 9;
    return 0;
}

// probe_core reads the header only. It never decodes the pixels.
static uint8_t probe_core(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h,
                          int32_t *ch, int32_t *kind) {
    if (size <= 0 || size > 0x7fffffff) return 0;
    if (is_webp(bytes, size)) {
        if (!webp_probe(bytes, size, w, h, ch)) return 0;
        *kind = 10;
        return 1;
    }
    if (is_tiff(bytes, size)) {
        if (!tiff_probe(bytes, size, w, h, ch)) return 0;
        *kind = 11;
        return 1;
    }
    if (avif_sniff(bytes, size)) {
        if (!avif_probe(bytes, size, w, h, ch)) return 0;
        *kind = 12;
        return 1;
    }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes, (int)size, &width, &height, &channels)) return 0;
    *w = width;
    *h = height;
    *ch = channels;
    *kind = sniff(bytes, size);
    if (!*kind) *kind = 5; // only TGA has no magic number
    return 1;
}

// img_probe_path gives size, channel count and format of the file.
uint8_t img_probe_path(img_slice path, int32_t *w, int32_t *h, int32_t *ch, int32_t *kind) {
    int64_t size = 0;
    uint8_t *bytes = read_file_bytes(path, &size);
    if (!bytes) return 0;
    uint8_t ok = probe_core(bytes, size, w, h, ch, kind);
    free(bytes);
    return ok;
}

// img_probe is the same for bytes in memory.
uint8_t img_probe(img_slice data, int32_t *w, int32_t *h, int32_t *ch, int32_t *kind) {
    return probe_core(data.data, data.len, w, h, ch, kind);
}

// The callback of the writers appends each chunk to the builder.
static void append_chunk(void *ctx, void *data, int size) {
    gs_bld_append(*(gs_rref *)ctx, data, size);
}

// img_encode encodes RGBA pixels into out. The format is 0 PNG, 1 JPEG,
// 2 BMP, 3 TGA, 4 HDR. The param is the JPEG quality (1 to 100), the PNG
// level (1 to 9) or the TGA RLE flag (1 on, 2 off). 0 means the default.
// The PNG level and the TGA flag go through stb globals. Goose threads
// share no globals, so this is safe.
uint8_t img_encode(img_slice pixels, gs_rref out, int32_t w, int32_t h, int32_t format, int32_t param) {
    if (w <= 0 || h <= 0 || pixels.len < (int64_t)w * h * 4) return 0;
    int ok = 0;
    switch (format) {
    case 0:
        stbi_write_png_compression_level = param > 0 ? (param > 9 ? 9 : param) : 8;
        ok = stbi_write_png_to_func(append_chunk, &out, w, h, 4, pixels.data, w * 4);
        break;
    case 1:
        ok = stbi_write_jpg_to_func(append_chunk, &out, w, h, 4, pixels.data,
                                    param > 0 ? (param > 100 ? 100 : param) : 90);
        break;
    case 2:
        ok = stbi_write_bmp_to_func(append_chunk, &out, w, h, 4, pixels.data);
        break;
    case 3:
        stbi_write_tga_with_rle = param != 2;
        ok = stbi_write_tga_to_func(append_chunk, &out, w, h, 4, pixels.data);
        break;
    case 4: {
        // HDR holds linear floats. stb_image reads HDR with a 1/2.2 gamma, so the
        // writer applies the 2.2 gamma and a round trip gives the same bytes.
        int64_t n = (int64_t)w * h;
        float *f = (float *)malloc((size_t)n * 3 * sizeof(float));
        if (!f) return 0;
        for (int64_t i = 0; i < n; i++)
            for (int c = 0; c < 3; c++) f[i * 3 + c] = powf(pixels.data[i * 4 + c] / 255.0f, 2.2f);
        ok = stbi_write_hdr_to_func(append_chunk, &out, w, h, 3, f);
        free(f);
        break;
    }
    }
    return ok ? 1 : 0;
}

// img_tiff_encode encodes RGBA pixels into out as a TIFF file. The
// compression is 1 for none, 5 for LZW and 8 for Deflate.
uint8_t img_tiff_encode(img_slice pixels, gs_rref out, int32_t w, int32_t h, int32_t compression) {
    if (w <= 0 || h <= 0 || pixels.len < (int64_t)w * h * 4) return 0;
    uint8_t *bytes = NULL;
    int64_t len = 0;
    if (!tiff_encode(pixels.data, w, h, compression, &bytes, &len)) return 0;
    if (bytes) {
        gs_bld_append(out, bytes, len);
        free(bytes);
    }
    return bytes != NULL;
}

// img_tiff_pages counts the pages of a TIFF file that sits in memory.
int32_t img_tiff_pages(img_slice data) {
    if (data.len <= 0) return 0;
    return (int32_t)tiff_pages(data.data, data.len);
}

// img_resize scales RGBA pixels. dims holds src w, src h, dst w, dst h
// and the filter number. It returns 0 for bad sizes or a bad filter.
uint8_t img_resize(img_slice src, gs_rref out, img_islice dims) {
    static const int filters[] = { 0, STBIR_FILTER_POINT_SAMPLE, STBIR_FILTER_BOX,
        STBIR_FILTER_TRIANGLE, STBIR_FILTER_CATMULLROM, STBIR_FILTER_MITCHELL,
        STBIR_FILTER_CUBICBSPLINE, STBIR_FILTER_LANCZOS4 };
    if (dims.len != 5) return 0;
    int sw = dims.data[0], sh = dims.data[1], dw = dims.data[2], dh = dims.data[3];
    int f = dims.data[4];
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || f < 0 || f > 7) return 0;
    if (src.len < (int64_t)sw * sh * 4) return 0;
    // Plain 8-bit values, edge clamp. Alpha is not premultiplied (RGBA layout).
    void *res = stbir_resize(src.data, sw, sh, sw * 4, NULL, dw, dh, dw * 4,
                             STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP,
                             (stbir_filter)filters[f]);
    if (!res) return 0;
    gs_bld_append(out, res, (int64_t)dw * dh * 4);
    free(res);
    return 1;
}

// ---- Metadata: the ICC profile and the EXIF block ----
//
// A JPEG carries the profile in one or more APP2 markers that hold
// ICC_PROFILE, a part number and a part count. It carries the EXIF in
// one APP1 marker that holds Exif, two zero bytes and the TIFF block.
// A PNG carries the profile in an iCCP chunk and the EXIF in an eXIf
// chunk.

// crc32 of the PNG chunks, bit by bit. The table is small enough to
// compute in place. The form with the running value continues over a
// second block.
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return crc;
}
static uint32_t crc32_of(const uint8_t *p, int64_t n) {
    return crc32_update(0xffffffffu, p, n) ^ 0xffffffffu;
}

static uint32_t adler32_of(const uint8_t *p, int64_t n) {
    uint32_t a = 1, b = 0;
    for (int64_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// big16 and big32 read the big endian numbers of PNG and TIFF.
static uint32_t big32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint32_t big16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

// put16 and put32 write big endian.
static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
// lit16 writes the little endian form that a stored block of DEFLATE
// uses.
static void lit16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// app2_of_one_part makes one APP2 marker of the profile. The part is
// one based, the count is the count of the parts.
static void app2_marker(gs_rref out, const uint8_t *part, int64_t n,
                        int part_no, int part_count) {
    uint8_t head[18];
    put16(head, 0xffe2);
    // The length counts the length bytes itself: the name, the two
    // numbers and the part.
    int64_t payload = 14 + n;
    put16(head + 2, (uint32_t)(payload + 2));
    memcpy(head + 4, "ICC_PROFILE\0", 12);
    head[16] = (uint8_t)part_no;
    head[17] = (uint8_t)part_count;
    gs_bld_append(out, head, 18);
    gs_bld_append(out, part, n);
}

// app1_of makes one APP1 marker of the EXIF. The block holds the TIFF
// bytes, without the Exif header.
static void app1_marker(gs_rref out, const uint8_t *block, int64_t n) {
    uint8_t head[14];
    put16(head, 0xffe1);
    int64_t payload = 6 + n; // Exif\0\0 + block
    put16(head + 2, (uint32_t)(payload + 2));
    memcpy(head + 4, "Exif\0\0", 6);
    gs_bld_append(out, head, 10);
    gs_bld_append(out, block, n);
}

// png_chunk makes one PNG chunk with its CRC.
static void png_chunk(gs_rref out, const char *kind, const uint8_t *body,
                      int64_t n) {
    uint8_t head[8];
    put32(head, (uint32_t)n);
    memcpy(head + 4, kind, 4);
    gs_bld_append(out, head, 8);
    gs_bld_append(out, body, n);
    uint8_t crc[4];
    uint32_t c = crc32_update(0xffffffffu, (const uint8_t *)kind, 4);
    c = crc32_update(c, body, n) ^ 0xffffffffu;
    put32(crc, c);
    gs_bld_append(out, crc, 4);
}

// img_encode16 writes RGBA pixels of 16 bits as a PNG of bit depth 16.
// Each row takes filter 0 and big endian samples. The pixels come in as u16 values.
uint8_t img_encode16(img_slice pixels, gs_rref out, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0 || w > 0x3fffffff / 8 || h > 0x3fffffff / 8) return 0;
    int64_t row = (int64_t)w * 8 + 1;
    if (row * h > 0x7fffffff || pixels.len < (int64_t)w * h * 4) return 0;
    const uint16_t *src = (const uint16_t *)pixels.data;
    uint8_t *raw = (uint8_t *)malloc((size_t)(row * h));
    if (!raw) return 0;
    for (int64_t y = 0; y < h; y++) {
        uint8_t *d = raw + y * row;
        *d++ = 0;
        for (int64_t i = 0; i < (int64_t)w * 4; i++) {
            uint16_t v = src[y * w * 4 + i];
            *d++ = (uint8_t)(v >> 8);
            *d++ = (uint8_t)v;
        }
    }
    int zlen = 0;
    uint8_t *z = stbi_zlib_compress(raw, (int)(row * h), &zlen, 8);
    free(raw);
    if (!z) return 0;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 16; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    gs_bld_append(out, sig, 8);
    png_chunk(out, "IHDR", ihdr, 13);
    png_chunk(out, "IDAT", z, zlen);
    png_chunk(out, "IEND", NULL, 0);
    STBIW_FREE(z);
    return 1;
}

// img_meta reads the ICC profile and the EXIF block of encoded bytes.
// The profile goes out joined. The EXIF goes out as the TIFF block,
// without the header of the marker. It gives 1 when the format is one
// of the two and the walk found no fault.
uint8_t img_meta(img_slice data, gs_rref icc, gs_rref exif) {
    if (data.len < 8) return 0;
    const uint8_t *b = data.data;
    if (b[0] == 0xff && b[1] == 0xd8) {
        // The walk of the markers of JPEG.
        int64_t at = 2;
        int parts_seen = 0, parts_want = 0;
        uint8_t *part[255];
        int64_t part_len[255];
        memset(part, 0, sizeof(part));
        memset(part_len, 0, sizeof(part_len));
        while (at + 4 <= data.len) {
            if (b[at] != 0xff) break;
            uint8_t mark = b[at + 1];
            if (mark == 0xd8 || (mark >= 0xd0 && mark <= 0xd9)) {
                at += 2;
                continue;
            }
            if (at + 4 > data.len) break;
            int64_t seg = big16(b + at + 2);
            if (at + 2 + seg > data.len) break;
            const uint8_t *body = b + at + 4;
            int64_t body_len = seg - 2;
            if (mark == 0xe2 && body_len > 14 &&
                memcmp(body, "ICC_PROFILE\0", 12) == 0) {
                int no = body[12];
                int count = body[13];
                if (no >= 1 && no <= 255 && count >= 1 && count <= 255) {
                    if (parts_want == 0) parts_want = count;
                    if (no >= 1 && no <= 255 && part[no - 1] == NULL) {
                        part[no - 1] = (uint8_t *)body + 14;
                        part_len[no - 1] = body_len - 14;
                        parts_seen++;
                    }
                }
            } else if (mark == 0xe1 && body_len > 8 &&
                       memcmp(body, "Exif\0\0", 6) == 0) {
                gs_bld_append(exif, body + 6, body_len - 6);
            }
            at += 2 + seg;
            if (mark == 0xda) break; // the scan starts
        }
        for (int i = 0; i < parts_want && i < 255; i++) {
            if (part[i]) gs_bld_append(icc, part[i], part_len[i]);
        }
        (void)parts_seen;
        return 1;
    }
    if (b[0] == 0x89 && memcmp(b + 1, "PNG\r\n\x1a\n", 7) == 0) {
        // The walk of the chunks of PNG.
        int64_t at = 8;
        while (at + 12 <= data.len) {
            uint32_t n = big32(b + at);
            if (at + 12 + (int64_t)n > data.len) break;
            const uint8_t *kind = b + at + 4;
            const uint8_t *body = b + at + 8;
            if (memcmp(kind, "iCCP", 4) == 0 && n > 2) {
                // The name, a zero, the method, the zlib stream.
                int64_t name_end = 0;
                while (name_end < n && body[name_end] != 0) name_end++;
                if (name_end + 2 < n) {
                    const uint8_t *z = body + name_end + 2;
                    int64_t zlen = n - name_end - 2;
                    int out_len = 0;
                    char *raw = stbi_zlib_decode_malloc((const char *)z,
                                                        (int)zlen, &out_len);
                    if (raw) {
                        gs_bld_append(icc, raw, out_len);
                        stbi_image_free(raw);
                    }
                }
            } else if (memcmp(kind, "eXIf", 4) == 0) {
                gs_bld_append(exif, body, n);
            }
            at += 12 + n;
            if (memcmp(kind, "IEND", 4) == 0) break;
        }
        return 1;
    }
    return 0;
}

// img_meta_encode puts the ICC profile and the EXIF block into an
// image that img_encode made. The base holds the bytes of the image.
// A JPEG takes APP1 before APP2, both after SOI. A PNG takes eXIf
// before iCCP, both after IHDR. The base goes out first in every
// case, and the metadata follows where it fits. It gives 0 when the
// base is not a PNG or a JPEG of this package.
uint8_t img_meta_encode(img_slice base, gs_rref out, img_slice icc,
                        img_slice exif) {
    if (base.len < 12) return 0;
    const uint8_t *b = base.data;
    if (b[0] == 0xff && b[1] == 0xd8) {
        gs_bld_append(out, b, 2);
        if (exif.len > 0 && exif.len <= 65527) app1_marker(out, exif.data, exif.len);
        if (icc.len > 0) {
            int64_t max_part = 65533 - 18;
            int64_t n = icc.len;
            int count = (int)((n + max_part - 1) / max_part);
            if (count > 255) return 0;
            for (int i = 0; i < count; i++) {
                int64_t at = (int64_t)i * max_part;
                int64_t take = n - at;
                if (take > max_part) take = max_part;
                app2_marker(out, icc.data + at, take, i + 1, count);
            }
        }
        gs_bld_append(out, b + 2, base.len - 2);
        return 1;
    }
    if (b[0] == 0x89 && memcmp(b + 1, "PNG\r\n\x1a\n", 7) == 0) {
        // The IHDR chunk is always the first. The metadata chunks go
        // behind it.
        uint32_t ihdr_len = big32(b + 8);
        if (memcmp(b + 12, "IHDR", 4) != 0) return 0;
        int64_t at = 16 + (int64_t)ihdr_len + 4; // behind the CRC
        if (at > base.len) return 0;
        gs_bld_append(out, b, at);
        if (exif.len > 0 && exif.len <= 0x7fffffff) {
            png_chunk(out, "eXIf", exif.data, exif.len);
        }
        if (icc.len > 0) {
            // The body of iCCP holds a name, a zero, the method and
            // the zlib stream. The stream holds stored blocks, so its
            // length is known before the write.
            static const char name[4] = { 'i', 'c', 'c', 0 };
            int64_t n = icc.len;
            int64_t parts = (n + 65534) / 65535;
            if (parts < 1) parts = 1;
            int64_t zlen = 2 + 4 + n + parts * 5;
            int64_t body_len = 5 + zlen;
            uint8_t head[8];
            put32(head, (uint32_t)body_len);
            memcpy(head + 4, "iCCP", 4);
            gs_bld_append(out, head, 8);
            uint32_t c = crc32_update(0xffffffffu, (const uint8_t *)"iCCP", 4);
            uint8_t nm[5];
            memcpy(nm, name, 4);
            nm[4] = 0; // the method of compression
            c = crc32_update(c, nm, 5);
            gs_bld_append(out, nm, 5);
            uint8_t zh[2] = { 0x78, 0x01 };
            c = crc32_update(c, zh, 2);
            gs_bld_append(out, zh, 2);
            int64_t off = 0;
            for (int64_t i = 0; i < parts; i++) {
                int64_t take = n - off;
                if (take > 65535) take = 65535;
                uint8_t bh[5];
                bh[0] = (off + take >= n) ? 1 : 0;
                lit16(bh + 1, (uint32_t)take);
                lit16(bh + 3, (uint32_t)(~(uint32_t)take & 0xffff));
                c = crc32_update(c, bh, 5);
                gs_bld_append(out, bh, 5);
                if (take > 0) {
                    c = crc32_update(c, icc.data + off, take);
                    gs_bld_append(out, icc.data + off, take);
                }
                off += take;
            }
            uint8_t tail[4];
            put32(tail, adler32_of(icc.data, n));
            c = crc32_update(c, tail, 4);
            gs_bld_append(out, tail, 4);
            uint8_t crcb[4];
            put32(crcb, c ^ 0xffffffffu);
            gs_bld_append(out, crcb, 4);
        }
        gs_bld_append(out, b + at, base.len - at);
        return 1;
    }
    return 0;
}

// img_encode_webp encodes RGBA pixels into out. The quality runs from 0 to
// 100. A lossless write keeps every pixel, also under a clear alpha.
uint8_t img_encode_webp(img_slice pixels, gs_rref out, int32_t w, int32_t h, int32_t quality, int32_t lossless) {
    if (w <= 0 || h <= 0 || pixels.len < (int64_t)w * h * 4) return 0;
    uint8_t *bytes = NULL;
    int64_t len = 0;
    if (!webp_encode(pixels.data, w, h, quality, lossless, &bytes, &len)) return 0;
    if (bytes) {
        gs_bld_append(out, bytes, len);
        free(bytes);
    }
    return bytes != NULL;
}
