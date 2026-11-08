// The AVIF encode part of the goose-image package. It wraps the vendored
// libaom and writes the ISOBMFF container of a still AVIF file.
//
// This file does not include goose.h on purpose, like src/avif.c.
//
// The write covers still images only. The color goes out as 4:2:0, or as
// 4:4:4 with quality 100. An alpha channel rides along as a second item,
// the way Pillow writes it.

#include "aom/aom_encoder.h"
#include "aom/aomcx.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The largest side and the largest output. The limits keep a bad call
// inside a sane memory budget.
#define AVIFENC_MAX_SIDE 16384
#define AVIFENC_MAX_PIXELS (64 * 1024 * 1024)
#define AVIFENC_MAX_OUT (64 * 1024 * 1024)

// --- growable byte buffer ----------------------------------------------

typedef struct {
    uint8_t *data;
    int64_t len;
    int64_t cap;
} AvifBuf;

static int buf_push(AvifBuf *b, const void *bytes, int64_t n) {
    if (n < 0 || b->len + n > AVIFENC_MAX_OUT) return 0;
    if (b->len + n > b->cap) {
        int64_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + n) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(b->data, (size_t)cap);
        if (!grown) return 0;
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, bytes, (size_t)n);
    b->len += n;
    return 1;
}

static int buf_u8(AvifBuf *b, uint32_t v) {
    uint8_t x = (uint8_t)v;
    return buf_push(b, &x, 1);
}

