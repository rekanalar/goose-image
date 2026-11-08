// The AVIF decode part of the goose-image package. It wraps the vendored
// dav1d and reads the ISOBMFF container of a still AVIF file.
//
// This file does not include goose.h on purpose. The types of dav1d and the
// types of the Goose compiler must not meet in one file. The caller frees
// the buffers that come back with free().
//
// The container read covers the still picture part of the format: ftyp,
// meta with pitm, iloc, iinf, iprp, idat and iref (alpha, premultiplied
// alpha), and mdat. Animation and grid items come back as an error. The
// transforms irot, imir and clap, and the ICC profile, are not applied.

#include "dav1d/dav1d.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The largest side and the largest pixel buffer. A hostile file can claim
// any size in its header, so the limits keep the memory in reach.
#define AVIF_MAX_SIDE 16384
#define AVIF_MAX_PIXELS (64 * 1024 * 1024)
#define AVIF_MAX_ITEMS 64
#define AVIF_MAX_PROPS 32
#define AVIF_MAX_PROP_BYTES 4096
#define AVIF_MAX_EXTENTS 32
#define AVIF_MAX_REFS 64
#define AVIF_MAX_IDAT (16 * 1024 * 1024)

// --- small readers -----------------------------------------------------

static uint32_t rd16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t rd24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t rd64(const uint8_t *p) {
    return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

static uint32_t fourcc(const char *s) {
    return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) |
           ((uint32_t)(uint8_t)s[2] << 8) | (uint32_t)(uint8_t)s[3];
}

// --- one file in memory -------------------------------------------------

// One item of the file.
typedef struct {
    uint32_t id;
    char type[5];
    int method; // 0 file offsets, 1 idat
    int n_ext;
    uint64_t ext_off[AVIF_MAX_EXTENTS];
    uint64_t ext_len[AVIF_MAX_EXTENTS];
    int prop[AVIF_MAX_PROPS]; // ipco indexes, 1 based
    int n_prop;
} AvifItem;

typedef struct {
    const uint8_t *bytes;
    int64_t size;
    uint32_t primary;
    AvifItem items[AVIF_MAX_ITEMS];
    int n_items;
    // The properties of ipco. Index 0 stays unused.
    uint32_t prop_type[AVIF_MAX_PROPS];
    uint8_t prop_data[AVIF_MAX_PROPS][AVIF_MAX_PROP_BYTES];
    int prop_len[AVIF_MAX_PROPS];
    int n_props;
    uint8_t *idat;
    int64_t idat_len;
    int alpha_item; // item id of the alpha item of the picture, 0 when none
    int alpha_prem; // 1 when the color is premultiplied by the alpha item
    // The auxl and prem references of iref, resolved after the walk.
    uint32_t ref_type[AVIF_MAX_REFS];
    uint32_t ref_from[AVIF_MAX_REFS];
    uint32_t ref_to[AVIF_MAX_REFS];
    int n_refs;
} AvifFile;

static void avif_clear(AvifFile *f) {
    free(f->idat);
}

// box_raw walks one box inside a raw buffer. at points at the size field.
// The return value is the offset after the box, or -1 on a bad box.
static int64_t box_raw(const uint8_t *p, int64_t size, int64_t at,
                       uint32_t *type, int64_t *body, int64_t *body_len) {
    if (at < 0 || at > size || size - at < 8) return -1;
    uint64_t box_size = rd32(p + at);
    *type = rd32(p + at + 4);
    int64_t head = 8;
    if (box_size == 1) {
        if (size - at < 16) return -1;
        box_size = rd64(p + at + 8);
        head = 16;
    } else if (box_size == 0) {
        box_size = (uint64_t)(size - at);
    }
    if (box_size < (uint64_t)head || box_size > (uint64_t)(size - at)) return -1;
    *body = at + head;
    *body_len = (int64_t)box_size - head;
    return at + (int64_t)box_size;
}

// in_bounds tells whether a byte range sits inside the file or the idat.
static int in_bounds(const AvifFile *f, int method, uint64_t off, uint64_t len) {
    if (method == 0) {
        if (off > (uint64_t)f->size || len > (uint64_t)f->size ||
            off > (uint64_t)f->size - len) return 0;
        return 1;
    }
    if (off > (uint64_t)f->idat_len || len > (uint64_t)f->idat_len ||
        off > (uint64_t)f->idat_len - len) return 0;
    return 1;
}

