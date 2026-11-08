// The GIF codec of the goose-image package.
//
// The reader gives every frame of a GIF as a full canvas of RGBA pixels.
// The writer takes frames of RGBA pixels and writes a GIF89a file.
// The byte layout follows Go image/gif.
//
// Each call takes few arguments. Big data goes in flat buffers.

#include "goose.h"
#include <stdlib.h>
#include <string.h>

#ifndef IMG_SL_U8
#define IMG_SL_U8 1
typedef struct { const uint8_t *data; int64_t len; } img_slice;
#endif
typedef struct { const int32_t *data; int64_t len; } gif_islice;

// The most bytes that the canvases of one file may hold.
#define GIF_MAX_OUT ((int64_t)1 << 26)

// ---- Reader ----

// lzw_decode fills dst with dn color indexes. It follows the decoder of
// Go compress/lzw in LSB order. It gives 0 when the data ends before the
// pixels are complete.
static int lzw_decode(const uint8_t *src, int64_t sn, int mcs, uint8_t *dst, int64_t dn) {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];
    int clear = 1 << mcs, eof = clear + 1;
    int width = mcs + 1, hi = eof, overflow = 1 << width, last = -1;
    uint32_t bits = 0;
    int nbits = 0;
    int64_t sp = 0, o = 0;
    while (o < dn) {
        while (nbits < width) {
            if (sp >= sn) return 0;
            bits |= (uint32_t)src[sp++] << nbits;
            nbits += 8;
        }
        int code = (int)(bits & ((1u << width) - 1));
        bits >>= width;
        nbits -= width;
        if (code < clear) {
            // A literal.
            dst[o++] = (uint8_t)code;
            if (last >= 0) { suffix[hi] = (uint8_t)code; prefix[hi] = (uint16_t)last; }
        } else if (code == clear) {
            width = mcs + 1;
            hi = eof;
            overflow = 1 << width;
            last = -1;
            continue;
        } else if (code == eof) {
            return 0;
        } else if (code <= hi) {
            int c = code, n = 0;
            if (code == hi && last >= 0) {
                // The code is not in the table. It is the last code plus
                // its first byte.
                c = last;
                while (c >= clear) c = prefix[c];
                stack[n++] = (uint8_t)c;
                c = last;
            }
            while (c >= clear) { stack[n++] = suffix[c]; c = prefix[c]; }
            stack[n++] = (uint8_t)c;
            // The head is the first byte of the expansion.
            uint8_t head = stack[n - 1];
            while (n > 0 && o < dn) dst[o++] = stack[--n];
            if (last >= 0) { suffix[hi] = head; prefix[hi] = (uint16_t)last; }
        } else {
            return 0;
        }
        last = code;
        hi++;
        if (hi >= overflow) {
            if (width == 12) { last = -1; hi--; }
            else { width++; overflow = 1 << width; }
        }
    }
    return 1;
}

// skip_blocks moves p past a run of sub-blocks. It gives 0 on truncation.
static int skip_blocks(const uint8_t *b, int64_t n, int64_t *p) {
    for (;;) {
        if (*p >= n) return 0;
        int len = b[(*p)++];
        if (len == 0) return 1;
        if (*p + len > n) return 0;
        *p += len;
    }
}

static int u16at(const uint8_t *b, int64_t p) { return b[p] | (b[p + 1] << 8); }

// A palette of the file.
typedef struct { uint8_t rgb[768]; int n; } gpal;

