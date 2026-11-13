#!/usr/bin/env python3
"""Make test/tiff_cases.goose from Pillow. Run it in the package directory.

Usage: python3 test/make_tiff_cases.py > test/tiff_cases.goose

Pillow writes the TIFF files in many forms and reads them back. Pillow cannot
write the big endian form, the predictor 2, the tiles or 16 bits of color, so
the script builds those files by hand. The hand built cases hold pixels that
the script computes itself. The seed is fixed.

The script also checks the facts of the layout of one fixture, and it reads the
files that the Goose test wrote with Pillow. The line of that check goes to the
error stream. Run the Goose test first, so that the files are there.
"""
import io
import os
import random
import struct
import sys
import warnings

import PIL
from PIL import Image

warnings.simplefilter("ignore")
rng = random.Random(20261107)


# ---- Files that Pillow writes ----

def smooth(w, h, mode="RGBA"):
    im = Image.new("RGBA", (w, h))
    im.putdata([(x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128,
                 x * 255 // max(w - 1, 1)) for y in range(h) for x in range(w)])
    return im.convert(mode)


def noise(w, h):
    im = Image.new("RGBA", (w, h))
    im.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256),
                 rng.randrange(256)) for _ in range(w * h)])
    return im


def tiff(im, **kw):
    b = io.BytesIO()
    im.save(b, "TIFF", **kw)
    return b.getvalue()


COMPS = [("raw", None), ("lzw", "tiff_lzw"), ("deflate", "tiff_adobe_deflate"),
         ("packbits", "packbits"), ("olddeflate", "tiff_deflate")]


def ifd_facts(data):
    """Read the layout of a fixture: order, compression and strip count."""
    end = "<" if data[:2] == b"II" else ">"
    off = struct.unpack_from(end + "I", data, 4)[0]
    n = struct.unpack_from(end + "H", data, off)[0]
    facts = {}
    for i in range(n):
        tag, typ, cnt = struct.unpack_from(end + "HHI", data, off + 2 + i * 12)
        val = struct.unpack_from(end + ("H" if typ == 3 and cnt == 1 else "I"), data, off + 10 + i * 12)[0]
        facts[tag] = (typ, cnt, val)
    return end, facts


cases = []  # (name, bytes, want rgba bytes, channels)


def add(name, data, want_im, channels):
    cases.append((name, data, want_im.convert("RGBA").tobytes(), channels))


for (cname, comp) in COMPS:
    kw = {} if comp is None else {"compression": comp}
    src = smooth(17, 9)
    add("rgba_" + cname, tiff(src, **kw), Image.open(io.BytesIO(tiff(src, **kw))), 4)
    for mode, ch in (("RGB", 3), ("L", 1), ("LA", 2), ("1", 1)):
        im = smooth(17, 9, mode)
        data = tiff(im, **kw)
        add("%s_%s" % (mode.lower(), cname), data, Image.open(io.BytesIO(data)), ch)
    im = smooth(17, 9, "P")
    data = tiff(im, **kw)
    add("pal_" + cname, data, Image.open(io.BytesIO(data)), 1)
# A larger image makes many strips and grows the codes of LZW to 12 bits.
big = noise(40, 34)
for (cname, comp) in COMPS:
    kw = {"tiffinfo": {278: 8}}
    if comp is not None:
        kw["compression"] = comp
    data = tiff(big, **kw)
    add("noise_" + cname, data, Image.open(io.BytesIO(data)), 4)
facts_end, facts = ifd_facts(tiff(big, compression="tiff_lzw", tiffinfo={278: 8}))
# Layout facts of one fixture: the little endian form, the LZW tag 5 and
# more than one strip. The Goose test relies on them.
assert facts_end == "<" and facts[259][2] == 5 and facts[273][1] > 1, facts


# ---- Files that the script builds ----

W, H = 13, 7