// item_payload joins the extents of one item into a fresh buffer. The
// caller frees the result.
static uint8_t *item_payload(const AvifFile *f, const AvifItem *it,
                             int64_t *out_len) {
    if (it->n_ext <= 0) return NULL;
    uint64_t total = 0;
    for (int e = 0; e < it->n_ext; e++) {
        if (!in_bounds(f, it->method, it->ext_off[e], it->ext_len[e])) return NULL;
        total += it->ext_len[e];
        if (total > (uint64_t)AVIF_MAX_PIXELS) return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc(total ? (size_t)total : 1);
    if (!buf) return NULL;
    uint64_t at = 0;
    for (int e = 0; e < it->n_ext; e++) {
        const uint8_t *src = it->method == 0
            ? f->bytes + (int64_t)it->ext_off[e]
            : f->idat + (int64_t)it->ext_off[e];
        memcpy(buf + at, src, (size_t)it->ext_len[e]);
        at += it->ext_len[e];
    }
    *out_len = (int64_t)total;
    return buf;
}

// read_iloc parses the iloc box body.
static int read_iloc(AvifFile *f, const uint8_t *p, int64_t n) {
    if (n < 8) return 0;
    uint32_t ver = p[0];
    if (ver > 2) return 0;
    // One byte holds the offset and length sizes, one holds the base and
    // index sizes. The index size exists from version 1 on.
    int off_sz = p[4] >> 4, len_sz = p[4] & 0xF;
    int base_sz = p[5] >> 4;
    int idx_sz = ver >= 1 ? (p[5] & 0xF) : 0;
    int64_t at = 6;
    if (off_sz > 8 || len_sz > 8 || base_sz > 8 || idx_sz > 8) return 0;
    if (off_sz == 0 || len_sz == 0) return 0;
    uint32_t count = 0;
    if (ver < 2) {
        if (n - at < 2) return 0;
        count = rd16(p + at); at += 2;
    } else {
        if (n - at < 4) return 0;
        count = rd32(p + at); at += 4;
    }
    if (count > AVIF_MAX_ITEMS) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = 0;
        if (ver < 2) {
            if (n - at < 2) return 0;
            id = rd16(p + at); at += 2;
        } else {
            if (n - at < 4) return 0;
            id = rd32(p + at); at += 4;
        }
        int method = 0;
        if (ver >= 1) {
            if (n - at < 2) return 0;
            method = (int)(rd16(p + at) & 0xF); at += 2;
        }
        if (method > 1) return 0;
        if (n - at < 2) return 0;
        at += 2; // data_reference_index, must be 0, checked below
        uint64_t base = 0;
        if (base_sz) {
            if (n - at < base_sz) return 0;
            for (int b = 0; b < base_sz; b++) base = (base << 8) | p[at + b];
            at += base_sz;
        }
        if (base > (uint64_t)0x7fffffff) return 0;
        if (n - at < 2) return 0;
        uint32_t n_ext = rd16(p + at); at += 2;
        if (n_ext > AVIF_MAX_EXTENTS) return 0;
        AvifItem *it = NULL;
        for (int k = 0; k < f->n_items; k++) {
            if (f->items[k].id == id) it = &f->items[k];
        }
        if (!it) {
            if (f->n_items >= AVIF_MAX_ITEMS) return 0;
            it = &f->items[f->n_items++];
            memset(it, 0, sizeof(*it));
            it->id = id;
            it->type[0] = '?';
        }
        it->method = method;
        it->n_ext = (int)n_ext;
        for (uint32_t e = 0; e < n_ext; e++) {
            if (idx_sz) {
                if (n - at < idx_sz) return 0;
                at += idx_sz;
            }
            uint64_t off = 0, len = 0;
            if (off_sz) {
                if (n - at < off_sz) return 0;
                for (int b = 0; b < off_sz; b++) off = (off << 8) | p[at + b];
                at += off_sz;
            }
            if (len_sz) {
                if (n - at < len_sz) return 0;
                for (int b = 0; b < len_sz; b++) len = (len << 8) | p[at + b];
                at += len_sz;
            }
            if (off > (uint64_t)0x7fffffff) return 0;
            it->ext_off[e] = off + base;
            it->ext_len[e] = len;
        }
    }
    return 1;
}

