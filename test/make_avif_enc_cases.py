#!/usr/bin/env python3
"""Make test/avif_enc_cases.goose. Run it in the package directory.

Usage: python3 test/make_avif_enc_cases.py > test/avif_enc_cases.goose
       python3 test/make_avif_enc_cases.py --check      (after goose run)

The first form writes the case module. Each case holds the source pixels
and the encode options. The Goose test encodes each case, checks the file
layout and writes the file as test-avif-enc-<n>.avif. The second form reads
those files with Pillow (libavif) and compares the pixels with the source.
A lossless case must match exactly. A lossy case must stay within the bound
of its row. The seed is fixed.
"""
import os
import random
import sys
import warnings

import PIL
from PIL import Image

warnings.simplefilter("ignore")
rng = random.Random(20261107)


def smooth(w, h, alpha):
    return [(x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128,
             (x * 255 // max(w - 1, 1)) if alpha else 255)
            for y in range(h) for x in range(w)]


def noise(w, h, alpha):
    return [(rng.randrange(256), rng.randrange(256), rng.randrange(256),
             rng.randrange(256) if alpha else 255) for _ in range(w * h)]


def clear(w, h):
    # All pixels transparent, with colors under the clear alpha.
    return [(rng.randrange(256), rng.randrange(256), rng.randrange(256), 0)
            for _ in range(w * h)]


# name, w, h, pixels, quality, speed, has_alpha, max error, mean error.
# The lossy bounds hold for smooth content. AV1 at 4:2:0 moves the color of
# a one pixel step by some levels. 100 means lossless and gives exact pixels.
CASES = []
for (w, h) in [(1, 1), (7, 5), (16, 16), (33, 17), (64, 48)]:
    for alpha in (False, True):
        src = smooth(w, h, alpha)
        CASES.append((w, h, src, 100, 6, alpha, 0, 0.0))
        CASES.append((w, h, src, 75, 6, alpha, 40, 8.0))
        CASES.append((w, h, src, 30, 9, alpha, 70, 14.0))
for (w, h) in [(5, 3), (16, 9)]:
    for alpha in (False, True):
        CASES.append((w, h, noise(w, h, alpha), 100, 9, alpha, 0, 0.0))
CASES.append((1, 1, clear(1, 1), 100, 6, True, 0, 0.0))
CASES.append((9, 7, clear(9, 7), 100, 9, True, 0, 0.0))
CASES.append((9, 7, clear(9, 7), 75, 9, True, 0, 0.0))  # alpha 0 must stay 0
CASES.append((2, 2, [(0, 0, 0, 255)] * 4, 75, 6, False, 8, 4.0))


def lit(px):
    return bytes(c for p in px for c in p).hex()


def check():
    n = 0
    worst = 0.0
    for i, (w, h, px, q, sp, alpha, tol, mean_tol) in enumerate(CASES):
        path = "test-avif-enc-%d.avif" % i
        if not os.path.exists(path):
            raise SystemExit("missing %s: run goose run test/run.goose first" % path)
        im = Image.open(path)
        im.load()
        if im.size != (w, h):
            raise SystemExit("%s: Pillow gives %s, want %s" % (path, im.size, (w, h)))
        if (im.mode == "RGBA") != alpha:
            raise SystemExit("%s: Pillow mode %s, alpha %s" % (path, im.mode, alpha))
        got = im.convert("RGBA").tobytes()
        want = bytes(c for p in px for c in p)
        # A lossy write of a fully clear image keeps the alpha. The colors
        # under the clear alpha do not survive. Compare the alpha only.
        if q < 100 and all(p[3] == 0 for p in px):
            for k, a in enumerate(got[3::4]):
                if a != 0:
                    raise SystemExit("%s: clear alpha byte %d is %d" % (path, k, a))
            print("Pillow reads %s: alpha 0 survives" % path, file=sys.stderr)
            continue
        total = 0
        for k in range(len(want)):
            d = abs(got[k] - want[k])
            total += d
            if d > tol:
                raise SystemExit("%s: byte %d differs by %d, bound %d" % (path, k, d, tol))
        mean = total / len(want)
        if mean > mean_tol:
            raise SystemExit("%s: mean %f over %f" % (path, mean, mean_tol))
        worst = max(worst, mean)
        n += 1
    print("Pillow %s (libavif) reads %d files from the package: all within bounds, worst mean %f"
          % (PIL.__version__, n, worst), file=sys.stderr)


if "--check" in sys.argv:
    check()
    sys.exit(0)

out = []
p = out.append
p("// Cases made by test/make_avif_enc_cases.py with Pillow %s." % PIL.__version__)
p("// Do not edit by hand. Make the file again with the script.")
p("")
p("namespace avif_enc_cases;")
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
p("// find gives the index of the four bytes tag in the bytes, or -1.")
p("fn find(b: u8[:], t: u8[:]) -> i64 {")
p("    var i = 0;")
p("    while i + 4 <= b.len {")
p("        if b[i] == t[0] && b[i + 1] == t[1] && b[i + 2] == t[2] && b[i + 3] == t[3] { return i; }")
p("        i += 1;")
p("    }")
p("    return -1;")
p("}")
p("")
p("fn be32(b: u8[:], at: i64) -> i64 {")
p("    return (b[at] as i64 << 24) | (b[at + 1] as i64 << 16) | (b[at + 2] as i64 << 8) | b[at + 3];")
p("}")
p("")
p("// avif_case encodes the pixels and checks the layout of the file: the ftyp")
p("// brand, the size in ispe, the alpha item and the mdat. It writes the file")
p("// for the Pillow check of the script.")
p("fn avif_case(n: i32, file: u8[:], w: i32, h: i32, q: i32, speed: i32, alpha: bool, want: u8[:]) {")
p("    let img = image::Image { width: w, height: h, pixels: from_hex(want), icc: [], exif: [] };")
p("    let out = image::encode_avif(img, q, speed);")
p("    assert(out.len > 64);")
p("    assert(be32(out, 0) == 28 || be32(out, 0) == 32);")
p("    assert(find(out, \"ftyp\") == 4 && find(out, \"avif\") == 8);")
p("    let at = find(out, \"ispe\");")
p("    assert(at > 0 && be32(out, at + 8) == w && be32(out, at + 12) == h);")
p("    assert((find(out, \"auxC\") > 0) == alpha);")
p("    assert((find(out, \"auxl\") > 0) == alpha);")
p("    assert(find(out, \"mdat\") > 0);")
p("    // The same bytes come out of a second encode.")
p("    assert(image::encode_avif(img, q, speed) == out);")
p("    assert(image::write_avif_with(file, img, q, speed));")
p("}")
p("")
p("fn check_cases() {")
for i, (w, h, px, q, sp, alpha, tol, mt) in enumerate(CASES):
    p('    avif_case(%d, "test-avif-enc-%d.avif", %d, %d, %d, %d, %s, "%s");' % (
        i, i, w, h, q, sp, "true" if alpha else "false", lit(px)))
p("}")
p("")
p("fn check_avif_enc() {")
p("    check_cases();")
p("    check_hostile();")
p('    print("avif encode cases ok\\n");')
p("}")
p("")
p("fn check_hostile() {")
p("    // A zero size image and an image with too few pixels fail with an empty result.")
p("    let none = image::Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };")
p("    assert(image::encode_avif(none, 75, 6).len == 0);")
p("    assert(!image::write_avif(\"test-avif-enc-none.avif\", none));")
p("    let short = image::Image { width: 4, height: 4, pixels: [], icc: [], exif: [] };")
p("    assert(image::encode_avif(short, 75, 6).len == 0);")
p("    let neg = image::Image { width: -3, height: 4, pixels: [], icc: [], exif: [] };")
p("    assert(image::encode_avif(neg, 75, 6).len == 0);")
p("    // A side over 16384 fails, with and without the pixels.")
p("    let wide = image::new_image(16385, 1, 1, 2, 3, 4);")
p("    assert(image::encode_avif(wide, 75, 6).len == 0);")
p("    let tall = image::new_image(1, 16385, 1, 2, 3, 4);")
p("    assert(image::encode_avif(tall, 75, 6).len == 0);")
p("    let wide_empty = image::Image { width: 16385, height: 1, pixels: [], icc: [], exif: [] };")
p("    assert(image::encode_avif(wide_empty, 75, 6).len == 0);")
p("    // A speed or a quality out of range does not abort.")
p("    let img = image::new_image(8, 8, 10, 200, 90, 255);")
p("    assert(image::encode_avif(img, -5, -1).len > 0);")
p("    assert(image::encode_avif(img, 900, 99).len > 0);")
p("    // The quality changes the size: noise needs more bytes at a high quality.")
p("    var noisy = image::new_image(32, 32, 0, 0, 0, 255);")
p("    for y in 0..32 { for x in 0..32 {\n        let xi = x as i32; let yi = y as i32;")
p("        image::set_pixel(noisy, xi, yi, ((xi * 37 + yi * 11) % 256) as u8, ((xi * 5 + yi * 71) % 256) as u8, ((xi * yi * 3) % 256) as u8, 255);")
p("    } }")
p("    let lo = image::encode_avif(noisy, 10, 9);")
p("    let hi = image::encode_avif(noisy, 90, 9);")
p("    assert(lo.len > 0 && hi.len > lo.len);")
p("    // encode takes the format number, and the numbers are stable.")
p("    let via = image::encode(img, image::FORMAT_AVIF, 50);")
p("    assert(via.len > 64 && via[4] == 'f' && via[8] == 'a');")
p("    assert(image::FORMAT_PNG == 0 && image::FORMAT_JPEG == 1 && image::FORMAT_BMP == 2);")
p("    assert(image::FORMAT_TGA == 3 && image::FORMAT_HDR == 4 && image::FORMAT_WEBP == 5);")
p("    assert(image::FORMAT_AVIF == 7);")
p("    assert(image::KIND_PIC == 9 && image::KIND_WEBP == 10 && image::KIND_AVIF == 12);")
p("}")
print("\n".join(out))
