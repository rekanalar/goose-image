// The WebP part of the goose-image package. It wraps the vendored libwebp.
//
// This file does not include goose.h on purpose. The types of libwebp and
// the types of the Goose compiler must not meet in one file: on Windows,
// libwebp declares its own whole-number types and the compiler's header
// brings the system ones. The two sets would collide.
//
// The functions here take plain C types. The caller frees the buffers that
// come back with free().

#include "src/webp/decode.h"
#include "src/webp/encode.h"

#include <stdlib.h>
#include <string.h>

// The largest side that the WebP format holds.
#define WEBP_MAX_SIDE 16383
// The largest count of bytes that a decode may take. A hostile file can
// claim a huge size in its header, so the limit keeps the memory in reach.
#define WEBP_MAX_BYTES (1 << 28)

// webp_probe reads the header only. channels is 4 with alpha, else 3.
int webp_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch) {
    if (size <= 0 || size > 0x7fffffff) return 0;
    WebPBitstreamFeatures f;
    if (WebPGetFeatures(bytes, (size_t)size, &f) != VP8_STATUS_OK) return 0;
    *w = (int32_t)f.width;
    *h = (int32_t)f.height;
    *ch = f.has_alpha ? 4 : 3;
    return 1;
}

// webp_free frees a buffer that webp_decode gave. The library may use its
// own allocator, so the buffer must go back through it.
void webp_free(void *p) {
    WebPFree(p);
}

// webp_decode gives RGBA pixels in a buffer that the caller frees with
// webp_free. The count of the bytes goes through out_len. An animated
// file fails here.
int webp_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                int32_t *w, int32_t *h) {
    int32_t ch = 0;
    if (!webp_probe(bytes, size, w, h, &ch)) return 0;
    if ((int64_t)*w * *h * 4 > WEBP_MAX_BYTES) return 0;
    int width = 0, height = 0;
    uint8_t *px = WebPDecodeRGBA(bytes, (size_t)size, &width, &height);
    if (!px) return 0;
    *out = px;
    *out_len = (int64_t)width * height * 4;
    *w = (int32_t)width;
    *h = (int32_t)height;
    return 1;
}

// webp_encode gives the WebP bytes of RGBA pixels in a buffer that the
// caller frees. quality runs from 0 to 100. A lossless write keeps every
// pixel, also under a clear alpha.
int webp_encode(const uint8_t *pixels, int32_t w, int32_t h, int32_t quality,
                int32_t lossless, uint8_t **out, int64_t *out_len) {
    if (w <= 0 || h <= 0 || w > WEBP_MAX_SIDE || h > WEBP_MAX_SIDE) return 0;
    WebPConfig cfg;
    if (!WebPConfigInit(&cfg)) return 0;
    cfg.quality = (float)(quality < 0 ? 0 : quality > 100 ? 100 : quality);
    cfg.lossless = lossless != 0;
    // A lossless write keeps the colors under a clear alpha.
    cfg.exact = lossless != 0;
    if (!WebPValidateConfig(&cfg)) return 0;
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return 0;
    pic.width = w;
    pic.height = h;
    // The encoder takes the pixels as they are. It turns them into the
    // colors of the image by itself, and it keeps the alpha.
    pic.use_argb = 1;
    WebPMemoryWriter mw;
    WebPMemoryWriterInit(&mw);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &mw;
    int ok = WebPPictureImportRGBA(&pic, pixels, w * 4) && WebPEncode(&cfg, &pic);
    *out = NULL;
    *out_len = 0;
    if (ok && mw.mem != NULL && mw.size > 0) {
        // The buffer of the writer goes to the caller, so it is copied
        // and the writer keeps nothing.
        uint8_t *copy = (uint8_t *)malloc(mw.size);
        if (copy) {
            memcpy(copy, mw.mem, mw.size);
            *out = copy;
            *out_len = (int64_t)mw.size;
        } else {
            ok = 0;
        }
    } else {
        ok = 0;
    }
    WebPPictureFree(&pic);
    WebPMemoryWriterClear(&mw);
    return ok ? 1 : 0;
}