// gif_decode_core is the reader. It appends one canvas a frame to out.
// meta holds numbers of four bytes each, little endian: the width, the
// height, the loop count, and then the delay and the disposal method of
// every frame.
static uint8_t gif_decode_core(const uint8_t *b, int64_t n, gs_rref out, gs_rref meta) {
    if (n < 13 || (memcmp(b, "GIF87a", 6) && memcmp(b, "GIF89a", 6))) return 0;
    int W = u16at(b, 6), H = u16at(b, 8);
    int packed = b[10];
    int64_t canvas_bytes = (int64_t)W * H * 4;
    if (W == 0 || H == 0 || canvas_bytes > GIF_MAX_OUT) return 0;
    int64_t p = 13;
    gpal g;
    g.n = 0;
    if (packed & 0x80) {
        g.n = 1 << ((packed & 7) + 1);
        if (p + (int64_t)g.n * 3 > n) return 0;
        memcpy(g.rgb, b + p, (size_t)g.n * 3);
        p += (int64_t)g.n * 3;
    }
    uint8_t *canvas = (uint8_t *)calloc((size_t)canvas_bytes, 1);
    uint8_t *snap = NULL;
    uint8_t *idx = NULL;
    uint8_t *data = NULL;
    uint8_t *fmeta = NULL;
    int64_t nframes = 0, cap = 0, total = 0;
    int32_t loop = -1;
    int delay = 0, disp = 0, tidx = -1;
    int pdisp = 0, px = 0, py = 0, pw = 0, ph = 0;
    uint8_t ok = 0;
    if (!canvas) goto done;
    for (;;) {
        if (p >= n) { ok = nframes > 0; break; }
        int t = b[p++];
        if (t == 0x3B) { ok = nframes > 0; break; }
        if (t == 0x21) {
            if (p >= n) break;
            int label = b[p++];
            if (label == 0xF9) {
                // Graphic control extension: size, flags, delay, index.
                if (p + 6 > n || b[p] != 4) break;
                int fl = b[p + 1];
                disp = (fl >> 2) & 7;
                delay = u16at(b, p + 2);
                tidx = (fl & 1) ? b[p + 4] : -1;
                p += 5;
                if (!skip_blocks(b, n, &p)) break;
            } else if (label == 0xFF) {
                if (p >= n) break;
                int len = b[p];
                if (len == 11 && p + 17 <= n &&
                    (!memcmp(b + p + 1, "NETSCAPE2.0", 11) || !memcmp(b + p + 1, "ANIMEXTS1.0", 11)) &&
                    b[p + 12] == 3 && b[p + 13] == 1)
                    loop = u16at(b, p + 14);
                if (!skip_blocks(b, n, &p)) break;
            } else if (!skip_blocks(b, n, &p)) break;
            continue;
        }
        if (t != 0x2C || p + 9 > n) break;
        int fx = u16at(b, p), fy = u16at(b, p + 2), fw = u16at(b, p + 4), fh = u16at(b, p + 6);
        int fp = b[p + 8];
        p += 9;
        if (fw == 0 || fh == 0 || fx + fw > W || fy + fh > H) break;
        const uint8_t *pal = g.rgb;
        int pal_n = g.n;
        uint8_t lct[768];
        if (fp & 0x80) {
            pal_n = 1 << ((fp & 7) + 1);
            if (p + (int64_t)pal_n * 3 > n) break;
            memcpy(lct, b + p, (size_t)pal_n * 3);
            pal = lct;
            p += (int64_t)pal_n * 3;
        }
        if (pal_n == 0 || p >= n) break;
        int mcs = b[p++];
        if (mcs < 2 || mcs > 8) break;
        // Gather the sub-blocks of the image data.
        free(data);
        data = (uint8_t *)malloc((size_t)(n - p) + 1);
        if (!data) break;
        int64_t dn = 0;
        int ended = 0;
        while (p < n) {
            int len = b[p++];
            if (len == 0) { ended = 1; break; }
            if (p + len > n) break;
            memcpy(data + dn, b + p, (size_t)len);
            dn += len;
            p += len;
        }
        if (!ended) break;
        int64_t fn = (int64_t)fw * fh;
        free(idx);
        idx = (uint8_t *)malloc((size_t)fn);
        if (!idx || !lzw_decode(data, dn, mcs, idx, fn)) break;
        // The disposal of the frame before this one.
        if (nframes > 0) {
            if (pdisp == 2) {
                for (int y = py; y < py + ph; y++)
                    memset(canvas + ((int64_t)y * W + px) * 4, 0, (size_t)pw * 4);
            } else if (pdisp == 3 && snap) {
                memcpy(canvas, snap, (size_t)canvas_bytes);
            }
        }
        if (disp == 3) {
            if (!snap) snap = (uint8_t *)malloc((size_t)canvas_bytes);
            if (!snap) break;
            memcpy(snap, canvas, (size_t)canvas_bytes);
        }
        int bad = 0;
        for (int64_t i = 0; i < fn; i++) {
            int c = idx[i];
            if (c >= pal_n) { bad = 1; break; }
            if (c == tidx) continue;
            int64_t row = i / fw;
            if (fp & 0x40) {
                // The rows of an interlaced image come in four passes.
                int64_t q = (fh + 7) / 8, r2 = (fh + 3) / 8, r3 = (fh + 1) / 4;
                if (row < q) row = row * 8;
                else if (row < q + r2) row = (row - q) * 8 + 4;
                else if (row < q + r2 + r3) row = (row - q - r2) * 4 + 2;
                else row = (row - q - r2 - r3) * 2 + 1;
            }
            uint8_t *d = canvas + (((int64_t)(fy + row)) * W + fx + i % fw) * 4;
            d[0] = pal[c * 3]; d[1] = pal[c * 3 + 1]; d[2] = pal[c * 3 + 2]; d[3] = 255;
        }
        if (bad) break;
        total += canvas_bytes;
        if (total > GIF_MAX_OUT) break;
        gs_bld_append(out, canvas, canvas_bytes);
        if (nframes == cap) {
            cap = cap ? cap * 2 : 16;
            uint8_t *m = (uint8_t *)realloc(fmeta, (size_t)cap * 8);
            if (!m) break;
            fmeta = m;
        }
        fmeta[nframes * 8] = (uint8_t)delay;
        fmeta[nframes * 8 + 1] = (uint8_t)(delay >> 8);
        fmeta[nframes * 8 + 2] = (uint8_t)(delay >> 8 >> 8);
        fmeta[nframes * 8 + 3] = (uint8_t)(delay >> 24);
        fmeta[nframes * 8 + 4] = (uint8_t)disp;
        fmeta[nframes * 8 + 5] = (uint8_t)(disp >> 8);
        fmeta[nframes * 8 + 6] = (uint8_t)(disp >> 16);
        fmeta[nframes * 8 + 7] = (uint8_t)(disp >> 24);
        nframes++;
        pdisp = disp; px = fx; py = fy; pw = fw; ph = fh;
        delay = 0; disp = 0; tidx = -1;
    }
    if (ok) {
        uint8_t hb[12];
        int32_t head[3] = { W, H, loop };
        for (int i = 0; i < 3; i++)
            for (int k = 0; k < 4; k++) hb[i * 4 + k] = (uint8_t)((uint32_t)head[i] >> (8 * k));
        gs_bld_append(meta, hb, 12);
        gs_bld_append(meta, fmeta, nframes * 8);
    }
done:
    free(canvas); free(snap); free(idx); free(data); free(fmeta);
    return ok;
}

