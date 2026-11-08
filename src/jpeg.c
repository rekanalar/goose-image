// The JPEG writer of the goose-image package. It is its own C file.
//
// The tables below are the tables of the JPEG standard (Annex K).

#include "goose.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// The slice types as the compiler lays them out. src/image.c declares the
// same types under the same guards.
typedef struct { const uint8_t *data; int64_t len; } jpg_slice;
typedef struct { const int32_t *data; int64_t len; } jpg_islice;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

   static const unsigned char std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
   static const unsigned char std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
   static const unsigned char std_ac_luminance_values[] = {
      0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
      0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
      0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
      0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
      0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
      0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
      0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   static const unsigned char std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
   static const unsigned char std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
   static const unsigned char std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
   static const unsigned char std_ac_chrominance_values[] = {
      0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
      0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
      0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
      0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
      0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
      0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
      0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
   };
   static const int YQT[] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
                             37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
   static const int UVQT[] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                              99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
// ---- JPEG writer: baseline and progressive, with a subsampling ----
//
// A self-contained encoder. The Huffman tables of the baseline form and the
// quantisation tables are the tables of the JPEG standard (Annex K). The
// progressive form runs the scan script of libjpeg and builds one Huffman
// table per AC scan from the counts of that scan (Annex K.2).


typedef struct { uint8_t *p; size_t n, cap; int fail; } jbuf;

static void jb_put(jbuf *b, int c) {
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        uint8_t *q = (uint8_t *)realloc(b->p, nc);
        if (!q) { b->fail = 1; return; }
        b->p = q; b->cap = nc;
    }
    b->p[b->n++] = (uint8_t)c;
}
static void jb_w16(jbuf *b, int v) { jb_put(b, v >> 8); jb_put(b, v & 255); }

typedef struct {
    uint16_t code[256];
    uint8_t size[256];
    uint8_t bits[17];   // bits[1..16] count the codes of each length
    uint8_t val[256];
    int n;
} jtab;

typedef struct {
    jbuf *b;
    uint32_t acc;
    int nb;
    int counting;       // 1: count the symbols, write nothing
    long *freqs[4];     // 257 counters of each table that is being built
    int tid;            // the table that writes the symbol
    jtab *tab;          // the table that writes the symbols
    int eobrun;
    int be;             // count of buffered correction bits
    uint8_t bitbuf[1024];
} jenc;

static void j_bits(jenc *e, unsigned v, int n) {
    if (e->counting || n == 0) return;
    e->acc = (e->acc << n) | (v & ((1u << n) - 1));
    e->nb += n;
    while (e->nb >= 8) {
        int c = (e->acc >> (e->nb - 8)) & 255;
        jb_put(e->b, c);
        if (c == 255) jb_put(e->b, 0);
        e->nb -= 8;
    }
    e->acc &= (1u << e->nb) - 1;
}
static void j_flush(jenc *e) {
    if (e->counting) return;
    if (e->nb > 0) j_bits(e, 0x7f, 8 - e->nb);
    e->acc = 0; e->nb = 0;
}
static void j_sym(jenc *e, int s) {
    if (e->counting) { e->freqs[e->tid][s]++; return; }
    j_bits(e, e->tab->code[s], e->tab->size[s]);
}

// tab_codes makes the canonical codes from bits and val.
static void tab_codes(jtab *t) {
    int k = 0;
    unsigned code = 0;
    memset(t->code, 0, sizeof t->code);
    memset(t->size, 0, sizeof t->size);
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < t->bits[l]; i++) {
            t->code[t->val[k]] = (uint16_t)code;
            t->size[t->val[k]] = (uint8_t)l;
            code++; k++;
        }
        code <<= 1;
    }
    t->n = k;
}

static void tab_std(jtab *t, const unsigned char *nr, const unsigned char *val, int n) {
    memset(t, 0, sizeof *t);
    for (int l = 1; l <= 16; l++) t->bits[l] = nr[l];
    memcpy(t->val, val, (size_t)n);
    tab_codes(t);
}