// read_iinf parses the iinf box body with the infe children.
static int read_iinf(AvifFile *f, const uint8_t *p, int64_t n) {
    if (n < 4) return 0;
    uint32_t ver = p[0];
    int64_t at = 4;
    uint32_t count = 0;
    if (ver == 0) {
        if (n - at < 2) return 0;
        count = rd16(p + at); at += 2;
    } else {
        if (n - at < 4) return 0;
        count = rd32(p + at); at += 4;
    }
    if (count > AVIF_MAX_ITEMS) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(p, n, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("infe") && body_len >= 4 && p[body] <= 3) {
            uint32_t ver2 = p[body];
            if (ver2 >= 2) {
                int64_t q = body + 4;
                uint32_t id = 0;
                if (ver2 == 2) {
                    if (n - q < 4) return 0;
                    id = rd16(p + q);
                    q += 2;
                } else {
                    if (n - q < 6) return 0;
                    id = rd32(p + q);
                    q += 4;
                }
                if (rd16(p + q) != 0) return 0; // protected items fail
                q += 2;
                if (n - q < 4) return 0;
                uint8_t itype[4];
                memcpy(itype, p + q, 4);
                q += 4;
                AvifItem *it = NULL;
                for (int k = 0; k < f->n_items; k++) {
                    if (f->items[k].id == id) { it = &f->items[k]; break; }
                }
                if (!it) {
                    if (f->n_items >= AVIF_MAX_ITEMS) return 0;
                    it = &f->items[f->n_items++];
                    memset(it, 0, sizeof(*it));
                    it->id = id;
                }
                memcpy(it->type, itype, 4);
                it->type[4] = 0;
            }
            // infe version 0 and 1 hold media types, not item types. The
            // still AVIF files of today use version 2 or 3, so they are
            // ignored here.
        }
        at = next;
    }
    return 1;
}

// read_iref parses the iref box body. It keeps the auxl and prem relations.
static int read_iref(AvifFile *f, const uint8_t *p, int64_t n) {
    if (n < 4) return 0;
    uint32_t ver = p[0];
    int64_t at = 4;
    int idsz = ver == 0 ? 2 : 4;
    while (at < n) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(p, n, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("auxl") || type == fourcc("prem")) {
            int64_t q = body;
            if (body_len < idsz + 2) return 0;
            uint32_t from = idsz == 2 ? rd16(p + q) : rd32(p + q);
            q += idsz;
            uint32_t refs = rd16(p + q);
            q += 2;
            if ((int64_t)refs * idsz > body + body_len - q) return 0;
            for (uint32_t r = 0; r < refs; r++) {
                uint32_t to = idsz == 2 ? rd16(p + q) : rd32(p + q);
                q += idsz;
                if (f->n_refs >= AVIF_MAX_REFS) return 0;
                f->ref_type[f->n_refs] = type;
                f->ref_from[f->n_refs] = from;
                f->ref_to[f->n_refs] = to;
                f->n_refs++;
            }
        }
        at = next;
    }
    return 1;
}

// item_prop returns the property index of one type for an item. 0 when the
// item holds no such property.
static int item_prop(const AvifItem *it, uint32_t type, const AvifFile *f) {
    for (int i = 0; i < it->n_prop; i++) {
        int idx = it->prop[i];
        if (idx > 0 && idx <= f->n_props && f->prop_type[idx] == type) return idx;
    }
    return 0;
}

// read_ipma parses the ipma box body.
static int read_ipma(AvifFile *f, const uint8_t *p, int64_t n) {
    if (n < 8) return 0;
    uint32_t ver = p[0];
    uint32_t flags = rd24(p + 1);
    int64_t at = 4;
    if (n - at < 4) return 0;
    uint32_t count = rd32(p + at); at += 4;
    if (count > AVIF_MAX_ITEMS) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = 0;
        if (ver == 0) {
            if (n - at < 2) return 0;
            id = rd16(p + at); at += 2;
        } else {
            if (n - at < 4) return 0;
            id = rd32(p + at); at += 4;
        }
        if (n - at < 1) return 0;
        uint32_t n_assoc = p[at]; at += 1;
        if (flags & 1) {
            if (n - at < 2) return 0;
            n_assoc = rd16(p + at); at += 2;
        }
        if (n_assoc > AVIF_MAX_PROPS) return 0;
        AvifItem *it = NULL;
        for (int k = 0; k < f->n_items; k++) {
            if (f->items[k].id == id) { it = &f->items[k]; break; }
        }
        if (!it) return 0;
        for (uint32_t a = 0; a < n_assoc; a++) {
            int essential = 0;
            uint32_t index = 0;
            if (flags & 1) {
                if (n - at < 2) return 0;
                essential = p[at] & 0x80;
                index = rd16(p + at) & 0x7FFF; at += 2;
            } else {
                if (n - at < 1) return 0;
                essential = p[at] & 0x80;
                index = p[at] & 0x7F; at += 1;
            }
            (void)essential;
            if (index == 0) continue;
            if (it->n_prop < AVIF_MAX_PROPS) it->prop[it->n_prop++] = (int)index;
        }
    }
    return 1;
}

