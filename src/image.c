// The C part of the goose-image package.
//
// It wraps stb_image for reading and stb_image_write for writing. Both
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

// img_load reads the image at the path and writes its pixels into out.
// The pixels are RGBA, four bytes each, in the order of the rows.
// The width and the height come back through the two pointers.
// It returns 1 on success and 0 on failure.
uint8_t img_load(img_slice path, gs_rref out, int32_t *w, int32_t *h) {
    int64_t size = 0;
    uint8_t *bytes = read_file_bytes(path, &size);
    if (!bytes) return 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(bytes, (int)size,
                                            &width, &height, &channels, 4);
    free(bytes);
    if (!pixels) return 0;
    gs_bld_append(out, pixels, (int64_t)width * height * 4);
    stbi_image_free(pixels);
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
}

// img_info gives the width and the height of the image at the path.
// It returns 1 on success and 0 on failure.
uint8_t img_info(img_slice path, int32_t *w, int32_t *h) {
    int64_t size = 0;
    uint8_t *bytes = read_file_bytes(path, &size);
    if (!bytes) return 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    int ok = stbi_info_from_memory(bytes, (int)size, &width, &height, &channels);
    free(bytes);
    if (!ok) return 0;
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
}

// img_write writes the pixels to the path. The pixels are RGBA, four
// bytes each. The format is 0 for PNG, 1 for JPEG and 2 for BMP.
// It returns 1 on success and 0 on failure. The path goes through a copy,
// because stb takes a C string and the path is bytes with a length.
uint8_t img_write(img_slice path, img_slice pixels, int32_t w, int32_t h, int32_t format) {
    char name[4096];
    if (path.len <= 0 || path.len >= (int64_t)sizeof(name)) return 0;
    memcpy(name, path.data, (size_t)path.len);
    name[path.len] = 0;
    int ok = 0;
    if (format == 1) {
        ok = stbi_write_jpg(name, w, h, 4, pixels.data, 90);
    } else if (format == 2) {
        ok = stbi_write_bmp(name, w, h, 4, pixels.data);
    } else {
        ok = stbi_write_png(name, w, h, 4, pixels.data, w * 4);
    }
    return ok ? 1 : 0;
}
