#!/usr/bin/env python3
"""Make test/geometry_cases.goose from Pillow. Run it in the package directory.

Usage: python3 test/make_geometry_cases.py > test/geometry_cases.goose

Every case holds the pixels that Pillow gave. The Goose test compares the
pixels of the package with them. The seed is fixed, so the output is stable.
"""
import random
import sys
import warnings

import PIL
from PIL import Image

warnings.simplefilter("ignore")
rng = random.Random(20261107)
count = {"crop": 0, "transpose": 0, "paste": 0, "blend": 0, "rotate": 0}


def rand_image(w, h):
    im = Image.new("RGBA", (w, h))
    px = []
    for i in range(w * h):
        a = rng.choice([0, 1, 64, 127, 128, 129, 200, 254, 255, rng.randrange(256)])
        px.append((rng.randrange(256), rng.randrange(256), rng.randrange(256), a))
    im.putdata(px)
    return im


def hx(im):
    return im.tobytes().hex()


out = []
w = out.append

w("// Cases made by tmp/make_geometry_cases.py with Pillow %s." % PIL.__version__)
w("// Do not edit by hand. Make the file again with the script.")
w("")
w("namespace geometry_cases;")
w("")
w("import std;")
w('import image from "..";')
w("")
w("fn hexval(c: u8) -> i64 {")
w("    if c >= 97 { return c - 87; }")
w("    return c - 48;")
w("}")
w("")
w("// from_hex makes an image from a string of hex digits.")
w("fn from_hex(w: i32, h: i32, s: u8[:]) -> image::Image {")
w("    var px: u8[>..] = [];")
w("    var i = 0;")
w("    while i + 1 < s.len {")
w("        let v: i64 = hexval(s[i]) * 16 + hexval(s[i + 1]);")
w("        px.push(v as u8);")
w("        i += 2;")
w("    }")
w("    return image::Image { width: w, height: h, pixels: px[..] };")
w("}")
w("")
w("// same tells whether the image has the size and the pixels of the string.")
w("fn same(img: image::Image&, w: i32, h: i32, s: u8[:]) -> bool {")
w("    if img.width != w || img.height != h { return false; }")
w("    let want = from_hex(w, h, s);")
w("    return img.pixels == want.pixels;")
w("}")
w("")
w("fn crop_case(sw: i32, sh: i32, src: u8[:], l: i32, t: i32, r: i32, b: i32,")
w("    ew: i32, eh: i32, want: u8[:]) {")
w("    var img = from_hex(sw, sh, src);")
w("    let got = image::crop(img, l, t, r, b);")
w("    assert(same(got, ew, eh, want));")
w("    // The source stays as it was.")
w("    assert(same(img, sw, sh, src));")
w("}")
w("")
w("fn transpose_case(sw: i32, sh: i32, src: u8[:], op: i32, ew: i32, eh: i32,")
w("    want: u8[:]) {")
w("    var img = from_hex(sw, sh, src);")
w("    let got = image::transpose(img, op);")
w("    assert(same(got, ew, eh, want));")
w("    assert(same(img, sw, sh, src));")
w("}")
w("")
w("fn paste_case(dw: i32, dh: i32, dst: u8[:], sw: i32, sh: i32, src: u8[:],")
w("    x: i32, y: i32, want: u8[:]) {")
w("    var d = from_hex(dw, dh, dst);")
w("    var s = from_hex(sw, sh, src);")
w("    image::paste(d, s, x, y);")
w("    assert(same(d, dw, dh, want));")
w("    assert(same(s, sw, sh, src));")
w("}")
w("")
w("fn blend_case(dw: i32, dh: i32, dst: u8[:], sw: i32, sh: i32, src: u8[:],")
w("    x: i32, y: i32, want: u8[:]) {")
w("    var d = from_hex(dw, dh, dst);")
w("    var s = from_hex(sw, sh, src);")
w("    image::alpha_composite(d, s, x, y);")
w("    assert(same(d, dw, dh, want));")
w("    assert(same(s, sw, sh, src));")
w("}")
w("")
w("fn rotate_case(sw: i32, sh: i32, src: u8[:], angle: f64, expand: bool,")
w("    ew: i32, eh: i32, want: u8[:]) {")
w("    var img = from_hex(sw, sh, src);")
w("    let got = image::rotate(img, angle, expand);")
w("    assert(same(got, ew, eh, want));")
w("    assert(same(img, sw, sh, src));")
w("}")
w("")