// img_gif_decode reads every frame of a GIF in memory.
uint8_t img_gif_decode(img_slice data, gs_rref out, gs_rref meta) {
    return gif_decode_core(data.data, data.len, out, meta);
}

// ---- Writer ----

typedef struct { uint8_t *p; int64_t n, cap; int err; } bw;

static void bw_byte(bw *w, int c) {
    if (w->err) return;
    if (w->n == w->cap) {
        int64_t cap = w->cap ? w->cap * 2 : 4096;
        uint8_t *q = (uint8_t *)realloc(w->p, (size_t)cap);
        if (!q) { w->err = 1; return; }
        w->p = q;
        w->cap = cap;
    }
    w->p[w->n++] = (uint8_t)c;
}
static void bw_u16(bw *w, int v) { bw_byte(w, v & 255); bw_byte(w, (v >> 8) & 255); }

// The LZW writer packs the codes into sub-blocks of at most 255 bytes.
typedef struct { bw *w; uint8_t blk[255]; int blen; uint32_t bits; int nbits; } lzw_out;

static void lz_flush_block(lzw_out *o) {
    if (!o->blen) return;
    bw_byte(o->w, o->blen);
    for (int i = 0; i < o->blen; i++) bw_byte(o->w, o->blk[i]);
    o->blen = 0;
}
static void lz_put(lzw_out *o, int code, int width) {
    o->bits |= (uint32_t)code << o->nbits;
    o->nbits += width;
    while (o->nbits >= 8) {
        o->blk[o->blen++] = (uint8_t)o->bits;
        o->bits >>= 8;
        o->nbits -= 8;
        if (o->blen == 255) lz_flush_block(o);
    }
}

