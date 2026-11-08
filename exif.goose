// The reader of the EXIF tags.
//
// The block of EXIF is the TIFF part of the APP1 marker of a JPEG or
// the eXIf part of a PNG. The TIFF starts with the byte order, a
// number 42 and the place of the first list of tags. Each entry holds
// a tag, a type, a count and the value or the place of the value.
//
// The reader takes the raw bytes and gives the value of one tag. It
// gives an empty text when the tag is not there or the block is not a
// TIFF. A hostile block never aborts the program.
namespace image;

import std;

const TYPE_BYTE = 1;
const TYPE_ASCII = 2;
const TYPE_SHORT = 3;
const TYPE_LONG = 4;
const TYPE_RATIONAL = 5;
const TYPE_UNDEF = 7;

// order_of tells whether the block holds the big endian form. It
// gives false for the little endian form and for a block that is not
// a TIFF.
fn order_of(tiff: const u8[:]) -> bool, bool {
    if tiff.len < 8 { return false, false; }
    if tiff[0] == 0x4d && tiff[1] == 0x4d { return true, true; }
    if tiff[0] == 0x49 && tiff[1] == 0x49 { return false, true; }
    return false, false;
}

// num_at reads one number of the width. The order comes from the
// tiff.
fn num_at(tiff: const u8[:], at: i64, width: i64, big: bool) -> i64 {
    if at < 0 || at + width > tiff.len { return 0; }
    var v: i64 = 0;
    if big {
        var i: i64 = 0;
        while i < width {
            v = (v << 8) | tiff[at + i];
            i += 1;
        }
    } else {
        var i: i64 = width - 1;
        while i >= 0 {
            v = (v << 8) | tiff[at + i];
            i -= 1;
        }
    }
    return v;
}

// entry_at gives the tag, the type, the count and the place of the
// value of the entry at `at`. It gives found false past the end.
fn entry_at(tiff: const u8[:], at: i64, big: bool) -> i64, i64, i64, i64, bool {
    if at < 0 || at + 12 > tiff.len { return 0, 0, 0, 0, false; }
    let tag = num_at(tiff, at, 2, big);
    let kind = num_at(tiff, at + 2, 2, big);
    let count = num_at(tiff, at + 4, 4, big);
    return tag, kind, count, at + 8, true;
}

// size_of_type gives the width of one value of the type.
fn size_of_type(kind: i64) -> i64 {
    if kind == TYPE_BYTE || kind == TYPE_ASCII || kind == TYPE_UNDEF { return 1; }
    if kind == TYPE_SHORT { return 2; }
    if kind == TYPE_LONG { return 4; }
    if kind == TYPE_RATIONAL { return 8; }
    return 0;
}

// value_place gives the place of the value. A value of four bytes or
// fewer sits in the entry itself.
fn value_place(tiff: const u8[:], place: i64, bytes: i64, big: bool) -> i64 {
    if bytes <= 4 { return place; }
    return num_at(tiff, place, 4, big);
}

// exif_value gives the raw bytes of the value of one tag. The text is
// empty when the tag is not there, the block is not a TIFF or the
// value is out of the block. The bytes are a slice over the block, so
// the block must outlive the value.
fn exif_value(tiff: const u8[:], want: i64) -> const u8[:] {
    let big, ok = order_of(tiff);
    if !ok { return ""; }
    if num_at(tiff, 2, 2, big) != 42 { return ""; }
    var ifd = num_at(tiff, 4, 4, big);
    var rounds: i64 = 0;
    while ifd > 0 && ifd + 2 <= tiff.len && rounds < 64 {
        rounds += 1;
        let count = num_at(tiff, ifd, 2, big);
        var at = ifd + 2;
        var i: i64 = 0;
        while i < count && at + 12 <= tiff.len {
            let tag, kind, n, place, ok2 = entry_at(tiff, at, big);
            if ok2 && tag == want {
                let width = size_of_type(kind);
                if width <= 0 || n <= 0 || width * n > 1048576 { return ""; }
                let vp = value_place(tiff, place, width * n, big);
                if vp < 0 || vp + width * n > tiff.len { return ""; }
                return tiff[vp..vp + width * n];
            }
            at += 12;
            i += 1;
        }
        ifd = num_at(tiff, at, 4, big);
    }
    return "";
}

// exif_text gives the value of one tag as a text. A SHORT or a LONG
// comes out as its digits. An ASCII comes out as itself. Anything
// else gives an empty text.
fn exif_text(tiff: const u8[:], tag: i64) -> const u8[:] {
    let v = exif_value(tiff, tag);
    if v.len == 0 { return ""; }
    // The text of ASCII ends with a zero byte. The slice keeps the
    // text only. A value of another form gives an empty text.
    if v[v.len - 1] != 0 { return ""; }
    return v[..v.len - 1];
}

// exif_short gives the value of one tag as a number. The form SHORT
// gives its number. The form LONG gives it too. A text or a RATIONAL
// gives 0.
fn exif_short(tiff: const u8[:], tag: i64) -> i64 {
    let big, ok = order_of(tiff);
    if !ok { return 0; }
    let v = exif_value(tiff, tag);
    if v.len == 2 { return num_at(v, 0, 2, big); }
    if v.len == 4 { return num_at(v, 0, 4, big); }
    return 0;
}

// ---- The builder of an EXIF block ----

