#!/usr/bin/env python3
"""Make test/png16_cases.goose from Pillow. Run it in the package directory.

Usage: python3 test/make_png16_cases.py > test/png16_cases.goose

16-bit cases: PNG files of bit depth 16 (gray from Pillow mode I;16, and
RGB, RGBA and gray-alpha built by hand with zlib and every filter type)
with the pixels that the reader must give. EXIF cases: the block that
the builder must make, checked here by Pillow Image.Exif().load, and the
pixels of ImageOps.exif_transpose for the eight orientations. The seed is
fixed, so the output is stable.
"""
import io
import random
import struct
import warnings
import zlib

import PIL
from PIL import Image, ImageOps

warnings.simplefilter("ignore")
rng = random.Random(20261107)


def chunk(kind, body):
    c = struct.pack(">I", len(body)) + kind + body
    return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filt(ftype, row, prev, bpp):
    out = bytearray([ftype])
    for i, x in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        pred = [0, a, b, (a + b) // 2, paeth(a, b, c)][ftype]
        out.append((x - pred) & 255)
    return bytes(out)


def png16(w, h, ctype, samples):
    """samples: list of rows, each a list of u16 values (channels interleaved)."""
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    bpp = 2 * ch
    raw = b""
    prev = bytes(w * bpp)
    for y in range(h):
        row = b"".join(struct.pack(">H", v) for v in samples[y])
        raw += filt(rng.randrange(5), row, prev, bpp)
        prev = row
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, ctype, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def v16():
    return rng.choice([0, 1, 255, 256, 257, 32767, 32768, 65534, 65535, rng.randrange(65536)])


def rgba_of(ctype, w, h):
    rows, exp = [], []
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    for y in range(h):
        r = []
        for x in range(w):
            px = [v16() for _ in range(ch)]
            r += px
            if ctype == 0:
                exp += [px[0]] * 3 + [65535]
            elif ctype == 2:
                exp += px + [65535]
            elif ctype == 4:
                exp += [px[0]] * 3 + [px[1]]
            else:
                exp += px
        rows.append(r)
    return rows, exp


def hx16(vals):
    return "".join("%04x" % v for v in vals)


def hx8(b):
    return bytes(b).hex()


out = []
w = out.append
w("// Cases made by test/make_png16_cases.py with Pillow %s." % PIL.__version__)
w("// Do not edit by hand. Make the file again with the script.")
w("")
w("namespace png16_cases;")
w("")
w("import std;")
w('import image from "..";')
w("")
w("fn hexval(c: u8) -> i64 {")
w("    if c >= 97 { return c - 87; }")
w("    return c - 48;")
w("}")
w("")
w("fn bytes_of(s: u8[:]) -> u8[] {")
w("    var px: u8[>..] = [];")
w("    var i = 0;")
w("    while i + 1 < s.len {")
w("        px.push((hexval(s[i]) * 16 + hexval(s[i + 1])) as u8);")
w("        i += 2;")
w("    }")
w("    return px[..];")
w("}")
w("")
w("fn words_of(s: u8[:]) -> u16[] {")
w("    var px: u16[>..] = [];")
w("    var i = 0;")
w("    while i + 3 < s.len {")
w("        px.push((hexval(s[i]) * 4096 + hexval(s[i + 1]) * 256 + hexval(s[i + 2]) * 16 + hexval(s[i + 3])) as u16);")
w("        i += 4;")
w("    }")
w("    return px[..];")
w("}")
w("")
w("// read16 checks the decode of a PNG against the pixels of the oracle.")
w("fn read16(png: u8[:], w: i32, h: i32, want: u8[:]) {")
w("    let img = image::decode16(bytes_of(png));")
w("    assert(img.width == w && img.height == h);")
w("    assert(img.pixels == words_of(want));")
w("}")
w("")
w("fn check_read16() {")
n = 0
# Gray 16 from Pillow mode I;16, read back by Pillow as the oracle.
for (pw, ph) in [(1, 1), (3, 2), (5, 4), (7, 3), (16, 9)]:
    vals = [v16() for _ in range(pw * ph)]
    im = Image.new("I;16", (pw, ph))
    im.putdata(vals)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    back = Image.open(io.BytesIO(buf.getvalue()))
    assert back.mode == "I;16", back.mode
    got = list(back.getdata())
    assert got == vals
    exp = []
    for v in got:
        exp += [v, v, v, 65535]
    w('    read16("%s", %d, %d, "%s");' % (hx8(buf.getvalue()), pw, ph, hx16(exp)))
    n += 1
# Hand-made 16-bit PNG: gray, RGB, gray-alpha, RGBA, every filter type.
for ctype in (0, 2, 4, 6):
    for (pw, ph) in [(1, 1), (4, 6), (9, 5), (13, 3)]:
        rows, exp = rgba_of(ctype, pw, ph)
        w('    read16("%s", %d, %d, "%s");' % (hx8(png16(pw, ph, ctype, rows)), pw, ph, hx16(exp)))
        n += 1
# 8-bit sources widen by 257 (stb_image).
for mode in ("L", "RGB", "RGBA"):
    im = Image.new(mode, (3, 2))
    nch = len(mode)
    im.putdata([tuple(rng.randrange(256) for _ in range(nch)) if nch > 1 else rng.randrange(256) for _ in range(6)])
    buf = io.BytesIO()
    im.save(buf, "PNG")
    rgba = Image.open(io.BytesIO(buf.getvalue())).convert("RGBA").tobytes()
    w('    read16("%s", 3, 2, "%s");' % (hx8(buf.getvalue()), hx16([b * 257 for b in rgba])))
    n += 1
w("}")
w("")

# EXIF builder cases. The reference builder follows TIFF 6.0 and EXIF 2.32.
TYPES = {"short": (3, 2), "long": (4, 4), "ascii": (2, 1), "rational": (5, 8)}


def ref_build(tags):
    ent = {}
    for t, kind, val in tags:
        ent[t] = (kind, val)
    keys = sorted(ent)
    head = b"II" + struct.pack("<HI", 42, 8) + struct.pack("<H", len(keys))
    data_at = 10 + 12 * len(keys) + 4
    body, tail = b"", b""
    for t in keys:
        kind, val = ent[t]
        if kind == "short":
            raw, cnt = struct.pack("<H", val), 1
        elif kind == "long":
            raw, cnt = struct.pack("<I", val), 1
        elif kind == "ascii":
            raw, cnt = val.encode() + b"\0", len(val) + 1
        else:
            raw, cnt = struct.pack("<II", *val), 1
        tcode = TYPES[kind][0]
        if len(raw) <= 4:
            body += struct.pack("<HHI", t, tcode, cnt) + raw.ljust(4, b"\0")
        else:
            body += struct.pack("<HHII", t, tcode, cnt, data_at + len(tail))
            tail += raw
            if len(tail) % 2:
                tail += b"\0"
    return head + body + struct.pack("<I", 0) + tail


def goose_str(s):
    return '"%s"' % s


w("// build_case makes a block through the builder and compares the bytes.")
w("fn check_exif_build() {")
cases = [
    [(0x0112, "short", 6)],
    [(0x010e, "ascii", "goose image description"), (0x0112, "short", 1)],
    [(0x011a, "rational", (72, 1)), (0x011b, "rational", (72, 1)), (0x0128, "short", 2)],
    [(0x0100, "long", 123456), (0x0101, "long", 7), (0x013b, "ascii", "ab"), (0x0110, "ascii", "abc")],
    [(0x0112, "short", 3), (0x0112, "short", 8), (0x010f, "ascii", "")],
    [(0x829a, "rational", (1, 250)), (0x9003, "ascii", "2026:11:07 12:00:00"), (0x0112, "short", 5)],
]
for tags in cases:
    blk = ref_build(tags)
    ex = Image.Exif()
    ex.load(b"Exif\x00\x00" + blk)
    for t, kind, val in tags:
        pass
    final = {}
    for t, kind, val in tags:
        final[t] = (kind, val)
    assert sorted(ex.keys()) == sorted(final), (sorted(ex.keys()), sorted(final))
    for t, (kind, val) in final.items():
        got = ex[t]
        if kind == "rational":
            assert float(got) == val[0] / val[1], (t, got)
        else:
            assert got == val, (t, got, val)
    w("    var b = image::exif_new();")
    for t, kind, val in tags:
        if kind == "short":
            w("    image::exif_set_short(b, %d, %d);" % (t, val))
        elif kind == "long":
            w("    image::exif_set_long(b, %d, %d);" % (t, val))
        elif kind == "ascii":
            w("    image::exif_set_text(b, %d, %s);" % (t, goose_str(val)))
        else:
            w("    image::exif_set_rational(b, %d, %d, %d);" % (t, val[0], val[1]))
    w('    assert(image::exif_build(b) == bytes_of("%s"));' % blk.hex())
w("}")
w("")

# exif_transpose: eight orientations on a 3x2 RGBA image.
w("// turn_case checks apply_orientation against ImageOps.exif_transpose.")
w("fn turn_case(w: i32, h: i32, src: u8[:], o: i64, ew: i32, eh: i32, want: u8[:]) {")
w("    var b = image::exif_new();")
w("    image::exif_set_short(b, 0x0112, o);")
w("    let img = image::Image { width: w, height: h, pixels: bytes_of(src), icc: [], exif: image::exif_build(b) };")
w("    assert(image::orientation(img) == o);")
w("    let got = image::apply_orientation(img);")
w("    assert(got.width == ew && got.height == eh);")
w("    assert(got.pixels == bytes_of(want));")
w("    if o != 1 { assert(image::orientation(got) == 1); }")
w("}")
w("")
w("fn check_turn() {")
for (pw, ph) in [(3, 2), (4, 1), (2, 5)]:
    im = Image.new("RGBA", (pw, ph))
    im.putdata([tuple(rng.randrange(256) for _ in range(4)) for _ in range(pw * ph)])
    for o in range(1, 9):
        a = im.copy()
        ex = a.getexif()
        ex[0x0112] = o
        t = ImageOps.exif_transpose(a)
        w('    turn_case(%d, %d, "%s", %d, %d, %d, "%s");' % (pw, ph, hx8(im.tobytes()), o, t.width, t.height, hx8(t.tobytes())))
w("}")
open("/dev/stdout", "w").write("\n".join(out) + "\n")