// tab_optimal builds a table from the counts (K.2 of the standard).
static void tab_optimal(jtab *t, long *freq) {
    int codesize[257], others[257], bits[40];
    int any = 0;
    for (int i = 0; i < 256; i++) if (freq[i]) any = 1;
    if (!any) freq[0] = 1;
    freq[256] = 1;
    memset(codesize, 0, sizeof codesize);
    memset(bits, 0, sizeof bits);
    for (int i = 0; i < 257; i++) others[i] = -1;
    for (;;) {
        int c1 = -1, c2 = -1;
        long v = 1000000000L;
        for (int i = 0; i <= 256; i++) if (freq[i] && freq[i] <= v) { v = freq[i]; c1 = i; }
        v = 1000000000L;
        for (int i = 0; i <= 256; i++) if (freq[i] && freq[i] <= v && i != c1) { v = freq[i]; c2 = i; }
        if (c2 < 0) break;
        freq[c1] += freq[c2];
        freq[c2] = 0;
        codesize[c1]++;
        while (others[c1] >= 0) { c1 = others[c1]; codesize[c1]++; }
        others[c1] = c2;
        codesize[c2]++;
        while (others[c2] >= 0) { c2 = others[c2]; codesize[c2]++; }
    }
    for (int i = 0; i <= 256; i++) if (codesize[i]) bits[codesize[i]]++;
    for (int i = 32; i > 16; i--) {
        while (bits[i] > 0) {
            int j = i - 2;
            while (bits[j] == 0) j--;
            bits[i] -= 2; bits[i - 1]++; bits[j + 1] += 2; bits[j]--;
        }
    }
    int i = 16;
    while (bits[i] == 0) i--;
    bits[i]--;   // the reserved all-ones code
    memset(t, 0, sizeof *t);
    for (int l = 1; l <= 16; l++) t->bits[l] = (uint8_t)bits[l];
    int p = 0;
    for (int l = 1; l <= 32; l++)
        for (int j = 0; j < 256; j++) if (codesize[j] == l) t->val[p++] = (uint8_t)j;
    tab_codes(t);
}

static void put_dht(jbuf *b, int cls, int id, const jtab *t) {
    int n = 0;
    for (int l = 1; l <= 16; l++) n += t->bits[l];
    jb_w16(b, 0xffc4);
    jb_w16(b, 2 + 1 + 16 + n);
    jb_put(b, (cls << 4) | id);
    for (int l = 1; l <= 16; l++) jb_put(b, t->bits[l]);
    for (int i = 0; i < n; i++) jb_put(b, t->val[i]);
}

