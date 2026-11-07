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
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(bytes, (int)size, &width, &height, &channels, 4);
    if (!pixels) return 0;
    gs_bld_append(out, pixels, (int64_t)width * height * 4);
    stbi_image_free(pixels);
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
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
