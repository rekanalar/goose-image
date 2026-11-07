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