static const uint8_t zz[64] = {
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

typedef struct {
    int h, v;           // sampling factors
    int bw, bh;         // blocks across and down, padded to whole MCUs
    int nw, nh;         // blocks across and down that the image covers
    int16_t *coef;      // bw * bh blocks of 64, in zigzag order
    int last_dc;
} jcomp;

static int nbits_of(int v) { int n = 0; while (v) { n++; v >>= 1; } return n; }

// ---- the block coders ----

static void blk_baseline(jenc *e, jtab *dc, jtab *ac, jcomp *c, const int16_t *blk,
    int dtid, int atid) {
    int diff = blk[0] - c->last_dc;
    c->last_dc = blk[0];
    int a = diff < 0 ? -diff : diff, nb = nbits_of(a);
    e->tab = dc; e->tid = dtid; j_sym(e, nb);
    j_bits(e, (unsigned)(diff < 0 ? diff - 1 : diff), nb);
    e->tab = ac; e->tid = atid;
    int r = 0;
    for (int k = 1; k < 64; k++) {
        int v = blk[k];
        if (v == 0) { r++; continue; }
        while (r > 15) { j_sym(e, 0xf0); r -= 16; }
        int av = v < 0 ? -v : v;
        nb = nbits_of(av);
        j_sym(e, (r << 4) | nb);
        j_bits(e, (unsigned)(v < 0 ? v - 1 : v), nb);
        r = 0;
    }
    if (r > 0) j_sym(e, 0);
}

static void emit_buffered(jenc *e, const uint8_t *buf, int n) {
    for (int i = 0; i < n; i++) j_bits(e, buf[i], 1);
}

static void emit_eobrun(jenc *e) {
    if (e->eobrun > 0) {
        int nb = nbits_of(e->eobrun) - 1;
        j_sym(e, nb << 4);
        if (nb) j_bits(e, (unsigned)e->eobrun & ((1u << nb) - 1), nb);
        e->eobrun = 0;
        emit_buffered(e, e->bitbuf, e->be);
        e->be = 0;
    }
}

static void blk_dc_first(jenc *e, jtab *dc, jcomp *c, const int16_t *blk, int al, int dtid) {
    int v = blk[0] >> al;
    int diff = v - c->last_dc;
    c->last_dc = v;
    int a = diff < 0 ? -diff : diff, nb = nbits_of(a);
    e->tab = dc; e->tid = dtid; j_sym(e, nb);
    j_bits(e, (unsigned)(diff < 0 ? diff - 1 : diff), nb);
}

static void blk_ac_first(jenc *e, jtab *ac, const int16_t *blk, int ss, int se, int al,
    int atid) {
    int r = 0;
    e->tab = ac; e->tid = atid;
    for (int k = ss; k <= se; k++) {
        int t = blk[k], t2;
        if (t < 0) { t = -t; t >>= al; t2 = ~t; } else { t >>= al; t2 = t; }
        if (t == 0) { r++; continue; }
        if (e->eobrun > 0) emit_eobrun(e);
        while (r > 15) { j_sym(e, 0xf0); r -= 16; }
        int nb = nbits_of(t);
        j_sym(e, (r << 4) + nb);
        j_bits(e, (unsigned)t2, nb);
        r = 0;
    }
    if (r > 0) {
        e->eobrun++;
        if (e->eobrun == 0x7fff) emit_eobrun(e);
    }
}

static void blk_ac_refine(jenc *e, jtab *ac, const int16_t *blk, int ss, int se, int al,
    int atid) {
    int absv[64], eob = 0, r = 0, br = 0;
    uint8_t *brbuf = e->bitbuf + e->be;
    e->tab = ac; e->tid = atid;
    for (int k = ss; k <= se; k++) {
        int t = blk[k] < 0 ? -blk[k] : blk[k];
        t >>= al;
        absv[k] = t;
        if (t == 1) eob = k;
    }
    for (int k = ss; k <= se; k++) {
        int t = absv[k];
        if (t == 0) { r++; continue; }
        while (r > 15 && k <= eob) {
            emit_eobrun(e);
            j_sym(e, 0xf0);
            r -= 16;
            emit_buffered(e, brbuf, br);
            brbuf = e->bitbuf; br = 0;
        }
        if (t > 1) { brbuf[br++] = (uint8_t)(t & 1); continue; }
        emit_eobrun(e);
        j_sym(e, (r << 4) + 1);
        j_bits(e, blk[k] < 0 ? 0 : 1, 1);
        emit_buffered(e, brbuf, br);
        brbuf = e->bitbuf; br = 0;
        r = 0;
    }
    if (r > 0 || br > 0) {
        e->eobrun++;
        e->be += br;
        // 1000 slots: a block adds at most 63 bits, so flush before the buffer is full.
        if (e->eobrun == 0x7fff || e->be > 1000 - 63 - 1) emit_eobrun(e);
    }
}

// ---- the encoder ----

typedef struct {
    int w, h, hmax, vmax, mcux, mcuy;
    jcomp c[3];
    uint8_t qt[2][64];    // zigzag order
    jtab dc[2], ac[2];
    jbuf b;
} jctx;

// run_scan walks the blocks of the scan. ncs is 3 for an interleaved scan
// and 1 for one component. Ss, Se, Ah, Al are the parameters of the scan.
static void run_scan(jctx *x, jenc *e, int first, int ncs, int ss, int se, int ah, int al,
                     int prog, jtab *dctab, jtab *actab) {
    for (int i = 0; i < 3; i++) x->c[i].last_dc = 0;
    e->eobrun = 0; e->be = 0; e->acc = 0; e->nb = 0;
    #define CODE(ci, blk) do { jcomp *cc = &x->c[ci]; \
        jtab *dt = x->dc + ((ci) ? 1 : 0), *at = actab ? actab : x->ac + ((ci) ? 1 : 0); \
        int dtid = (ci) ? 1 : 0, atid = (ci) ? 3 : 2; \
        if (!prog) blk_baseline(e, dt, at, cc, blk, dtid, atid); \
        else if (ss == 0) { if (ah == 0) blk_dc_first(e, dt, cc, blk, al, dtid); \
                            else j_bits(e, (unsigned)((blk)[0] >> al) & 1, 1); } \
        else if (ah == 0) blk_ac_first(e, at, blk, ss, se, al, atid); \
        else blk_ac_refine(e, at, blk, ss, se, al, atid); } while (0)
    (void)dctab;
    if (ncs == 1) {
        jcomp *c = &x->c[first];
        // A scan of one component skips the padding blocks (B.2.3).
        for (int by = 0; by < c->nh; by++)
            for (int bx = 0; bx < c->nw; bx++) {
                const int16_t *blk = c->coef + ((size_t)by * c->bw + bx) * 64;
                CODE(first, blk);
            }
    } else {
        for (int my = 0; my < x->mcuy; my++)
            for (int mx = 0; mx < x->mcux; mx++)
                for (int ci = 0; ci < 3; ci++) {
                    jcomp *c = &x->c[ci];
                    for (int v = 0; v < c->v; v++)
                        for (int h = 0; h < c->h; h++) {
                            const int16_t *blk = c->coef +
                                ((size_t)(my * c->v + v) * c->bw + mx * c->h + h) * 64;
                            CODE(ci, blk);
                        }
                }
    }
    #undef CODE
    if (prog && ss > 0 && ah == 0) emit_eobrun(e);
    if (prog && ss > 0 && ah > 0) emit_eobrun(e);
    j_flush(e);
}