// read_iprp parses the iprp box body with ipco and ipma.
static int read_iprp(AvifFile *f, const uint8_t *p, int64_t n) {
    int64_t at = 0;
    int saw_ipma = 0;
    while (at < n) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(p, n, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("ipco")) {
            int64_t q = body;
            while (q < body + body_len) {
                uint32_t ptype = 0;
                int64_t pbody = 0, pbody_len = 0;
                int64_t pnext = box_raw(p, n, q, &ptype, &pbody, &pbody_len);
                if (pnext < 0) return 0;
                if (pbody_len > AVIF_MAX_PROP_BYTES) return 0;
                if (f->n_props + 1 >= AVIF_MAX_PROPS) return 0;
                int idx = ++f->n_props;
                f->prop_type[idx] = ptype;
                f->prop_len[idx] = (int)pbody_len;
                memcpy(f->prop_data[idx], p + pbody, (size_t)pbody_len);
                q = pnext;
            }
        } else if (type == fourcc("ipma")) {
            // ipma must come after ipco, which the format orders anyway.
            // Parse it after the walk to keep the order free.
            saw_ipma = 1;
            at = next;
            continue;
        }
        at = next;
    }
    if (!saw_ipma) return 1;
    // Second walk for ipma, now that the properties are known.
    at = 0;
    while (at < n) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(p, n, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("ipma") && !read_ipma(f, p + body, body_len)) return 0;
        at = next;
    }
    return 1;
}

// read_meta parses the meta box body with its children.
static int read_meta(AvifFile *f, const uint8_t *p, int64_t n) {
    if (n < 4 || p[0] != 0) return 0; // version 0 only
    int64_t at = 4;
    int64_t steps = 0;
    while (at < n) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(p, n, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("pitm")) {
            if (body_len < 6 || p[body] != 0) return 0;
            if (body_len == 6) {
                f->primary = rd16(p + body + 4);
            } else if (body_len == 8) {
                f->primary = rd32(p + body + 4);
            } else {
                return 0;
            }
        } else if (type == fourcc("iloc")) {
            if (!read_iloc(f, p + body, body_len)) return 0;
        } else if (type == fourcc("iinf")) {
            if (!read_iinf(f, p + body, body_len)) return 0;
        } else if (type == fourcc("iref")) {
            if (!read_iref(f, p + body, body_len)) return 0;
        } else if (type == fourcc("iprp")) {
            if (!read_iprp(f, p + body, body_len)) return 0;
        } else if (type == fourcc("idat")) {
            if (body_len > AVIF_MAX_IDAT) return 0;
            f->idat = (uint8_t *)malloc(body_len ? (size_t)body_len : 1);
            if (!f->idat) return 0;
            memcpy(f->idat, p + body, (size_t)body_len);
            f->idat_len = body_len;
        }
        if (++steps > 64) return 0;
        at = next;
    }
    return 1;
}