def samples(spp, bits):
    """The samples of the image: a list of tuples, row by row."""
    mx = (1 << bits) - 1
    out = []
    for y in range(H):
        for x in range(W):
            out.append(tuple(((x * 37 + y * 91 + k * 53 + (x * y)) * 257 // 256) % (mx + 1)
                             if bits == 16 else (x * 20 + y * 40 + k * 70 + x * y) % 256
                             for k in range(spp)))
    return out


def pack(sm, bits, end):
    out = bytearray()
    for p in sm:
        for v in p:
            out += struct.pack(end + "H", v) if bits == 16 else bytes([v])
    return out


def build(be=False, bits=8, spp=1, photo=1, predictor=1, tiles=False, comp=1,
          blocks=None, override=None, next_ifd=0, w=W, h=H, rps=None):
    end = ">" if be else "<"
    step = spp * bits // 8
    sm = samples(spp, bits)
    raw = pack(sm, bits, end)
    def difference(buf, width):
        """The predictor 2 on each row of a block: the difference to the left."""
        rowb = width * step
        for r in range(len(buf) // rowb):
            row = buf[r * rowb:(r + 1) * rowb]
            if bits == 8:
                for i in range(len(row) - 1, step - 1, -1):
                    row[i] = (row[i] - row[i - step]) & 255
            else:
                vals = list(struct.unpack(end + "%dH" % (len(row) // 2), row))
                for i in range(len(vals) - 1, spp - 1, -1):
                    vals[i] = (vals[i] - vals[i - spp]) & 0xFFFF
                row = bytearray(struct.pack(end + "%dH" % len(vals), *vals))
            buf[r * rowb:(r + 1) * rowb] = row
        return buf

    if blocks is None:
        if tiles:
            tw, th = 8, 4
            blocks = []
            for ty in range((H + th - 1) // th):
                for tx in range((W + tw - 1) // tw):
                    buf = bytearray()
                    for r in range(th):
                        for c in range(tw):
                            gx, gy = tx * tw + c, ty * th + r
                            off = (gy * W + gx) * step
                            buf += raw[off:off + step] if gx < W and gy < H else bytes(step)
                    blocks.append(bytes(difference(buf, tw) if predictor == 2 else buf))
        else:
            blocks = [bytes(difference(bytearray(raw), W) if predictor == 2 else raw)]
    offk, cntk = (324, 325) if tiles else (273, 279)
    tags = {256: (4, 1, [w]), 257: (4, 1, [h]), 258: (3, spp, [bits] * spp), 259: (3, 1, [comp]),
            262: (3, 1, [photo]), offk: (4, len(blocks), [0] * len(blocks)), 277: (3, 1, [spp]),
            cntk: (4, len(blocks), [0] * len(blocks))}
    if not tiles:
        tags[278] = (4, 1, [rps if rps else H])
    if spp == 4:
        tags[338] = (3, 1, [2])
    if predictor == 2:
        tags[317] = (3, 1, [2])
    if tiles:
        tags[322] = (4, 1, [8])
        tags[323] = (4, 1, [4])
    if override:
        tags.update(override)
    keys = sorted(tags)
    n = len(keys)
    base = 8 + 2 + n * 12 + 4
    extra = bytearray()
    place = {}
    for k in keys:
        typ, cnt, vals = tags[k]
        if cnt * (2 if typ == 3 else 4) > 4:
            place[k] = base + len(extra)
            extra += bytes(cnt * (2 if typ == 3 else 4))  # a hole to fill below
    data_at = base + len(extra)
    offs, at = [], data_at
    for b in blocks:
        offs.append(at)
        at += len(b)
    if offk in tags and tags[offk][2] == [0] * len(blocks):
        tags[offk] = (4, len(blocks), offs)
        tags[cntk] = (4, len(blocks), [len(b) for b in blocks])
    ex = bytearray()
    entries = []
    for k in keys:
        typ, cnt, vals = tags[k]
        size = cnt * (2 if typ == 3 else 4)
        fmt = end + ("%dH" if typ == 3 else "%dI") % cnt
        if size > 4:
            at_k = base + len(ex)
            ex += struct.pack(fmt, *vals)
            entries.append(struct.pack(end + "HHII", k, typ, cnt, at_k))
        elif typ == 3:
            entries.append(struct.pack(end + "HHI", k, typ, cnt) + struct.pack(end + "H", vals[0]) + b"\0\0")
        else:
            entries.append(struct.pack(end + "HHII", k, typ, cnt, vals[0]))
    out = bytearray((b"MM" if be else b"II") + struct.pack(end + "HI", 42, 8))
    out += struct.pack(end + "H", n) + b"".join(entries) + struct.pack(end + "I", next_ifd)
    assert len(out) == base
    out += ex
    out += b"".join(blocks)
    return bytes(out), sm


def want_of(sm, spp, photo, bits):
    out = bytearray()
    for p in sm:
        v = [x >> 8 if bits == 16 else x for x in p]
        if spp == 1:
            g = v[0] if photo == 1 else 255 - v[0]
            out += bytes([g, g, g, 255])
        elif spp == 3:
            out += bytes(v + [255])
        else:
            out += bytes(v)
    return bytes(out)


built = []
for name, kw in [("le_gray", {}), ("be_gray", dict(be=True)), ("le_rgb", dict(spp=3, photo=2)),
                 ("be_rgb", dict(be=True, spp=3, photo=2)), ("le_rgba", dict(spp=4, photo=2)),
                 ("be_rgba", dict(be=True, spp=4, photo=2)),
                 ("white_is_zero", dict(photo=0)),
                 ("pred_gray", dict(predictor=2)), ("pred_rgb", dict(spp=3, photo=2, predictor=2)),
                 ("be_pred_rgba", dict(be=True, spp=4, photo=2, predictor=2)),
                 ("tiles_rgb", dict(spp=3, photo=2, tiles=True)),
                 ("be_tiles_rgba", dict(be=True, spp=4, photo=2, tiles=True)),
                 ("tiles_pred_gray", dict(tiles=True, predictor=2)),
                 ("gray16", dict(bits=16)), ("be_gray16", dict(be=True, bits=16)),
                 ("rgb16", dict(spp=3, photo=2, bits=16)), ("be_rgba16", dict(be=True, spp=4, photo=2, bits=16)),
                 ("pred_rgb16", dict(spp=3, photo=2, bits=16, predictor=2)),
                 ("rows_per_strip", dict(spp=3, photo=2, rps=3))]:
    kw2 = dict(kw)
    rps = kw2.pop("rps", None)
    if rps:
        # Strips of 3 rows: the last strip holds one row.
        _, sm = build(**kw2)
        spp = kw2.get("spp", 1)
        raw = pack(sm, 8, "<")
        rowb = W * spp
        blocks = [bytes(raw[i * rowb * 3:(i + 1) * rowb * 3]) for i in range((H + 2) // 3)]
        data, sm = build(blocks=blocks, rps=3, **kw2)
    else:
        data, sm = build(**kw2)
    spp, photo, bits = kw2.get("spp", 1), kw2.get("photo", 1), kw2.get("bits", 8)
    built.append((name, data, want_of(sm, spp, photo, bits), 4 if spp == 4 else spp))

# A multi page file: Pillow writes three pages. The first page is the pixels.
pages = [smooth(9, 5), smooth(6, 6), smooth(4, 3)]
b = io.BytesIO()
pages[0].save(b, "TIFF", save_all=True, append_images=pages[1:])
multi = b.getvalue()
multi_want = Image.open(io.BytesIO(multi)).convert("RGBA").tobytes()
assert Image.open(io.BytesIO(multi)).n_frames == 3


# ---- Hostile files ----

hostile = []
good, _ = build(spp=3, photo=2)
hostile.append(good[:len(good) // 2])
hostile.append(good[:20])
hostile.append(good[:7])
# The directory points to itself as the next directory.
loop, _ = build(next_ifd=8)
hostile.append(loop)
# A header that claims 65535 x 65535 pixels.
hostile.append(build(override={256: (4, 1, [65535]), 257: (4, 1, [65535])})[0])
# A size under the cap that the strip cannot fill: 8000 x 8000 with one tiny strip.
hostile.append(build(override={256: (4, 1, [8000]), 257: (4, 1, [8000])})[0])
# A strip place beyond the end of the file.
hostile.append(build(override={273: (4, 1, [0x7fffff00])})[0])
hostile.append(build(override={279: (4, 1, [0xffffffff])})[0])
# A count of strips that is lower than the strips of the image.
hostile.append(build(override={278: (4, 1, [1])})[0])
# Tiles: a count lower than the tiles, a huge tile size, a zero tile size.
hostile.append(build(tiles=True, spp=3, photo=2, blocks=[bytes(96)])[0])
hostile.append(build(tiles=True, spp=3, photo=2, override={322: (4, 1, [0x7fffffff]), 323: (4, 1, [0x7fffffff])})[0])
hostile.append(build(tiles=True, spp=3, photo=2, override={322: (4, 1, [0])})[0])
# Odd bit depths, sample counts and planar form.
hostile.append(build(override={258: (3, 1, [3])})[0])
hostile.append(build(override={277: (3, 1, [200])})[0])
hostile.append(build(override={284: (3, 1, [2])})[0])
hostile.append(build(override={259: (3, 1, [7])})[0])
# The palette form without a table of colors.
hostile.append(build(photo=3)[0])
# Garbage in a compressed strip: LZW, Deflate and PackBits.
for comp in (5, 8, 32773):
    junk = bytes(rng.randrange(256) for _ in range(120))
    hostile.append(build(comp=comp, blocks=[junk])[0])
# A deflate stream that claims a long match with no history, and an LZW stream
# of clear codes only.
hostile.append(build(comp=8, blocks=[b"\x78\x01\xff\xff\xff\xff\xff\xff\xff\xff"])[0])
hostile.append(build(comp=5, blocks=[b"\x80\x40\x20\x10\x08\x04\x02\x01" * 20])[0])
# Random flips in a good file of each compression.
for (cname, comp) in COMPS[1:4]:
    data = bytearray(tiff(smooth(17, 9), compression=comp))
    for _ in range(6):
        data[rng.randrange(8, len(data))] = rng.randrange(256)
    hostile.append(bytes(data))


# ---- The cross check of the writer ----

def check_package_output(base):
    """Pillow reads the files that the Goose test wrote. They hold busy()."""
    if not os.path.exists(base % "none"):
        return None
    want = Image.new("RGBA", (32, 32))
    want.putdata([((x * 7 + y * 13) % 256, x * 8, y * 8, 255) for y in range(32) for x in range(32)])
    lines = []
    for c in ("none", "lzw", "deflate"):
        im = Image.open(base % c)
        im.load()
        if im.size != (32, 32) or im.convert("RGBA").tobytes() != want.tobytes():
            raise SystemExit("%s: Pillow reads other pixels" % (base % c))
        lines.append("%s: mode %s, compression %s" % (base % c, im.mode, im.info.get("compression")))
    return "// Pillow reads the files of the package: " + "; ".join(lines)


# ---- The Goose file ----

out = []
p = out.append
p("// Generated by test/make_tiff_cases.py. Do not edit by hand.")
p("// The cases come from Pillow %s." % PIL.__version__)
p("")
p("namespace tiff_cases;")
p("")
p("import std;")
p('import image from "..";')
p("")
p("fn hexval(c: u8) -> i64 {")
p("    if c >= 97 { return c - 87; }")
p("    return c - 48;")
p("}")
p("")
p("fn from_hex(s: u8[:]) -> u8[] {")
p("    var px: u8[>..] = [];")
p("    var i = 0;")
p("    while i + 1 < s.len {")
p("        let v: i64 = hexval(s[i]) * 16 + hexval(s[i + 1]);")
p("        px.push(v as u8);")
p("        i += 2;")
p("    }")
p("    return px[..];")
p("}")
p("")
p("// tiff_case decodes the file. The pixels must match the pixels of the oracle.")
p("fn tiff_case(file: u8[:], w: i32, h: i32, channels: i32, want: u8[:]) {")
p("    let bytes = from_hex(file);")
p("    let m = image::metadata_bytes(bytes);")
p("    assert(m.ok && m.kind == image::KIND_TIFF && m.width == w && m.height == h);")
p("    assert(m.channels == channels);")
p("    let img = image::decode(bytes);")
p("    assert(img.width == w && img.height == h);")
p("    assert(img.pixels == from_hex(want));")
p("}")
p("")
p("// pages_case counts the pages and decodes the first one.")
p("fn pages_case(file: u8[:], pages: i32, w: i32, h: i32, want: u8[:]) {")
p("    let bytes = from_hex(file);")
p("    assert(image::decode_tiff_pages(bytes) == pages);")
p("    let img = image::decode(bytes);")
p("    assert(img.width == w && img.height == h);")
p("    assert(img.pixels == from_hex(want));")
p("}")
p("")
p("// hostile_check decodes the bytes and asks for the header. Nothing aborts and")
p("// nothing gives a size that the bytes do not hold.")
p("fn hostile_check(file: u8[:]) {")
p("    let bytes = from_hex(file);")
p("    let img = image::decode(bytes);")
p("    assert(img.pixels.len == 0 || img.pixels.len == img.width * img.height * 4);")
p("    image::metadata_bytes(bytes);")
p("    image::decode_tiff_pages(bytes);")
p("}")
p("")
p("fn check_decode() {")
for name, data, want, ch in cases + built:
    im = None
    w = h = 0
    if name in dict((n, 0) for n, _, _, _ in built):
        w, h = W, H
    else:
        w, h = Image.open(io.BytesIO(data)).size
    p("    // %s" % name)
    p('    tiff_case("%s", %d, %d, %d, "%s");' % (data.hex(), w, h, ch, want.hex()))
p("}")
p("")
p("fn check_pages() {")
p('    pages_case("%s", 3, 9, 5, "%s");' % (multi.hex(), multi_want.hex()))
p("}")
p("")
p("fn check_hostile() {")
for data in hostile:
    p('    hostile_check("%s");' % data.hex())
p("}")
print("\n".join(out))

line = check_package_output("test-image-%s.tif")
if line is not None:
    print(line, file=sys.stderr)