// ExifBuilder holds the tags of IFD0 as records in one pool. A record is
// the tag, the kind (2 bytes each), the count (4 bytes) and the value
// bytes, all little endian. Build with exif_build.
struct ExifBuilder {
    pool: u8[>..],
}

fn exif_new() -> ExifBuilder {
    return ExifBuilder { pool: [] };
}

fn put_le(pool: u8[>..]&, v: i64, width: i64) {
    var i: i64 = 0;
    while i < width {
        pool.push(((v >> (8 * i)) & 255) as u8);
        i += 1;
    }
}

fn exif_add(b: ExifBuilder&, tag: i64, kind: i64, count: i64) {
    put_le(b.pool, tag, 2);
    put_le(b.pool, kind, 2);
    put_le(b.pool, count, 4);
}

// exif_set_short adds a SHORT tag. A later set of the same tag wins.
fn exif_set_short(b: ExifBuilder&, tag: i64, v: i64) {
    exif_add(b, tag, TYPE_SHORT, 1);
    put_le(b.pool, v & 0xffff, 2);
}

fn exif_set_long(b: ExifBuilder&, tag: i64, v: i64) {
    exif_add(b, tag, TYPE_LONG, 1);
    put_le(b.pool, v & 0xffffffff, 4);
}

// exif_set_text adds an ASCII tag. The zero byte at the end comes with it.
fn exif_set_text(b: ExifBuilder&, tag: i64, s: const u8[:]) {
    exif_add(b, tag, TYPE_ASCII, s.len + 1);
    for i in 0..s.len { b.pool.push(s[i]); }
    b.pool.push(0);
}

// exif_set_rational adds a RATIONAL tag, a numerator and a denominator.
fn exif_set_rational(b: ExifBuilder&, tag: i64, num: i64, den: i64) {
    exif_add(b, tag, TYPE_RATIONAL, 1);
    put_le(b.pool, num & 0xffffffff, 4);
    put_le(b.pool, den & 0xffffffff, 4);
}

// record_len gives the length of the record at `at` of the pool.
fn record_len(pool: const u8[:], at: i64) -> i64 {
    let kind = num_at(pool, at + 2, 2, false);
    let count = num_at(pool, at + 4, 4, false);
    return 8 + size_of_type(kind) * count;
}

// exif_build gives the TIFF block: the header II, 42 and the offset 8,
// then IFD0 with the entries sorted by tag, then the values that need
// more than four bytes. The values start on even places. The next IFD
// offset is 0. A tag set twice counts once, the last set.
fn exif_build(b: ExifBuilder&) -> u8[] {
    let pool = b.pool[..];
    // The records to write: the last record of each tag, in tag order.
    var order: i64[>..] = [];
    var prev: i64 = -1;
    while true {
        var best: i64 = -1;
        var best_tag: i64 = 0;
        var at: i64 = 0;
        while at < pool.len {
            let tag = num_at(pool, at, 2, false);
            if tag > prev && (best < 0 || tag <= best_tag) {
                best = at;
                best_tag = tag;
            }
            at += record_len(pool, at);
        }
        if best < 0 { break; }
        order.push(best);
        prev = best_tag;
    }
    let n = order.len;
    var out: u8[>..] = [];
    out.push(0x49); out.push(0x49); out.push(42); out.push(0);
    put_le(out, 8, 4);
    put_le(out, n, 2);
    let data_at: i64 = 8 + 2 + 12 * n + 4;
    var tail: u8[>..] = [];
    for k in 0..n {
        let at = order[k];
        let bytes = record_len(pool, at) - 8;
        for j in 0..8 { out.push(pool[at + j]); }
        if bytes <= 4 {
            for j in 0..4 { out.push(if j < bytes { pool[at + 8 + j] } else { 0 }); }
        } else {
            put_le(out, data_at + tail.len, 4);
            for j in 0..bytes { tail.push(pool[at + 8 + j]); }
            if tail.len % 2 == 1 { tail.push(0); }
        }
    }
    put_le(out, 0, 4);
    for j in 0..tail.len { out.push(tail[j]); }
    return out[..];
}

// exif_with_short gives a copy of the block with the SHORT value of one
// tag of IFD0 replaced. The block comes back unchanged when the tag is
// missing, is not a SHORT or the block is hostile.
fn exif_with_short(tiff: const u8[:], want: i64, v: i64) -> u8[] {
    var out: u8[>..] = [];
    for i in 0..tiff.len { out.push(tiff[i]); }
    let big, ok = order_of(tiff);
    if !ok || num_at(tiff, 2, 2, big) != 42 { return out[..]; }
    let ifd = num_at(tiff, 4, 4, big);
    if ifd <= 0 || ifd + 2 > tiff.len { return out[..]; }
    let count = num_at(tiff, ifd, 2, big);
    var i: i64 = 0;
    while i < count && ifd + 2 + 12 * i + 12 <= tiff.len {
        let at = ifd + 2 + 12 * i;
        if num_at(tiff, at, 2, big) == want && num_at(tiff, at + 2, 2, big) == TYPE_SHORT && num_at(tiff, at + 4, 4, big) == 1 {
            if big {
                out[at + 8] = ((v >> 8) & 255) as u8;
                out[at + 9] = (v & 255) as u8;
            } else {
                out[at + 8] = (v & 255) as u8;
                out[at + 9] = ((v >> 8) & 255) as u8;
            }
            return out[..];
        }
        i += 1;
    }
    return out[..];
}