static void put_sos(jbuf *b, int first, int ncs, int ss, int se, int ah, int al) {
    jb_w16(b, 0xffda);
    jb_w16(b, 6 + 2 * ncs);
    jb_put(b, ncs);
    for (int i = 0; i < ncs; i++) {
        int ci = ncs == 3 ? i : first;
        jb_put(b, ci + 1);
        jb_put(b, ci ? 0x11 : 0x00);
    }
    jb_put(b, ss); jb_put(b, se); jb_put(b, (ah << 4) | al);
}

// do_ac_scan writes one progressive AC scan of one component: it counts,
// builds the table, then writes.
static void do_ac_scan(jctx *x, jenc *e, int ci, int ss, int se, int ah, int al) {
    long freq[257];
    jtab t;
    memset(freq, 0, sizeof freq);
    e->counting = 1; e->freqs[ci ? 3 : 2] = freq;
    run_scan(x, e, ci, 1, ss, se, ah, al, 1, NULL, &t);
    tab_optimal(&t, freq);
    e->counting = 0;
    put_dht(&x->b, 1, ci ? 1 : 0, &t);
    put_sos(&x->b, ci, 1, ss, se, ah, al);
    e->b = &x->b;
    run_scan(x, e, ci, 1, ss, se, ah, al, 1, NULL, &t);
}

// cos table of the DCT
static void fdct_quant(const double *in, const uint8_t *qt, int16_t *out, const double ct[8][8]) {
    double tmp[64], res[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            double s = 0;
            for (int xx = 0; xx < 8; xx++) s += in[y * 8 + xx] * ct[u][xx];
            tmp[y * 8 + u] = s;
        }
    for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++) {
            double s = 0;
            for (int y = 0; y < 8; y++) s += tmp[y * 8 + u] * ct[v][y];
            res[v * 8 + u] = s;
        }
    for (int k = 0; k < 64; k++) {
        double q = res[zz[k]] / qt[k];
        out[k] = (int16_t)(q < 0 ? q - 0.5 : q + 0.5);
    }
}