// avif_parse reads the container of a still AVIF file. It fills the file
// struct and returns 1 on success.
static int avif_parse(AvifFile *f, const uint8_t *bytes, int64_t size) {
    memset(f, 0, sizeof(*f));
    if (size < 12 || size > 0x7fffffff) return 0;
    f->bytes = bytes;
    f->size = size;
    if (memcmp(bytes + 4, "ftyp", 4) != 0) return 0;
    // The major brand or a compatible brand must name avif or avis.
    uint32_t major = rd32(bytes + 8);
    int ok_brand = major == fourcc("avif") || major == fourcc("avis");
    int64_t ftyp_len = rd32(bytes);
    if (ftyp_len < 8 || ftyp_len > size) return 0;
    for (int64_t i = 16; i + 4 <= ftyp_len; i += 4) {
        uint32_t b = rd32(bytes + i);
        if (b == fourcc("avif") || b == fourcc("avis")) ok_brand = 1;
    }
    if (!ok_brand) return 0;
    int64_t at = (int64_t)rd32(bytes); // first box after ftyp
    uint32_t saw_meta = 0;
    while (at < size) {
        uint32_t type = 0;
        int64_t body = 0, body_len = 0;
        int64_t next = box_raw(bytes, size, at, &type, &body, &body_len);
        if (next < 0) return 0;
        if (type == fourcc("meta")) {
            saw_meta = 1;
            if (!read_meta(f, bytes + body, body_len)) return 0;
        }
        // moov and trak mean an animation or a movie. A still reader fails.
        if (type == fourcc("moov")) return 0;
        at = next;
    }
    if (!saw_meta || f->primary == 0) return 0;
    AvifItem *pic = NULL;
    for (int k = 0; k < f->n_items; k++) {
        if (f->items[k].id == f->primary && memcmp(f->items[k].type, "av01", 4) == 0) {
            pic = &f->items[k];
        }
    }
    if (!pic) return 0;
    // The alpha item is an av01 item that points at the picture through
    // auxl and carries the alpha auxiliary type. Other auxl items, such as
    // a depth map, are ignored.
    for (int r = 0; r < f->n_refs; r++) {
        if (f->ref_type[r] != fourcc("auxl") || f->ref_to[r] != f->primary) continue;
        AvifItem *al = NULL;
        for (int k = 0; k < f->n_items; k++) {
            if (f->items[k].id == f->ref_from[r] &&
                memcmp(f->items[k].type, "av01", 4) == 0) al = &f->items[k];
        }
        if (!al) continue;
        int aux = item_prop(al, fourcc("auxC"), f);
        // The auxC body holds 4 bytes of version and flags, then the URN.
        // Two URNs name alpha: the one of the AVIF spec and the one of MPEG-B.
        static const char *const urns[2] = {"urn:mpeg:avif:aux:alpha",
                                            "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"};
        int is_alpha = 0;
        for (int u = 0; aux && u < 2; u++) {
            int n = (int)strlen(urns[u]);
            if (f->prop_len[aux] >= 4 + n && memcmp(f->prop_data[aux] + 4, urns[u], (size_t)n) == 0) is_alpha = 1;
        }
        if (!is_alpha) continue;
        f->alpha_item = (int)al->id;
        break;
    }
    if (f->alpha_item) {
        for (int r = 0; r < f->n_refs; r++) {
            if (f->ref_type[r] == fourcc("prem") && f->ref_from[r] == f->primary &&
                f->ref_to[r] == (uint32_t)f->alpha_item) f->alpha_prem = 1;
        }
    }
    return 1;
}

// ispe_dims reads the ispe property of an item.
static int ispe_dims(const AvifFile *f, const AvifItem *it, int32_t *w, int32_t *h) {
    int idx = item_prop(it, fourcc("ispe"), f);
    if (!idx || f->prop_len[idx] < 8) return 0;
    uint32_t a = rd32(f->prop_data[idx] + 4);
    uint32_t b = rd32(f->prop_data[idx] + 8);
    if (a == 0 || b == 0 || a > (uint32_t)AVIF_MAX_SIDE || b > (uint32_t)AVIF_MAX_SIDE) return 0;
    if ((uint64_t)a * b * 4 > (uint64_t)AVIF_MAX_PIXELS) return 0;
    *w = (int32_t)a;
    *h = (int32_t)b;
    return 1;
}

// --- dav1d --------------------------------------------------------------

// decode_obus decodes one AV1 OBU sequence. It gives the first frame back
// in pic. It returns 1 on success.
static int decode_obus(const uint8_t *payload, int64_t len, Dav1dPicture *pic) {
    Dav1dSettings s;
    dav1d_default_settings(&s);
    s.logger.callback = NULL; // a hostile file must not fill stderr
    s.n_threads = 1;
    s.max_frame_delay = 1;
    // A stream may claim more pixels than the ispe box. The limit is in pixels.
    s.frame_size_limit = (unsigned)(AVIF_MAX_PIXELS / 4);
    Dav1dContext *c = NULL;
    if (dav1d_open(&c, &s) != 0) return 0;
    Dav1dData data;
    memset(&data, 0, sizeof(data));
    int ok = 0;
    uint8_t *buf = dav1d_data_create(&data, (size_t)len); // gives a pointer, NULL on failure
    if (!buf) goto done;
    memcpy(buf, payload, (size_t)len);
    for (int tries = 0; tries < 16; tries++) {
        int r = dav1d_send_data(c, &data);
        if (r == 0) break;
        if (r != DAV1D_ERR(EAGAIN)) goto done;
        // The queue is full, so a frame must come out first.
        Dav1dPicture tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (dav1d_get_picture(c, &tmp) != 0) goto done;
        dav1d_picture_unref(&tmp);
    }
    if (dav1d_get_picture(c, pic) == 0) ok = 1;
done:
    dav1d_data_unref(&data);
    dav1d_close(&c);
    return ok;
}

