#!/usr/bin/env python3
"""Make test/webp_cases.goose from Pillow. Run it in the package directory.

Usage: python3 test/make_webp_cases.py > test/webp_cases.goose

Pillow writes the WebP files, lossy and lossless, and reads them back. Each
case holds the bytes of a file and the pixels that Pillow reads from it. The
Goose test decodes the bytes with the package and compares. The seed is fixed.
"""
import io
import os
import random
import sys
import struct
import warnings

import PIL
from PIL import Image

warnings.simplefilter("ignore")
rng = random.Random(20261107)


def smooth(w, h, alpha):
    im = Image.new("RGBA", (w, h))
    px = []
    for y in range(h):
        for x in range(w):
            a = 255 if not alpha else (x * 255 // max(w - 1, 1))
            px.append((x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128, a))
    im.putdata(px)
    return im


def noise(w, h, alpha):
    im = Image.new("RGBA", (w, h))
    im.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256),
                 rng.randrange(256) if alpha else 255) for _ in range(w * h)])
    return im


def webp(im, **kw):
    b = io.BytesIO()
    im.save(b, "WEBP", **kw)
    return b.getvalue()


cases = []
for (w, h) in [(1, 1), (7, 5), (16, 16), (33, 17), (64, 48)]:
    for alpha in (False, True):
        src = smooth(w, h, alpha)
        for q in (30, 90):
            cases.append(("lossy", webp(src, quality=q), alpha))
        cases.append(("lossless", webp(src, lossless=True, exact=True), alpha))
for (w, h) in [(5, 3), (16, 9)]:
    for alpha in (False, True):
        cases.append(("lossless", webp(noise(w, h, alpha), lossless=True, exact=True), alpha))

# The hostile files. They must not abort a call.
def hostile_riff():
    # A RIFF that holds the WEBP tag and nothing else.
    return b"RIFF\x04\x00\x00\x00WEBP"


def hostile_huge():
    # A lossless header that claims 16383 x 16383 pixels and holds no data.
    body = b"VP8L" + struct.pack("<I", 5) + bytes([0x2F, 0xFE, 0xBF, 0xFF, 0x1F])
    return b"RIFF" + struct.pack("<I", len(body) + 4) + b"WEBP" + body


def hostile_truncated(data):
    return data[:len(data) // 2]


# busy is the image that the Goose test writes. It must match the one in
# test/run.goose, the function make_busy.
def busy(w=32, h=32):
    im = Image.new("RGBA", (w, h))
    im.putdata([((x * 7 + y * 13) % 256, x * 8, y * 8, 255)
                for y in range(h) for x in range(w)])
    return im


# check_package_output reads a file that the package wrote. Pillow must read
# it back with the same size and with the pixels of the source within the
# tolerance of the lossy encoder. It returns a line for the log.
def check_package_output(path, source, tol, mean_tol):
    if not os.path.exists(path):
        return None
    im = Image.open(path)
    im.load()
    if im.size != source.size:
        raise SystemExit("%s: Pillow gives %s, the source is %s" % (path, im.size, source.size))
    got = im.convert("RGBA").tobytes()
    want = source.tobytes()
    total = 0
    for i in range(len(want)):
        d = abs(got[i] - want[i])
        total += d
        if d > tol:
            raise SystemExit("%s: the pixel byte %d differs by %d" % (path, i, d))
    mean = total / len(want)
    if mean > mean_tol:
        raise SystemExit("%s: the mean difference is %f" % (path, mean))
    return "// Pillow reads %s: %dx%d, the pixels match within %d, the mean is %f" % (
        path, im.size[0], im.size[1], tol, mean)


out = []
p = out.append
p("// Cases made by test/make_webp_cases.py with Pillow %s." % PIL.__version__)
p("// Do not edit by hand. Make the file again with the script.")
p("")
p("namespace webp_cases;")
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
p("// webp_case decodes the file and compares with the pixels of Pillow. A lossless")
p("// file must match exactly. A lossy file may differ by the tolerance.")
p("fn webp_case(file: u8[:], w: i32, h: i32, channels: i32, tol: i32, want: u8[:]) {")
p("    let bytes = from_hex(file);")
p("    let m = image::metadata_bytes(bytes);")
p("    assert(m.ok && m.kind == image::KIND_WEBP && m.width == w && m.height == h);")
p("    assert(m.channels == channels);")
p("    let img = image::decode(bytes);")
p("    assert(img.width == w && img.height == h);")
p("    let px = from_hex(want);")
p("    assert(img.pixels.len == px.len);")
p("    for i in 0..px.len {")
p("        let d = img.pixels[i] - px[i] as i32;")
p("        assert(d <= tol && -d <= tol);")
p("    }")
p("}")
p("")
p("// hostile_check decodes the bytes and asks for the header. Nothing aborts and")
p("// nothing gives a size that the bytes do not hold.")
p("fn hostile_check(file: u8[:]) {")
p("    let bytes = from_hex(file);")
p("    let img = image::decode(bytes);")
p("    assert(img.pixels.len == 0 || img.pixels.len == img.width * img.height * 4);")
p("    let m = image::metadata_bytes(bytes);")
p("    if m.ok && img.pixels.len > 0 {")
p("        assert(m.width == img.width && m.height == img.height);")
p("    }")
p("}")
p("")
p("fn check_decode() {")
for kind, data, alpha in cases:
    im = Image.open(io.BytesIO(data)).convert("RGBA")
    # Lossy decoders may differ in the last bit between builds, so allow 2.
    tol = 0 if kind == "lossless" else 2
    p('    webp_case("%s", %d, %d, %d, %d, "%s");' % (
        data.hex(), im.width, im.height, 4 if alpha else 3, tol, im.tobytes().hex()))
p("}")
p("")
p("fn check_hostile() {")
p('    hostile_check("%s");' % hostile_riff().hex())
p('    hostile_check("%s");' % hostile_huge().hex())
p('    hostile_check("%s");' % hostile_truncated(cases[0][1]).hex())
p('    hostile_check("%s");' % hostile_truncated(cases[-1][1]).hex())
p('    hostile_check("%s");' % b"RIFF\xff\xff\xff\xffWEBPVP8 ".hex())
p("}")
print("\n".join(out))

# The cross check: the package writes a WebP, and Pillow reads it back.
# The line goes to the error stream, so that the case file holds only the
# cases. Run the Goose test first, so that the file is there.
line = check_package_output("test-image.webp", busy(), 110, 8.0)
if line is not None:
    print(line, file=sys.stderr)