# crop
w("fn check_crop() {")
for (sw, sh) in [(1, 1), (4, 3), (5, 5), (2, 6)]:
    im = rand_image(sw, sh)
    boxes = [(0, 0, sw, sh), (1, 0, sw, sh), (0, 0, 1, 1)]
    for _ in range(14):
        l = rng.randrange(-3, sw + 2)
        t = rng.randrange(-3, sh + 2)
        r = l + rng.randrange(1, sw + 4)
        b = t + rng.randrange(1, sh + 4)
        boxes.append((l, t, r, b))
    for box in boxes:
        # A box without area is a contract case. Pillow gives a side of 0 there.
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        c = im.crop(box)
        w("    crop_case(%d, %d, \"%s\", %d, %d, %d, %d, %d, %d, \"%s\");" % (
            sw, sh, hx(im), box[0], box[1], box[2], box[3], c.size[0], c.size[1], hx(c)))
        count["crop"] += 1
w("}")
w("")

# transpose
ops = [("FLIP_LEFT_RIGHT", 0), ("FLIP_TOP_BOTTOM", 1), ("ROTATE_90", 2),
       ("ROTATE_180", 3), ("ROTATE_270", 4), ("TRANSPOSE", 5), ("TRANSVERSE", 6)]
w("fn check_transpose() {")
for (sw, sh) in [(1, 1), (3, 2), (2, 3), (4, 4), (5, 1), (1, 5)]:
    im = rand_image(sw, sh)
    for name, n in ops:
        t = im.transpose(getattr(Image.Transpose, name))
        w("    transpose_case(%d, %d, \"%s\", %d, %d, %d, \"%s\");" % (
            sw, sh, hx(im), n, t.size[0], t.size[1], hx(t)))
        count["transpose"] += 1
w("}")
w("")


def clip(dw, dh, sw, sh, x, y):
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(dw, x + sw), min(dh, y + sh)
    return x0, y0, x1, y1


def do_paste(dst, src, x, y, blend):
    d = dst.copy()
    x0, y0, x1, y1 = clip(d.size[0], d.size[1], src.size[0], src.size[1], x, y)
    if x1 <= x0 or y1 <= y0:
        return d
    part = src.crop((x0 - x, y0 - y, x1 - x, y1 - y))
    if blend:
        under = d.crop((x0, y0, x1, y1))
        part = Image.alpha_composite(under, part)
    d.paste(part, (x0, y0))
    return d


# paste and blend
w("fn check_paste() {")
for _ in range(30):
    dw, dh = rng.randrange(1, 7), rng.randrange(1, 7)
    sw, sh = rng.randrange(1, 5), rng.randrange(1, 5)
    d, s = rand_image(dw, dh), rand_image(sw, sh)
    x, y = rng.randrange(-4, dw + 3), rng.randrange(-4, dh + 3)
    r = do_paste(d, s, x, y, False)
    w("    paste_case(%d, %d, \"%s\", %d, %d, \"%s\", %d, %d, \"%s\");" % (
        dw, dh, hx(d), sw, sh, hx(s), x, y, hx(r)))
    count["paste"] += 1
w("}")
w("")
w("fn check_blend() {")
for _ in range(40):
    dw, dh = rng.randrange(1, 7), rng.randrange(1, 7)
    sw, sh = rng.randrange(1, 5), rng.randrange(1, 5)
    d, s = rand_image(dw, dh), rand_image(sw, sh)
    x, y = rng.randrange(-4, dw + 3), rng.randrange(-4, dh + 3)
    r = do_paste(d, s, x, y, True)
    w("    blend_case(%d, %d, \"%s\", %d, %d, \"%s\", %d, %d, \"%s\");" % (
        dw, dh, hx(d), sw, sh, hx(s), x, y, hx(r)))
    count["blend"] += 1
# A grid of alpha pairs. The source alpha runs along x, the destination along y.
n = 32
d = Image.new("RGBA", (n, n))
s = Image.new("RGBA", (n, n))
d.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256), min(255, y * 8 + (y // 31) * 7))
           for y in range(n) for x in range(n)])
s.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256), min(255, x * 8 + (x // 31) * 7))
           for y in range(n) for x in range(n)])
r = Image.alpha_composite(d, s)
w("    blend_case(%d, %d, \"%s\", %d, %d, \"%s\", 0, 0, \"%s\");" % (
    n, n, hx(d), n, n, hx(s), hx(r)))
count["blend"] += 1
w("}")
w("")

# rotate
angles = [float(a) for a in range(-360, 361, 15)] + [10.0, 33.0, 77.0, 123.5, 200.0, -45.5, 0.5]
w("fn check_rotate() {")
for (sw, sh) in [(3, 2), (4, 4), (5, 3)]:
    im = rand_image(sw, sh)
    for a in angles:
        for ex in (False, True):
            r = im.rotate(a, resample=Image.Resampling.NEAREST, expand=ex)
            w("    rotate_case(%d, %d, \"%s\", %s, %s, %d, %d, \"%s\");" % (
                sw, sh, hx(im), repr(a), "true" if ex else "false",
                r.size[0], r.size[1], hx(r)))
            count["rotate"] += 1
w("}")

sys.stdout.write("\n".join(out) + "\n")
sys.stderr.write("cases: %s total=%d\n" % (count, sum(count.values())))