// The table of the writer maps a pair of a prefix and a byte to a code.
typedef struct { int32_t keys[8192]; uint16_t vals[8192]; } lzw_tab;

// lzw_encode writes the indexes of one frame as LZW data. It follows the
// writer of Go compress/lzw in LSB order.
static void lzw_encode(bw *w, const uint8_t *px, int64_t n, int mcs) {
    static lzw_tab tab;
    lzw_out o = { w, {0}, 0, 0, 0 };
    int clear = 1 << mcs, eof = clear + 1;
    int width = mcs + 1, hi = eof, overflow = 1 << width;
    memset(tab.keys, 0, sizeof tab.keys);
    bw_byte(w, mcs);
    lz_put(&o, clear, width);
    if (n == 0) { lz_put(&o, eof, width); lz_flush_block(&o); bw_byte(w, 0); return; }
    int code = px[0];
    for (int64_t i = 1; i < n; i++) {
        int literal = px[i];
        int32_t key = (code << 8) | literal;
        uint32_t h = ((uint32_t)key * 2654435761u) >> 19;
        while (tab.keys[h] && tab.keys[h] != key) h = (h + 1) & 8191;
        if (tab.keys[h]) { code = tab.vals[h]; continue; }
        // The code goes out, the literal starts the next one.
        lz_put(&o, code, width);
        code = literal;
        hi++;
        if (hi == overflow) { width++; overflow <<= 1; }
        if (hi == 4095) {
            // The table is full. The stream starts again.
            lz_put(&o, clear, width);
            memset(tab.keys, 0, sizeof tab.keys);
            width = mcs + 1;
            hi = eof;
            overflow = clear << 1;
            continue;
        }
        tab.keys[h] = key;
        tab.vals[h] = (uint16_t)hi;
    }
    lz_put(&o, code, width);
    hi++;
    if (hi == overflow) { width++; overflow <<= 1; }
    lz_put(&o, eof, width);
    if (o.nbits > 0) { o.blk[o.blen++] = (uint8_t)o.bits; if (o.blen == 255) lz_flush_block(&o); }
    lz_flush_block(&o);
    bw_byte(w, 0);
}

// A palette of the writer: the colors and the transparent index.
typedef struct { uint8_t rgb[768]; int n; int trans; int quant; } wpal;

// The hash of a color. It gives the slot in the table of the colors.
static uint32_t color_hash(int r, int g, int b) {
    uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return (key * 2654435761u) >> 22;
}