static uint16_t clamp16(int64_t v) {
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}

// The matrix values of AV1.
#define MC_IDENTITY 0
#define MC_709 1
#define MC_2020 9

// norm16 scales one sample of a plane to 0..65535 with the range of its
// stream. Limited range puts black at 16 and white at 235 for 8 bits.
static int64_t norm16(int64_t v, int bits, int full) {
    int64_t lmax = ((int64_t)1 << bits) - 1;
    if (full) return v * 65535 / lmax;
    int64_t lo = (int64_t)16 << (bits - 8), span = (int64_t)219 << (bits - 8);
    return clamp16((v - lo) * 65535 / span);
}

// chroma reads one chroma sample for the luma place (x, y). With a
// subsampled axis it blends the two nearest chroma samples 3 to 1, which is
// the bilinear upsampling of libavif for chroma that sits between the luma.
static int64_t chroma(const Dav1dPicture *p, int plane, int words, int32_t x, int32_t y,
                      int hsub, int vsub) {
    int32_t cw = (p->p.w + hsub) >> hsub;
    int32_t ch = (p->p.h + vsub) >> vsub;
    int32_t x0 = x >> hsub, y0 = y >> vsub;
    int32_t x1 = x0, y1 = y0;
    // The first and the last luma sample take their own chroma sample alone.
    if (hsub) { x1 = (x & 1) ? x0 + 1 : x0 - 1; if (x1 < 0 || x1 >= cw || x == p->p.w - 1) x1 = x0; }
    if (vsub) { y1 = (y & 1) ? y0 + 1 : y0 - 1; if (y1 < 0 || y1 >= ch) y1 = y0; }
    const uint8_t *base = (const uint8_t *)p->data[plane];
    ptrdiff_t st = p->stride[1];
    int64_t a = words ? ((const uint16_t *)(base + y0 * st))[x0] : (base + y0 * st)[x0];
    int64_t b = words ? ((const uint16_t *)(base + y0 * st))[x1] : (base + y0 * st)[x1];
    int64_t c = words ? ((const uint16_t *)(base + y1 * st))[x0] : (base + y1 * st)[x0];
    int64_t d = words ? ((const uint16_t *)(base + y1 * st))[x1] : (base + y1 * st)[x1];
    return (9 * a + 3 * b + 3 * c + d + 8) >> 4;
}

// sample reads one sample of a plane.
static int64_t sample(const uint8_t *row, int words, int32_t x) {
    return words ? ((const uint16_t *)row)[x] : row[x];
}