static int buf_u16(AvifBuf *b, uint32_t v) {
    uint8_t x[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return buf_push(b, x, 2);
}

static int buf_u32(AvifBuf *b, uint32_t v) {
    uint8_t x[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return buf_push(b, x, 4);
}

static int buf_type(AvifBuf *b, const char *t) {
    return buf_push(b, t, 4);
}

// A box with a patched size. box_open writes a placeholder size, box_end
// patches it with the true size.
typedef struct {
    AvifBuf *b;
    int64_t size_at;
    int64_t start;
} BoxW;

static int box_open(BoxW *w, AvifBuf *b, const char *type) {
    w->b = b;
    w->size_at = b->len;
    w->start = b->len;
    if (!buf_u32(b, 0) || !buf_type(b, type)) return 0;
    return 1;
}

static int box_end(BoxW *w) {
    int64_t size = w->b->len - w->start;
    if (size < 8 || size > 0xffffffffLL) return 0;
    int64_t at = w->b->len;
    w->b->len = w->size_at;
    int ok = buf_u32(w->b, (uint32_t)size);
    w->b->len = at;
    return ok;
}

static int box_u32(AvifBuf *b, const char *type, uint32_t version_flags,
                   uint32_t a, uint32_t bb) {
    BoxW w;
    if (!box_open(&w, b, type) || !buf_u32(b, version_flags) ||
        !buf_u32(b, a) || !buf_u32(b, bb)) return 0;
    return box_end(&w);
}

// --- sequence header OBU parse ------------------------------------------

// A bit reader over one OBU payload.
typedef struct {
    const uint8_t *p;
    int64_t len;
    int64_t bit;
} BitRd;

static int br_bit(BitRd *r) {
    if (r->bit >= r->len * 8) return 0;
    int64_t byte = r->bit >> 3;
    int off = (int)(r->bit & 7);
    r->bit++;
    return (r->p[byte] >> (7 - off)) & 1;
}

static uint32_t br_bits(BitRd *r, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)br_bit(r);
    return v;
}

// The facts that the av1C box needs from the sequence header.
typedef struct {
    int profile;
    int level;
    int high_bitdepth;
    int twelve_bit;
    int monochrome;
    int sub_x;
    int sub_y;
    int chr_pos;
    int primaries;
    int transfer;
    int matrix;
    int full_range;
} SeqInfo;

// read_obu_seq_header parses the first sequence header OBU of the stream.
// It returns 1 on success. libaom writes a reduced still picture header
// here; a full header stream fails on purpose.
static int read_obu_seq_header(const uint8_t *p, int64_t len, SeqInfo *si) {
    memset(si, 0, sizeof(*si));
    int64_t at = 0;
    while (at + 2 <= len) {
        int type = (p[at] >> 3) & 0xF;
        int has_size = (p[at] >> 1) & 1;
        int64_t body = at + 1;
        int64_t body_len = 0;
        if (has_size) {
            int64_t v = 0;
            int shift = 0;
            for (int i = 0; i < 8 && body < len; i++) {
                uint8_t byte = p[body++];
                v |= (int64_t)(byte & 0x7F) << shift;
                shift += 7;
                if (!(byte & 0x80)) break;
            }
            body_len = v;
        } else {
            body_len = len - body;
        }
        if (body + body_len > len) return 0;
        if (type == 1) { // OBU_SEQUENCE_HEADER
            BitRd r;
            r.p = p + body;
            r.len = body_len;
            r.bit = 0;
            if (!br_bit(&r)) return 0; // marker
            si->profile = (int)br_bits(&r, 3);
            br_bit(&r); // still_picture
            int reduced = br_bit(&r);
            si->level = (int)br_bits(&r, 5);
            if (!reduced) return 0;
            br_bit(&r); // use_128x128_superblock
            br_bit(&r); // enable_filter_intra
            br_bit(&r); // enable_intra_edge_filter
            si->high_bitdepth = br_bit(&r);
            if (si->profile == 0 && si->high_bitdepth) si->twelve_bit = br_bit(&r);
            if (si->profile == 1) {
                si->monochrome = 0;
            } else {
                si->monochrome = br_bit(&r);
            }
            int desc = br_bit(&r);
            if (desc) {
                si->primaries = (int)br_bits(&r, 8);
                si->transfer = (int)br_bits(&r, 8);
                si->matrix = (int)br_bits(&r, 8);
            } else {
                si->primaries = 2;
                si->transfer = 2;
                si->matrix = 2;
            }
            si->full_range = br_bit(&r);
            if (si->monochrome || si->matrix == 0) {
                si->sub_x = si->monochrome ? 1 : 0;
                si->sub_y = si->monochrome ? 1 : 0;
            } else if (si->profile == 0) {
                si->sub_x = br_bit(&r);
                si->sub_y = br_bit(&r);
            }
            if (si->sub_x && si->sub_y) si->chr_pos = (int)br_bits(&r, 2);
            return 1;
        }
        at = body + body_len;
    }
    return 0;
}

// --- RGB to YUV ----------------------------------------------------------

// rgb8_to_yuv fills the planes in the 601 full range form. subsampling is
// 0 for 4:4:4 and 1 for 4:2:0.
static void rgb8_to_yuv(const uint8_t *px, int32_t w, int32_t h, int subsampling,
                        uint8_t *yuv[3], int64_t stride_y, int64_t stride_c) {
    for (int32_t y = 0; y < h; y++) {
        uint8_t *yrow = yuv[0] + y * stride_y;
        for (int32_t x = 0; x < w; x++) {
            const uint8_t *q = px + ((int64_t)y * w + x) * 4;
            int r = q[0], g = q[1], b = q[2];
            yrow[x] = (uint8_t)((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
        }
        if (subsampling == 0) {
            uint8_t *urow = yuv[1] + y * stride_c;
            uint8_t *vrow = yuv[2] + y * stride_c;
            for (int32_t x = 0; x < w; x++) {
                const uint8_t *q = px + ((int64_t)y * w + x) * 4;
                int r = q[0], g = q[1], b = q[2];
                urow[x] = (uint8_t)(((b - g) * 101589 >> 16) + 128);
                vrow[x] = (uint8_t)(((r - g) * 131473 >> 16) + 128);
            }
        } else if (y % 2 == 0) {
            uint8_t *urow = yuv[1] + (y >> 1) * stride_c;
            uint8_t *vrow = yuv[2] + (y >> 1) * stride_c;
            for (int32_t x = 0; x < w; x += 2) {
                int rs = 0, gs = 0, bs = 0, cnt = 0;
                for (int dy = 0; dy < 2 && y + dy < h; dy++) {
                    for (int dx = 0; dx < 2 && x + dx < w; dx++) {
                        const uint8_t *q = px + ((int64_t)(y + dy) * w + x + dx) * 4;
                        rs += q[0]; gs += q[1]; bs += q[2];
                        cnt++;
                    }
                }
                rs /= cnt; gs /= cnt; bs /= cnt;
                urow[x >> 1] = (uint8_t)(((bs - gs) * 101589 >> 16) + 128);
                vrow[x >> 1] = (uint8_t)(((rs - gs) * 131473 >> 16) + 128);
            }
        }
    }
}

// --- aom encode ----------------------------------------------------------

// encode_frame runs one aom encode of one frame and appends the OBU bytes
// to out. The planes come in row by row, sy and sc are the strides.
static int encode_frame(int32_t w, int32_t h, const uint8_t *y, const uint8_t *u,
                        const uint8_t *v, int64_t sy, int64_t sc, int profile,
                        int mono, int lossless, int cq, int speed, AvifBuf *out) {
    aom_codec_enc_cfg_t cfg;
    if (aom_codec_enc_config_default(aom_codec_av1_cx(), &cfg, 0) != AOM_CODEC_OK) return 0;
    cfg.g_w = (unsigned int)w;
    cfg.g_h = (unsigned int)h;
    cfg.g_limit = 1;
    cfg.g_threads = 1;
    cfg.g_lag_in_frames = 0;
    cfg.g_profile = (unsigned int)profile;
    cfg.g_bit_depth = AOM_BITS_8;
    cfg.g_input_bit_depth = 8;
    cfg.rc_end_usage = AOM_Q;
    cfg.rc_target_bitrate = 1000000;
    cfg.kf_min_dist = 0;
    cfg.kf_max_dist = 0;
    cfg.monochrome = (unsigned int)(mono ? 1 : 0);
    aom_codec_ctx_t codec;
    if (aom_codec_enc_init(&codec, aom_codec_av1_cx(), &cfg, 0) != AOM_CODEC_OK) return 0;
    int ok = 0;
    do {
        if (aom_codec_control(&codec, AOME_SET_CPUUSED, (unsigned int)speed) != AOM_CODEC_OK) break;
        if (aom_codec_control(&codec, AOME_SET_CQ_LEVEL, (unsigned int)cq) != AOM_CODEC_OK) break;
        if (lossless && aom_codec_control(&codec, AV1E_SET_LOSSLESS, 1u) != AOM_CODEC_OK) break;
        aom_image_t img;
        aom_img_fmt_t fmt = profile == 1 ? AOM_IMG_FMT_I444 : AOM_IMG_FMT_I420;
        if (!aom_img_alloc(&img, fmt, (unsigned int)w, (unsigned int)h, 32)) break;
        img.monochrome = mono ? 1 : 0;
        for (int32_t r = 0; r < h; r++) {
            memcpy(img.planes[0] + (int64_t)r * img.stride[0], y + r * sy, (size_t)w);
        }
        if (!mono) {
            int32_t ch = profile == 1 ? h : (h + 1) / 2;
            int32_t cw = profile == 1 ? w : (w + 1) / 2;
            for (int32_t r = 0; r < ch; r++) {
                memcpy(img.planes[1] + (int64_t)r * img.stride[1], u + r * sc, (size_t)cw);
                memcpy(img.planes[2] + (int64_t)r * img.stride[1], v + r * sc, (size_t)cw);
            }
        }
        if (aom_codec_encode(&codec, &img, 0, 1, AOM_EFLAG_FORCE_KF) != AOM_CODEC_OK) {
            aom_img_free(&img);
            break;
        }
        aom_img_free(&img);
        if (aom_codec_encode(&codec, NULL, 0, 1, 0) != AOM_CODEC_OK) break;
        aom_codec_iter_t iter = NULL;
        const aom_codec_cx_pkt_t *pkt;
        int got = 0;
        while ((pkt = aom_codec_get_cx_data(&codec, &iter)) != NULL) {
            if (pkt->kind == AOM_CODEC_CX_FRAME_PKT && pkt->data.frame.sz > 0) {
                if (!buf_push(out, pkt->data.frame.buf, (int64_t)pkt->data.frame.sz)) {
                    got = 0;
                    break;
                }
                got = 1;
            }
        }
        ok = got && out->len > 0;
    } while (0);
    aom_codec_destroy(&codec);
    return ok;
}

// --- container write -----------------------------------------------------

static void av1c_bytes(const SeqInfo *si, uint8_t out[4]) {
    out[0] = 0x81; // marker and version
    out[1] = (uint8_t)((si->profile << 5) | (si->level & 0x1F));
    out[2] = (uint8_t)(((si->profile >= 1 || si->high_bitdepth) ? 0x40 : 0) |
                       ((si->high_bitdepth && si->twelve_bit) ? 0x20 : 0) |
                       (si->monochrome ? 0x10 : 0) |
                       ((si->sub_x & 1) << 3) | ((si->sub_y & 1) << 2) |
                       (si->chr_pos & 3));
    out[3] = 0;
}

static int box_av1c(AvifBuf *b, const SeqInfo *si) {
    BoxW w;
    if (!box_open(&w, b, "av1C")) return 0;
    uint8_t c[4];
    av1c_bytes(si, c);
    if (!buf_push(b, c, 4)) return 0;
    return box_end(&w);
}

static int box_ispe(AvifBuf *b, int32_t w, int32_t h) {
    return box_u32(b, "ispe", 0, (uint32_t)w, (uint32_t)h);
}

static int box_pixi(AvifBuf *b, int channels) {
    BoxW w;
    if (!box_open(&w, b, "pixi") || !buf_u32(b, 0) || !buf_u8(b, (uint32_t)channels)) return 0;
    for (int i = 0; i < channels; i++) {
        if (!buf_u8(b, 8)) return 0;
    }
    return box_end(&w);
}

static int box_colr(AvifBuf *b, int primaries, int transfer, int matrix, int full) {
    BoxW w;
    if (!box_open(&w, b, "colr") || !buf_type(b, "nclx") ||
        !buf_u16(b, (uint32_t)primaries) || !buf_u16(b, (uint32_t)transfer) ||
        !buf_u16(b, (uint32_t)matrix) || !buf_u8(b, full ? 0x80 : 0)) return 0;
    return box_end(&w);
}

static int box_auxc(AvifBuf *b) {
    BoxW w;
    if (!box_open(&w, b, "auxC") || !buf_u32(b, 0)) return 0;
    const char *urn = "urn:mpeg:avif:aux:alpha";
    if (!buf_push(b, urn, 23) || !buf_u8(b, 0)) return 0;
    return box_end(&w);
}

// write_avif assembles the whole file into out. The OBUs of the color item
// sit in color_obus, the alpha OBUs in alpha_obus when alpha is set.
static int write_avif(AvifBuf *out, const uint8_t *color_obus, int64_t color_len,
                      const uint8_t *alpha_obus, int64_t alpha_len,
                      int32_t w, int32_t h) {
    int has_alpha = alpha_obus != NULL && alpha_len > 0;
    SeqInfo si, asi;
    if (!read_obu_seq_header(color_obus, color_len, &si)) return 0;
    if (has_alpha && !read_obu_seq_header(alpha_obus, alpha_len, &asi)) return 0;
    // ftyp
    BoxW w1;
    if (!box_open(&w1, out, "ftyp") || !buf_type(out, "avif") || !buf_u32(out, 0) ||
        !buf_type(out, "avif") || !buf_type(out, "mif1") || !buf_type(out, "miaf")) return 0;
    if (!box_end(&w1)) return 0;
    // The meta box. iloc comes last inside it, so the whole box is built
    // into a scratch buffer first. The offsets point into mdat, which sits
    // after meta, and the iloc length is fixed, so one pass with a second
    // patch of the offsets is enough.
    AvifBuf meta;
    memset(&meta, 0, sizeof(meta));
    BoxW wm;
    int ok = box_open(&wm, &meta, "meta") && buf_u32(&meta, 0); // version 0
    // hdlr
    if (ok) {
        BoxW wb;
        ok = box_open(&wb, &meta, "hdlr") && buf_u32(&meta, 0) &&
             buf_type(&meta, "pict") && buf_u32(&meta, 0) && buf_u32(&meta, 0) &&
             buf_u32(&meta, 0) && buf_u8(&meta, 0) && box_end(&wb);
    }
    // pitm
    if (ok) {
        BoxW wb;
        ok = box_open(&wb, &meta, "pitm") && buf_u32(&meta, 0) &&
             buf_u16(&meta, 1) && box_end(&wb);
    }
    // iloc with dummy offsets, patched after the length is known. The
    // size nibbles ride in two extra bytes, the way libavif writes them,
    // because a flags field with nibbles would break the libavif reader.
    int64_t iloc_at = meta.len;
    if (ok) {
        BoxW wb;
        ok = box_open(&wb, &meta, "iloc") && buf_u32(&meta, 0) &&
             buf_u8(&meta, 0x44) && buf_u8(&meta, 0) &&
             buf_u16(&meta, has_alpha ? 2 : 1);
        for (int i = 0; ok && i < (has_alpha ? 2 : 1); i++) {
            ok = buf_u16(&meta, (uint32_t)(i + 1)) && buf_u16(&meta, 0) &&
                 buf_u16(&meta, 1) && buf_u32(&meta, 0) && buf_u32(&meta, 0);
        }
        if (ok) ok = box_end(&wb);
    }
    // iinf
    if (ok) {
        BoxW wb, wi;
        ok = box_open(&wb, &meta, "iinf") && buf_u32(&meta, 0) &&
             buf_u16(&meta, has_alpha ? 2 : 1);
        for (int i = 0; ok && i < (has_alpha ? 2 : 1); i++) {
            ok = box_open(&wi, &meta, "infe") && buf_u32(&meta, 2u << 24) &&
                 buf_u16(&meta, (uint32_t)(i + 1)) && buf_u16(&meta, 0) &&
                 buf_type(&meta, "av01") && buf_u8(&meta, 0) && box_end(&wi);
        }
        if (ok) ok = box_end(&wb);
    }
    // iprp with ipco and ipma
    if (ok) {
        BoxW wp, wc, wa;
        ok = box_open(&wp, &meta, "iprp") && box_open(&wc, &meta, "ipco");
        if (ok) ok = box_av1c(&meta, &si) && box_ispe(&meta, w, h) &&
                     box_pixi(&meta, 3) && box_colr(&meta,
                        si.primaries == 2 ? 1 : si.primaries,
                        si.transfer == 2 ? 13 : si.transfer,
                        si.matrix == 2 ? 6 : si.matrix, si.full_range);
        if (ok && has_alpha) {
            ok = box_av1c(&meta, &asi) && box_ispe(&meta, w, h) &&
                 box_pixi(&meta, 1) && box_colr(&meta, 1, 13, 6, 1) &&
                 box_auxc(&meta);
        }
        if (ok) ok = box_end(&wc);
        if (ok) {
            ok = box_open(&wa, &meta, "ipma") && buf_u32(&meta, 0) &&
                 buf_u32(&meta, has_alpha ? 2 : 1) &&
                 buf_u16(&meta, 1) && buf_u8(&meta, 4) &&
                 buf_u8(&meta, 0x81) && buf_u8(&meta, 2) && buf_u8(&meta, 3) &&
                 buf_u8(&meta, 4);
            if (ok && has_alpha) {
                ok = buf_u16(&meta, 2) && buf_u8(&meta, 5) &&
                     buf_u8(&meta, 0x85) && buf_u8(&meta, 6) && buf_u8(&meta, 7) &&
                     buf_u8(&meta, 8) && buf_u8(&meta, 0x89);
            }
            if (ok) ok = box_end(&wa);
        }
        if (ok) ok = box_end(&wp);
    }
    if (ok) ok = box_end(&wm);
    if (!ok) {
        free(meta.data);
        return 0;
    }
    int64_t meta_len = meta.len;
    // Patch the iloc offsets. mdat starts after ftyp and meta.
    int64_t mdat_data = out->len + meta_len + 8;
    int64_t at = iloc_at + 8 + 4 + 2 + 2 + 2 + 2; // first offset field
    meta.len = at;
    ok = buf_u32(&meta, (uint32_t)mdat_data) && buf_u32(&meta, (uint32_t)color_len);
    if (ok && has_alpha) {
        meta.len = at + 14;
        ok = buf_u32(&meta, (uint32_t)(mdat_data + color_len)) &&
             buf_u32(&meta, (uint32_t)alpha_len);
    }
    meta.len = meta_len;
    if (!ok) {
        free(meta.data);
        return 0;
    }
    if (!buf_push(out, meta.data, meta_len)) {
        free(meta.data);
        return 0;
    }
    free(meta.data);
    // mdat
    BoxW wd;
    if (!box_open(&wd, out, "mdat")) return 0;
    if (!buf_push(out, color_obus, color_len)) return 0;
    if (has_alpha && !buf_push(out, alpha_obus, alpha_len)) return 0;
    return box_end(&wd);
}

// avif_encode encodes RGBA pixels into an AVIF file. quality runs from 0
// to 100, 100 means lossless. speed runs from 0 to 9. The caller frees
// the result.
int avif_encode(const uint8_t *pixels, int32_t w, int32_t h, int32_t quality,
                int32_t speed, uint8_t **out, int64_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (!pixels || w <= 0 || h <= 0 || w > AVIFENC_MAX_SIDE || h > AVIFENC_MAX_SIDE) return 0;
    if ((int64_t)w * h * 4 > AVIFENC_MAX_PIXELS) return 0;
    if (quality < 0) quality = 0;
    if (quality > 100) quality = 100;
    if (speed < 0) speed = 0;
    if (speed > 9) speed = 9;
    int lossless = quality >= 100;
    int profile = lossless ? 1 : 0;
    int has_alpha = 0;
    int64_t n = (int64_t)w * h;
    for (int64_t i = 0; i < n; i++) {
        if (pixels[i * 4 + 3] != 255) { has_alpha = 1; break; }
    }
    int64_t sy = w;
    int64_t sc = profile == 1 ? w : (w + 1) / 2;
    int64_t ch = profile == 1 ? h : (h + 1) / 2;
    uint8_t *yuv[3];
    yuv[0] = (uint8_t *)malloc((size_t)(sy * h));
    yuv[1] = (uint8_t *)malloc((size_t)(sc * ch));
    yuv[2] = (uint8_t *)malloc((size_t)(sc * ch));
    if (!yuv[0] || !yuv[1] || !yuv[2]) {
        free(yuv[0]); free(yuv[1]); free(yuv[2]);
        return 0;
    }
    rgb8_to_yuv(pixels, w, h, profile == 1 ? 0 : 1, yuv, sy, sc);
    int cq = lossless ? 0 : (int)((100.0 - quality) * 2.55 + 0.5);
    AvifBuf color;
    memset(&color, 0, sizeof(color));
    int ok = encode_frame(w, h, yuv[0], yuv[1], yuv[2], sy, sc, profile, 0,
                          lossless, cq, speed, &color);
    free(yuv[0]); free(yuv[1]); free(yuv[2]);
    AvifBuf alpha;
    memset(&alpha, 0, sizeof(alpha));
    if (ok && has_alpha) {
        uint8_t *ay = (uint8_t *)malloc((size_t)n);
        if (ay) {
            for (int64_t i = 0; i < n; i++) ay[i] = pixels[i * 4 + 3];
            ok = encode_frame(w, h, ay, NULL, NULL, w, 0, 0, 1, 0, cq, speed, &alpha);
            free(ay);
        } else {
            ok = 0;
        }
    }
    int done = 0;
    if (ok && color.len > 0) {
        int use_alpha = has_alpha && alpha.len > 0;
        AvifBuf file;
        memset(&file, 0, sizeof(file));
        if (write_avif(&file, color.data, color.len,
                       use_alpha ? alpha.data : NULL, use_alpha ? alpha.len : 0,
                       w, h)) {
            *out = file.data;
            *out_len = file.len;
            done = 1;
        } else {
            free(file.data);
        }
    }
    free(color.data);
    free(alpha.data);
    return done;
}