// build_palette collects the colors of the pixels. It gives 0 when the
// colors do not fit in the table of 256 entries.
static int build_palette(const uint8_t *px, int64_t cnt, wpal *pal, int32_t *table) {
    int32_t keys[1024];
    memset(keys, -1, sizeof keys);
    pal->n = 0;
    pal->trans = -1;
    pal->quant = 0;
    for (int64_t i = 0; i < cnt; i++) {
        const uint8_t *p = px + i * 4;
        if (p[3] < 128) { pal->trans = pal->n; continue; }
        int32_t key = (p[0] << 16) | (p[1] << 8) | p[2];
        uint32_t h = color_hash(p[0], p[1], p[2]);
        while (keys[h] != -1 && keys[h] != key) h = (h + 1) & 1023;
        if (keys[h] == key) continue;
        if (pal->n >= 255) return 0;
        keys[h] = key;
        pal->rgb[pal->n * 3] = p[0];
        pal->rgb[pal->n * 3 + 1] = p[1];
        pal->rgb[pal->n * 3 + 2] = p[2];
        table[pal->n] = key;
        pal->n++;
    }
    if (pal->trans >= 0) {
        // The transparent entry takes one slot more.
        if (pal->n >= 256) return 0;
        pal->trans = pal->n;
        pal->n++;
    }
    return 1;
}

// quant_palette gives the fixed palette of 128 colors for a frame with
// more colors than the table holds. The index of a color is built from the
// top bits of its parts.
static void quant_palette(wpal *pal) {
    pal->n = 128;
    pal->quant = 1;
    for (int i = 0; i < 128; i++) {
        pal->rgb[i * 3] = (uint8_t)(((i >> 4) & 7) * 255 / 7);
        pal->rgb[i * 3 + 1] = (uint8_t)(((i >> 1) & 7) * 255 / 7);
        pal->rgb[i * 3 + 2] = (uint8_t)((i & 1) * 255);
    }
}

// quant_index gives the index of a color in the fixed palette.
static int quant_index(const uint8_t *p) {
    return (p[0] >> 5) * 16 + (p[1] >> 5) * 2 + (p[2] >> 7);
}

// palette_index gives the index of a color in an exact palette.
static int palette_index(const wpal *pal, const int32_t *table, const uint8_t *p) {
    int32_t key = (p[0] << 16) | (p[1] << 8) | p[2];
    for (int i = 0; i < pal->n; i++)
        if (table[i] == key) return i;
    return 0;
}

// write_table writes a color table of 2^bits entries.
static void write_table(bw *w, const wpal *pal, int bits) {
    int size = 1 << bits;
    for (int i = 0; i < size; i++)
        for (int c = 0; c < 3; c++) bw_byte(w, i < pal->n ? pal->rgb[i * 3 + c] : 0);
}

// table_bits gives the number of bits of a table that holds n colors.
static int table_bits(int n) {
    int bits = 1;
    while ((1 << bits) < n) bits++;
    return bits;
}