// yuv_to_rgba turns the planes of a picture into RGBA. alpha is the picture
// of the alpha item or NULL. unprem is 1 when the alpha item premultiplies
// the color. With wide set the pixels go out as words, else as bytes. The
// chroma of 4:2:0 and 4:2:2 is upsampled with a bilinear blend.
static void yuv_to_rgba(const Dav1dPicture *p, const Dav1dPicture *alpha,
                        int unprem, int cmatrix, int crange, uint8_t *out, int wide) {
    int32_t w = p->p.w;
    int32_t h = p->p.h;
    int bits = p->p.bpc;
    const Dav1dSequenceHeader *s = p->seq_hdr;
    // A colr box of the container wins over the sequence header.
    int matrix = cmatrix >= 0 ? cmatrix : (int)s->mtrx;
    int full = crange >= 0 ? crange : s->color_range != 0;
    int mono = p->p.layout == DAV1D_PIXEL_LAYOUT_I400;
    int64_t ccenter = (int64_t)1 << (bits - 1);
    int64_t cspan = full ? (((int64_t)1 << bits) - 1) : ((int64_t)224 << (bits - 8));
    // The parts of the matrix in 1/1024: R from V, G from U and V, B from U.
    int64_t rv, gv1, gv2, bu;
    if (matrix == MC_709) { rv = 1613; gv1 = 192; gv2 = 479; bu = 1900; }
    else if (matrix == MC_2020) { rv = 1510; gv1 = 168; gv2 = 585; bu = 1927; }
    else { rv = 1435; gv1 = 352; gv2 = 731; bu = 1815; } // 601
    int hsub = p->p.layout == DAV1D_PIXEL_LAYOUT_I420 ||
               p->p.layout == DAV1D_PIXEL_LAYOUT_I422;
    int vsub = p->p.layout == DAV1D_PIXEL_LAYOUT_I420;
    int words = bits > 8;
    int awords = 0, abits = 8, afull = 1;
    if (alpha) {
        awords = alpha->p.bpc > 8;
        abits = alpha->p.bpc;
        afull = alpha->seq_hdr->color_range != 0;
    }
    for (int32_t y = 0; y < h; y++) {
        const uint8_t *yl = (const uint8_t *)p->data[0] + y * p->stride[0];
        const uint8_t *al = alpha ? (const uint8_t *)alpha->data[0] + y * alpha->stride[0] : NULL;
        uint8_t *row = out + (int64_t)y * w * 4 * (wide ? 2 : 1);
        for (int32_t x = 0; x < w; x++) {
            int64_t y16 = norm16(sample(yl, words, x), bits, full);
            int64_t R, G, B;
            if (mono) {
                R = G = B = y16;
            } else if (matrix == MC_IDENTITY) {
                // The planes hold G, B and R as they are.
                G = y16;
                B = norm16(chroma(p, 1, words, x, y, hsub, vsub), bits, full);
                R = norm16(chroma(p, 2, words, x, y, hsub, vsub), bits, full);
            } else {
                // The chroma runs from -32768 to 32767 around its center.
                int64_t cu = (chroma(p, 1, words, x, y, hsub, vsub) - ccenter) * 65535 / cspan;
                int64_t cv = (chroma(p, 2, words, x, y, hsub, vsub) - ccenter) * 65535 / cspan;
                R = y16 + ((rv * cv) >> 10);
                G = y16 - ((gv1 * cu) >> 10) - ((gv2 * cv) >> 10);
                B = y16 + ((bu * cu) >> 10);
            }
            int64_t A = 65535;
            if (al) A = norm16(sample(al, awords, x), abits, afull);
            if (wide) {
                if (unprem && A > 0 && A < 65535) {
                    R = (int64_t)clamp16(R) * 65535 / A;
                    G = (int64_t)clamp16(G) * 65535 / A;
                    B = (int64_t)clamp16(B) * 65535 / A;
                }
                uint16_t *row16 = (uint16_t *)row;
                row16[x * 4 + 0] = clamp16(R);
                row16[x * 4 + 1] = clamp16(G);
                row16[x * 4 + 2] = clamp16(B);
                row16[x * 4 + 3] = clamp16(A);
            } else {
                int r8 = (clamp16(R) + 128) / 257, g8 = (clamp16(G) + 128) / 257;
                int b8 = (clamp16(B) + 128) / 257, a8 = (clamp16(A) + 128) / 257;
                // Like libavif, undo the premultiplication on the 8 bit parts.
                if (unprem && a8 > 0 && a8 < 255) {
                    r8 = (r8 * 255 + a8 / 2) / a8; if (r8 > 255) r8 = 255;
                    g8 = (g8 * 255 + a8 / 2) / a8; if (g8 > 255) g8 = 255;
                    b8 = (b8 * 255 + a8 / 2) / a8; if (b8 > 255) b8 = 255;
                }
                row[x * 4 + 0] = (uint8_t)r8;
                row[x * 4 + 1] = (uint8_t)g8;
                row[x * 4 + 2] = (uint8_t)b8;
                row[x * 4 + 3] = (uint8_t)a8;
            }
        }
    }
}

// --- public -------------------------------------------------------------

// avif_sniff tells whether the bytes start like an AVIF file: an ftyp box
// with the brand avif or avis as major or compatible brand.
int avif_sniff(const uint8_t *bytes, int64_t size) {
    if (size < 16 || memcmp(bytes + 4, "ftyp", 4) != 0) return 0;
    int64_t len = rd32(bytes);
    if (len < 16 || len > size) len = size < 64 ? size : 64;
    if (len > size) len = size;
    uint32_t major = rd32(bytes + 8);
    if (major == fourcc("avif") || major == fourcc("avis")) return 1;
    for (int64_t i = 16; i + 4 <= len; i += 4) {
        uint32_t b = rd32(bytes + i);
        if (b == fourcc("avif") || b == fourcc("avis")) return 1;
    }
    return 0;
}

int avif_probe(const uint8_t *bytes, int64_t size, int32_t *w, int32_t *h, int32_t *ch) {
    AvifFile f;
    if (!avif_parse(&f, bytes, size)) return 0;
    AvifItem *pic = NULL;
    for (int k = 0; k < f.n_items; k++) {
        if (f.items[k].id == f.primary) pic = &f.items[k];
    }
    int ok = 0;
    if (pic && ispe_dims(&f, pic, w, h)) {
        // A monochrome file counts one channel, and two with alpha.
        int ai = item_prop(pic, fourcc("av1C"), &f);
        int mono = ai && f.prop_len[ai] >= 4 && (f.prop_data[ai][2] & 0x10);
        *ch = (mono ? 1 : 3) + (f.alpha_item ? 1 : 0);
        ok = 1;
    }
    avif_clear(&f);
    return ok;
}