// jpeg_encode: opts holds w, h, quality, progressive, subsampling.
// Returns 1 and appends the file to out. Returns 0 when the input is bad.
static int jpeg_core(const uint8_t *px, int w, int h, int quality, int prog, int sub,
    int opt, jbuf *ob) {
    jctx *x = (jctx *)calloc(1, sizeof *x);
    if (!x) return 0;
    int ok = 0;
    uint8_t *plane[3] = {0, 0, 0};
    x->w = w; x->h = h;
    int hy = sub == 0 ? 1 : 2, vy = sub == 2 ? 2 : 1;
    x->hmax = hy; x->vmax = vy;
    x->c[0].h = hy; x->c[0].v = vy;
    for (int i = 1; i < 3; i++) { x->c[i].h = 1; x->c[i].v = 1; }
    x->mcux = (w + 8 * hy - 1) / (8 * hy);
    x->mcuy = (h + 8 * vy - 1) / (8 * vy);
    int pw = x->mcux * 8 * hy, ph = x->mcuy * 8 * vy;

    // quantisation tables
    int q = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    int scale = q < 50 ? 5000 / q : 200 - q * 2;
    for (int i = 0; i < 64; i++) {
        int a = (YQT[i] * scale + 50) / 100, b = (UVQT[i] * scale + 50) / 100;
        x->qt[0][i] = (uint8_t)(a < 1 ? 1 : a > 255 ? 255 : a);
        x->qt[1][i] = (uint8_t)(b < 1 ? 1 : b > 255 ? 255 : b);
    }
    // YQT is in natural order, so the table is already in natural order. Make it zigzag.
    {
        uint8_t t0[64], t1[64];
        for (int k = 0; k < 64; k++) { t0[k] = x->qt[0][zz[k]]; t1[k] = x->qt[1][zz[k]]; }
        memcpy(x->qt[0], t0, 64); memcpy(x->qt[1], t1, 64);
    }

    // full-size planes with the edge repeated, then the chroma average
    size_t np = (size_t)pw * ph;
    for (int i = 0; i < 3; i++) {
        plane[i] = (uint8_t *)malloc(np);
        if (!plane[i]) goto done;
    }
    for (int y = 0; y < ph; y++) {
        int sy = y < h ? y : h - 1;
        for (int xx = 0; xx < pw; xx++) {
            int sx = xx < w ? xx : w - 1;
            const uint8_t *p = px + ((size_t)sy * w + sx) * 4;
            double r = p[0], g = p[1], b = p[2];
            double Y = 0.299 * r + 0.587 * g + 0.114 * b;
            double Cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128;
            double Cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128;
            #define CL(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (uint8_t)((v) + 0.5))
            plane[0][(size_t)y * pw + xx] = CL(Y);
            plane[1][(size_t)y * pw + xx] = CL(Cb);
            plane[2][(size_t)y * pw + xx] = CL(Cr);
            #undef CL
        }
    }
    double ct[8][8];
    for (int u = 0; u < 8; u++)
        for (int n = 0; n < 8; n++)
            ct[u][n] = (u == 0 ? sqrt(0.125) : 0.5) * cos((2 * n + 1) * u * M_PI / 16.0);

    for (int ci = 0; ci < 3; ci++) {
        jcomp *c = &x->c[ci];
        int sh = ci ? hy : 1, sv = ci ? vy : 1;      // the average factors
        c->bw = x->mcux * c->h;
        c->bh = x->mcuy * c->v;
        int cw = (w * c->h + hy - 1) / hy, chh = (h * c->v + vy - 1) / vy;
        c->nw = (cw + 7) / 8; c->nh = (chh + 7) / 8;
        c->coef = (int16_t *)malloc((size_t)c->bw * c->bh * 64 * sizeof(int16_t));
        if (!c->coef) goto done;
        for (int by = 0; by < c->bh; by++)
            for (int bx = 0; bx < c->bw; bx++) {
                double in[64];
                for (int y = 0; y < 8; y++)
                    for (int xx = 0; xx < 8; xx++) {
                        int cx = bx * 8 + xx, cy = by * 8 + y;
                        double s = 0;
                        for (int dy = 0; dy < sv; dy++)
                            for (int dx = 0; dx < sh; dx++)
                                s += plane[ci][(size_t)(cy * sv + dy) * pw + cx * sh + dx];
                        in[y * 8 + xx] = s / (sh * sv) - 128.0;
                    }
                fdct_quant(in, x->qt[ci ? 1 : 0], c->coef + ((size_t)by * c->bw + bx) * 64, ct);
            }
    }

    // header
    jbuf *b = &x->b;
    jb_w16(b, 0xffd8);
    jb_w16(b, 0xffe0); jb_w16(b, 16);
    { const char jf[5] = {'J','F','I','F',0}; for (int i = 0; i < 5; i++) jb_put(b, jf[i]); }
    jb_put(b, 1); jb_put(b, 1); jb_put(b, 0); jb_w16(b, 1); jb_w16(b, 1); jb_put(b, 0); jb_put(b, 0);
    for (int t = 0; t < 2; t++) {
        jb_w16(b, 0xffdb); jb_w16(b, 67); jb_put(b, t);
        for (int i = 0; i < 64; i++) jb_put(b, x->qt[t][i]);
    }
    jb_w16(b, prog ? 0xffc2 : 0xffc0);
    jb_w16(b, 17); jb_put(b, 8); jb_w16(b, h); jb_w16(b, w); jb_put(b, 3);
    for (int i = 0; i < 3; i++) { jb_put(b, i + 1); jb_put(b, (x->c[i].h << 4) | x->c[i].v); jb_put(b, i ? 1 : 0); }

    tab_std(&x->dc[0], std_dc_luminance_nrcodes, std_dc_luminance_values, 12);
    tab_std(&x->dc[1], std_dc_chrominance_nrcodes, std_dc_chrominance_values, 12);
    tab_std(&x->ac[0], std_ac_luminance_nrcodes, std_ac_luminance_values, 162);
    tab_std(&x->ac[1], std_ac_chrominance_nrcodes, std_ac_chrominance_values, 162);

    jenc e;
    memset(&e, 0, sizeof e);
    e.b = b;
    // The counters of the four tables of the file.
    long freq[4][257];
    memset(freq, 0, sizeof freq);
    for (int i = 0; i < 4; i++) e.freqs[i] = freq[i];
    if (!prog) {
        if (opt) {
            // The first pass counts the symbols of the scan. The tables of
            // the standard give way to tables that fit the symbols.
            e.counting = 1;
            run_scan(x, &e, 0, 3, 0, 63, 0, 0, 0, NULL, NULL);
            e.counting = 0;
            tab_optimal(&x->dc[0], freq[0]);
            tab_optimal(&x->ac[0], freq[2]);
            tab_optimal(&x->dc[1], freq[1]);
            tab_optimal(&x->ac[1], freq[3]);
            put_dht(b, 0, 0, &x->dc[0]); put_dht(b, 1, 0, &x->ac[0]);
            put_dht(b, 0, 1, &x->dc[1]); put_dht(b, 1, 1, &x->ac[1]);
        } else {
            put_dht(b, 0, 0, &x->dc[0]); put_dht(b, 1, 0, &x->ac[0]);
            put_dht(b, 0, 1, &x->dc[1]); put_dht(b, 1, 1, &x->ac[1]);
        }
        put_sos(b, 0, 3, 0, 63, 0, 0);
        run_scan(x, &e, 0, 3, 0, 63, 0, 0, 0, NULL, NULL);
    } else {
        // The scan script of libjpeg (jpeg_simple_progression) for YCbCr.
        if (opt) {
            // The first pass counts the symbols of the first scan. The
            // tables of the standard give way to tables that fit them.
            e.counting = 1;
            run_scan(x, &e, 0, 3, 0, 0, 0, 1, 1, NULL, NULL);
            e.counting = 0;
            tab_optimal(&x->dc[0], freq[0]);
            tab_optimal(&x->dc[1], freq[1]);
        }
        put_dht(b, 0, 0, &x->dc[0]); put_dht(b, 0, 1, &x->dc[1]);
        put_sos(b, 0, 3, 0, 0, 0, 1);
        run_scan(x, &e, 0, 3, 0, 0, 0, 1, 1, NULL, NULL);
        do_ac_scan(x, &e, 0, 1, 5, 0, 2);
        do_ac_scan(x, &e, 2, 1, 63, 0, 1);
        do_ac_scan(x, &e, 1, 1, 63, 0, 1);
        do_ac_scan(x, &e, 0, 6, 63, 0, 2);
        do_ac_scan(x, &e, 0, 1, 63, 2, 1);
        put_sos(b, 0, 3, 0, 0, 1, 0);
        run_scan(x, &e, 0, 3, 0, 0, 1, 0, 1, NULL, NULL);
        do_ac_scan(x, &e, 2, 1, 63, 1, 0);
        do_ac_scan(x, &e, 1, 1, 63, 1, 0);
        do_ac_scan(x, &e, 0, 1, 63, 1, 0);
    }
    jb_w16(b, 0xffd9);
    if (!b->fail) { ok = 1; *ob = *b; b->p = NULL; }
done:
    for (int i = 0; i < 3; i++) { free(plane[i]); free(x->c[i].coef); }
    free(x->b.p);
    free(x);
    return ok;
}

// img_jpeg encodes RGBA pixels as a JPEG file and appends it to out. The
// opts hold the width, the height, the quality (1 to 100), the progressive
// flag, the subsampling (0 is 4:4:4, 1 is 4:2:2, 2 is 4:2:0) and the flag
// that builds the Huffman tables of the scan.
uint8_t img_jpeg(jpg_slice pixels, gs_rref out, jpg_islice opts) {
    if (opts.len != 6) return 0;
    int w = opts.data[0], h = opts.data[1], q = opts.data[2];
    int prog = opts.data[3], sub = opts.data[4], opt = opts.data[5];
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) return 0;
    if (sub < 0 || sub > 2 || q < 1 || q > 100) return 0;
    if (pixels.len < (int64_t)w * h * 4) return 0;
    jbuf ob;
    memset(&ob, 0, sizeof ob);
    if (!jpeg_core(pixels.data, w, h, q, prog != 0, sub, opt != 0, &ob)) return 0;
    gs_bld_append(out, ob.p, (int64_t)ob.n);
    free(ob.p);
    return 1;
}