// img_gif_encode writes the frames as a GIF89a. meta holds the width, the
// height, the loop count and the frame count, and then the width, the
// height, the delay and the disposal of each frame. pixels holds the RGBA
// bytes of the frames one after the other.
uint8_t img_gif_encode(img_slice pixels, gs_rref out, gif_islice meta) {
    if (meta.len < 4) return 0;
    int32_t W = meta.data[0], H = meta.data[1], loop = meta.data[2], nf = meta.data[3];
    if (W < 1 || H < 1 || W > 65535 || H > 65535 || loop > 65535 || loop < -1) return 0;
    if (nf < 1 || meta.len != 4 + (int64_t)nf * 4) return 0;
    const int32_t *fr = meta.data + 4;
    int64_t total = 0;
    for (int f = 0; f < nf; f++) {
        int32_t fw = fr[f * 4], fh = fr[f * 4 + 1], dl = fr[f * 4 + 2], dp = fr[f * 4 + 3];
        if (fw < 1 || fh < 1 || fw > W || fh > H || dl < 0 || dl > 65535 || dp < 0 || dp > 3) return 0;
        total += (int64_t)fw * fh * 4;
    }
    if (pixels.len != total) return 0;
    wpal *pals = (wpal *)calloc((size_t)nf, sizeof(wpal));
    int32_t *tables = (int32_t *)calloc((size_t)nf * 256, sizeof(int32_t));
    uint8_t *idx = NULL;
    bw w = {0};
    if (!pals || !tables) { free(pals); free(tables); return 0; }
    const uint8_t *px = pixels.data;
    // The palette of every frame.
    for (int f = 0; f < nf; f++) {
        int32_t fw = fr[f * 4], fh = fr[f * 4 + 1];
        int64_t cnt = (int64_t)fw * fh;
        int32_t *table = tables + (size_t)f * 256;
        if (!build_palette(px, cnt, &pals[f], table)) quant_palette(&pals[f]);
        px += cnt * 4;
    }
    // The frames share one palette when they all have the same colors.
    int shared = 1;
    for (int f = 1; f < nf && shared; f++) {
        if (pals[f].n != pals[0].n || pals[f].trans != pals[0].trans || pals[f].quant != pals[0].quant) { shared = 0; break; }
        if (memcmp(pals[f].rgb, pals[0].rgb, (size_t)pals[0].n * 3)) shared = 0;
    }
    int gbits = shared ? table_bits(pals[0].n) : 0;
    px = pixels.data;
    bw_byte(&w, 'G'); bw_byte(&w, 'I'); bw_byte(&w, 'F');
    bw_byte(&w, '8'); bw_byte(&w, '9'); bw_byte(&w, 'a');
    bw_u16(&w, W); bw_u16(&w, H);
    bw_byte(&w, shared ? (0x80 | (gbits - 1)) : 0);
    bw_byte(&w, 0);
    bw_byte(&w, 0);
    if (shared) write_table(&w, &pals[0], gbits);
    if (loop >= 0 && nf > 1) {
        const char *id = "NETSCAPE2.0";
        bw_byte(&w, 0x21); bw_byte(&w, 0xFF); bw_byte(&w, 11);
        for (int i = 0; i < 11; i++) bw_byte(&w, id[i]);
        bw_byte(&w, 3); bw_byte(&w, 1); bw_u16(&w, loop); bw_byte(&w, 0);
    }
    for (int f = 0; f < nf && !w.err; f++) {
        int fw = fr[f * 4], fh = fr[f * 4 + 1];
        int64_t cnt = (int64_t)fw * fh;
        const wpal *pal = shared ? &pals[0] : &pals[f];
        int bits = shared ? gbits : table_bits(pal->n);
        int32_t *table = shared ? tables : tables + (size_t)f * 256;
        free(idx);
        idx = (uint8_t *)malloc((size_t)cnt);
        if (!idx) { w.err = 1; break; }
        for (int64_t i = 0; i < cnt; i++) {
            const uint8_t *p = px + i * 4;
            if (p[3] < 128) { idx[i] = (uint8_t)pal->trans; continue; }
            idx[i] = (uint8_t)(pal->quant ? quant_index(p) : palette_index(pal, table, p));
        }
        // The graphic control extension holds the delay and the disposal.
        bw_byte(&w, 0x21); bw_byte(&w, 0xF9); bw_byte(&w, 4);
        bw_byte(&w, (fr[f * 4 + 3] << 2) | (pal->trans >= 0 ? 1 : 0));
        bw_u16(&w, fr[f * 4 + 2]);
        bw_byte(&w, pal->trans >= 0 ? pal->trans : 0);
        bw_byte(&w, 0);
        // The image descriptor.
        bw_byte(&w, 0x2C);
        bw_u16(&w, 0); bw_u16(&w, 0); bw_u16(&w, fw); bw_u16(&w, fh);
        bw_byte(&w, shared ? 0 : (0x80 | (bits - 1)));
        if (!shared) write_table(&w, pal, bits);
        lzw_encode(&w, idx, cnt, bits < 2 ? 2 : bits);
        px += cnt * 4;
    }
    bw_byte(&w, 0x3B);
    uint8_t ok = !w.err;
    if (ok) gs_bld_append(out, w.p, w.n);
    free(w.p); free(idx); free(pals); free(tables);
    return ok;
}