// avif_decode_bytes decodes the file into RGBA. bits 8 gives bytes, bits 16
// gives words. The caller frees the result.
static uint8_t *avif_decode_bytes(const uint8_t *bytes, int64_t size,
                                  int32_t *w, int32_t *h, int bits) {
    AvifFile f;
    if (!avif_parse(&f, bytes, size)) return NULL;
    uint8_t *result = NULL;
    AvifItem *pic = NULL;
    for (int k = 0; k < f.n_items; k++) {
        if (f.items[k].id == f.primary) pic = &f.items[k];
    }
    if (!pic) goto fail;
    int32_t cw = 0, chh = 0;
    if (!ispe_dims(&f, pic, &cw, &chh)) goto fail;
    int64_t paylen = 0;
    uint8_t *payload = item_payload(&f, pic, &paylen);
    if (!payload) goto fail;
    Dav1dPicture color;
    memset(&color, 0, sizeof(color));
    if (!decode_obus(payload, paylen, &color)) { free(payload); goto fail; }
    free(payload);
    if (color.p.w != cw || color.p.h != chh) {
        dav1d_picture_unref(&color);
        goto fail;
    }
    // The alpha item decodes in its own context.
    Dav1dPicture apic;
    memset(&apic, 0, sizeof(apic));
    if (f.alpha_item) {
        AvifItem *al = NULL;
        for (int k = 0; k < f.n_items; k++) {
            if (f.items[k].id == (uint32_t)f.alpha_item) al = &f.items[k];
        }
        int64_t alen = 0;
        uint8_t *apay = al ? item_payload(&f, al, &alen) : NULL;
        if (!apay || !decode_obus(apay, alen, &apic)) {
            free(apay);
            dav1d_picture_unref(&color);
            goto fail;
        }
        free(apay);
        if (apic.p.w != cw || apic.p.h != chh) {
            dav1d_picture_unref(&color);
            dav1d_picture_unref(&apic);
            goto fail;
        }
    }
    int64_t n = (int64_t)cw * chh * 4;
    int64_t unit = bits == 8 ? 1 : 2;
    if (n * unit > (int64_t)AVIF_MAX_PIXELS) { // the output is capped too
        dav1d_picture_unref(&color);
        if (f.alpha_item) dav1d_picture_unref(&apic);
        goto fail;
    }
    uint8_t *out = (uint8_t *)malloc((size_t)(n * unit));
    if (!out) {
        dav1d_picture_unref(&color);
        if (f.alpha_item) dav1d_picture_unref(&apic);
        goto fail;
    }
    int cmatrix = -1, crange = -1;
    for (int i = 0; i < pic->n_prop; i++) { // an item may hold a colr of prof and one of nclx
        int ci = pic->prop[i];
        if (ci > 0 && ci <= f.n_props && f.prop_type[ci] == fourcc("colr") &&
            f.prop_len[ci] >= 11 && memcmp(f.prop_data[ci], "nclx", 4) == 0) {
            cmatrix = (int)rd16(f.prop_data[ci] + 8);
            crange = (f.prop_data[ci][10] & 0x80) != 0;
        }
    }
    yuv_to_rgba(&color, f.alpha_item ? &apic : NULL, f.alpha_prem, cmatrix, crange,
                out, bits == 16 ? 1 : 0);
    dav1d_picture_unref(&color);
    if (f.alpha_item) dav1d_picture_unref(&apic);
    *w = cw;
    *h = chh;
    result = out;
fail:
    avif_clear(&f);
    return result;
}

int avif_decode(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                int32_t *w, int32_t *h) {
    uint8_t *px = avif_decode_bytes(bytes, size, w, h, 8);
    if (!px) return 0;
    *out = px;
    *out_len = (int64_t)*w * *h * 4;
    return 1;
}

int avif_decode16(const uint8_t *bytes, int64_t size, uint8_t **out, int64_t *out_len,
                  int32_t *w, int32_t *h) {
    uint8_t *px = avif_decode_bytes(bytes, size, w, h, 16);
    if (!px) return 0;
    *out = px;
    *out_len = (int64_t)*w * *h * 8;
    return 1;
}
