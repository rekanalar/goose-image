#!/usr/bin/env python3
"""Make test/jpeg_cases.goose with Pillow as the oracle.

Usage (in the package directory, with goose on PATH and TMPDIR set):
    python3 test/make_jpeg_cases.py > test/jpeg_cases.goose

The script makes the source images, lets the package write each JPEG, reads
the bytes back with Pillow and checks them: the pixels against the source,
the progressive flag, and the subsampling. It then writes the Pillow result
into the case file. The Goose test encodes again, so the case file holds the
expected flags and the expected pixels that Pillow decoded.
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile
import warnings

import PIL
from PIL import Image, JpegImagePlugin

warnings.simplefilter("ignore")
rng = random.Random(20261107)
pkg = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def grad(w, h):
    return [((x * 255 // max(1, w - 1)), (y * 255 // max(1, h - 1)), ((x * y) * 3) % 256, 255)
            for y in range(h) for x in range(w)]


def noise(w, h):
    return [(rng.randrange(256), rng.randrange(256), rng.randrange(256), 255) for _ in range(w * h)]


def edges(w, h):
    px = []
    for y in range(h):
        for x in range(w):
            on = ((x // 3) + (y // 2)) % 2 == 0
            px.append((250, 20, 30, 255) if on else (10, 200, 240, 255))
    return px


def flat(w, h):
    return [(200, 120, 40, 255)] * (w * h)


sources = [
    ("one", 1, 1, [(10, 200, 90, 255)]),
    ("tiny", 7, 5, grad(7, 5)),
    ("flat", 8, 8, flat(8, 8)),
    ("grad16", 16, 16, grad(16, 16)),
    ("odd", 17, 9, grad(17, 9)),
    ("edges", 40, 16, edges(40, 16)),
    ("noise", 24, 20, noise(24, 20)),
    ("big", 33, 31, grad(33, 31)),
    # A wide image: many AC blocks, so the end-of-band runs get long.
    ("wide", 200, 9, grad(200, 9)),
]
qualities = [90, 50, 100, 10, 75]
cases = []  # (src index, quality, progressive, subsampling)
for i in range(len(sources)):
    for sub in range(3):
        for prog in (False, True):
            cases.append((i, qualities[(i + sub) % len(qualities)], prog, sub))


def hx(px, n):
    return "".join("%02x" % c for p in px for c in p[:n])


work = tempfile.mkdtemp(dir=os.environ["TMPDIR"], prefix="jpegcases-")
lines = ["import std;", "import os;", 'import image from "%s";' % pkg, "fn hexval(c: u8) -> i64 {",
         "    if c >= 97 { return c - 87; }", "    return c - 48;", "}",
         "fn mk(w: i32, h: i32, s: u8[:]) -> image::Image {", "    var px: u8[>..] = [];", "    var i = 0;",
         "    while i + 1 < s.len {", "        px.push((hexval(s[i]) * 16 + hexval(s[i + 1])) as u8);", "        i += 2;",
         "    }", "    return image::Image { width: w, height: h, pixels: px[..], icc: [], exif: [] };", "}",
         "fn main() {"]
for n, (i, q, prog, sub) in enumerate(cases):
    _, w, h, px = sources[i]
    lines.append('    var a%d = mk(%d, %d, "%s");' % (n, w, h, hx(px, 4)))
    lines.append('    assert(image::write_jpeg_with("%s/c%d.jpg", a%d, %d, %s, %d));' % (
        work, n, n, q, "true" if prog else "false", sub))
    lines.append('    assert(image::write_jpeg_opts("%s/o%d.jpg", a%d, %d, %s, %d, true));' % (
        work, n, n, q, "true" if prog else "false", sub))
lines += ["    return 0;", "}"]
open(os.path.join(work, "driver.goose"), "w").write("\n".join(lines) + "\n")
subprocess.run([shutil.which("goose"), "run", "driver.goose"], cwd=work, check=True)

out = []
w_ = out.append
w_("// Cases made by test/make_jpeg_cases.py with Pillow %s." % PIL.__version__)
w_("// Do not edit by hand. Make the file again with the script.")
w_("")
w_("namespace jpeg_cases;")
w_("")
w_("import std;")
w_("import os;")
w_('import image from "..";')
w_("""
fn hexval(c: u8) -> i64 {
    if c >= 97 { return c - 87; }
    return c - 48;
}

fn bytes_of(s: u8[:]) -> u8[] {
    var px: u8[>..] = [];
    var i = 0;
    while i + 1 < s.len {
        px.push((hexval(s[i]) * 16 + hexval(s[i + 1])) as u8);
        i += 2;
    }
    return px[..];
}

fn from_hex(w: i32, h: i32, s: u8[:]) -> image::Image {
    return image::Image { width: w, height: h, pixels: bytes_of(s), icc: [], exif: [] };
}

// sof_at gives the byte at the offset from the start of the SOF marker, or
// 0 when the bytes hold no SOF marker. The offset 1 is the marker type.
fn sof_at(b: u8[:], off: i64) -> i64 {
    var i: i64 = 2;
    while i + 4 < b.len {
        if b[i] != 255 { return 0; }
        let m = b[i + 1];
        if m == 0xc0 || m == 0xc2 {
            if i + off < b.len { return b[i + off]; }
            return 0;
        }
        let hi: i64 = b[i + 2];
        i += 2 + hi * 256 + b[i + 3];
    }
    return 0;
}

// gap gives the largest and the mean difference between the RGBA pixels and
// the RGB string. The mean is times 1000. It gives -1, -1 for a bad size.
fn gap(img: image::Image&, w: i32, h: i32, rgb: u8[:]) -> i64, i64 {
    if img.width != w || img.height != h { return -1, -1; }
    let want = bytes_of(rgb);
    var worst: i64 = 0;
    var sum: i64 = 0;
    var i: i64 = 0;
    while i < w * h {
        for c in 0..3 {
            var d: i64 = img.pixels[i * 4 + c] as i64 - want[i * 3 + c];
            if d < 0 { d = -d; }
            if d > worst { worst = d; }
            sum += d;
        }
        i += 1;
    }
    return worst, sum * 1000 / (w * h * 3);
}

// jpeg_case encodes the source, checks the marker and the sampling bytes
// that Pillow reports, and checks the pixels that Pillow decoded.
fn jpeg_case(w: i32, h: i32, src: u8[:], q: i32, prog: bool, sub: i32,
    sof: i64, samp: i64, want: u8[:]) {
    let img = from_hex(w, h, src);
    let bytes = image::encode_jpeg(img, q, prog, sub);
    assert(bytes.len > 4);
    assert(bytes[0] == 0xff && bytes[1] == 0xd8);
    assert(bytes[bytes.len - 2] == 0xff && bytes[bytes.len - 1] == 0xd9);
    assert(sof_at(bytes, 1) == sof);
    assert(sof_at(bytes, 11) == samp);
    let back = image::decode(bytes);
    // The decoder of the package (stb) upsamples the 4:2:2 chroma with a
    // different filter than libjpeg: on a 4:2:2 file that Pillow itself wrote,
    // the two decoders differ by up to 23 levels. So 4:2:2 gets a loose bound.
    let worst, mean = gap(back, w, h, want);
    assert(worst >= 0);
    if sub == 0 { assert(worst <= 8); }
    if sub == 2 { assert(worst <= 12); }
    if sub == 1 { assert(worst <= 110 && mean <= 2500); }
    // The two forms hold the same coefficients, so the pixels are the same.
    let other = image::decode(image::encode_jpeg(img, q, !prog, sub));
    assert(back.pixels == other.pixels);
    let meta = image::metadata_bytes(bytes);
    assert(meta.ok && meta.width == w && meta.height == h && meta.kind == image::KIND_JPEG);
    // The tables of the scan give a smaller file with the same pixels.
    let opt = image::encode_jpeg_opts(img, q, prog, sub, true);
    assert(opt.len > 4);
    assert(opt.len <= bytes.len);
    assert(sof_at(opt, 1) == sof);
    assert(sof_at(opt, 11) == samp);
    let oback = image::decode(opt);
    assert(oback.pixels == back.pixels);
    let ometa = image::metadata_bytes(opt);
    assert(ometa.ok && ometa.width == w && ometa.height == h && ometa.kind == image::KIND_JPEG);
}
""")
w_("fn check_jpeg() {")
errs = []
for n, (i, q, prog, sub) in enumerate(cases):
    name, w, h, px = sources[i]
    im = Image.open(os.path.join(work, "c%d.jpg" % n))
    im.load()
    # The flags that Pillow reports.
    is_prog = bool(im.info.get("progressive") or im.info.get("progression"))
    assert is_prog == prog, (n, "progressive")
    assert JpegImagePlugin.get_sampling(im) == sub, (n, "sampling", JpegImagePlugin.get_sampling(im))
    assert im.size == (w, h) and im.mode == "RGB"
    rgb = list(im.convert("RGB").getdata())
    # The error against the source: a lossy codec, so a bound by the quality.
    err = sum(abs(a - b) for p, r in zip(px, rgb) for a, b in zip(p[:3], r)) / (3 * w * h)
    # Pillow (libjpeg) encodes the same source with the same options. Our
    # error must stay near its error.
    src = Image.new("RGB", (w, h))
    src.putdata([p_[:3] for p_ in px])
    ref = os.path.join(work, "ref%d.jpg" % n)
    src.save(ref, quality=q, progressive=prog, subsampling=sub)
    rim = Image.open(ref)
    rrgb = list(rim.convert("RGB").getdata())
    rerr = sum(abs(a - b) for p, r in zip(px, rrgb) for a, b in zip(p[:3], r)) / (3 * w * h)
    assert err <= rerr * 1.5 + 3, (n, name, q, sub, err, rerr)
    # The file with the tables of the scan: Pillow reads it, the pixels are
    # the same and the file is not bigger.
    oim = Image.open(os.path.join(work, "o%d.jpg" % n))
    oim.load()
    assert oim.size == (w, h) and oim.mode == "RGB"
    assert bool(oim.info.get("progressive") or oim.info.get("progression")) == prog, (n, "progressive")
    assert JpegImagePlugin.get_sampling(oim) == sub, (n, "sampling")
    assert list(oim.convert("RGB").getdata()) == rgb, (n, "pixels")
    assert os.path.getsize(os.path.join(work, "o%d.jpg" % n)) <= os.path.getsize(
        os.path.join(work, "c%d.jpg" % n)), (n, "size")
    sof = 0xC2 if is_prog else 0xC0
    samp = im.layer[0][1] << 4 | im.layer[0][2]
    w_('    jpeg_case(%d, %d, "%s", %d, %s, %d, %d, %d, "%s");' % (
        w, h, hx(px, 4), q, "true" if prog else "false", sub, sof, samp, hx(rgb, 3)))
w_("}")
w_("""
fn all_gray(w: i32, h: i32) -> image::Image {
    return image::new_image(w, h, 90, 90, 90, 255);
}

// check_jpeg_hostile: a bad size or a bad option never aborts and gives the
// empty result.
fn check_jpeg_hostile() {
    let ok = all_gray(9, 7);
    assert(image::encode_jpeg(ok, 90, true, 3).len == 0);
    assert(image::encode_jpeg(ok, 90, false, -1).len == 0);
    // A quality of 0 means 90. A quality above 100 counts as 100.
    assert(image::encode_jpeg(ok, 0, false, 2) == image::encode_jpeg(ok, 90, false, 2));
    assert(image::encode_jpeg(ok, 500, true, 0) == image::encode_jpeg(ok, 100, true, 0));
    // An empty image, a negative side, a size that does not match the bytes.
    let empty = image::Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    assert(image::encode_jpeg(empty, 90, false, 0).len == 0);
    assert(image::encode_jpeg(empty, 90, true, 2).len == 0);
    let neg = image::Image { width: -5, height: 3, pixels: [1, 2, 3, 4], icc: [], exif: [] };
    assert(image::encode_jpeg(neg, 90, false, 0).len == 0);
    let huge = image::Image { width: 100000, height: 100000, pixels: [1, 2, 3, 4], icc: [], exif: [] };
    assert(image::encode_jpeg(huge, 90, true, 2).len == 0);
    let short = image::Image { width: 5, height: 5, pixels: [1, 2, 3, 4], icc: [], exif: [] };
    assert(image::encode_jpeg(short, 90, false, 1).len == 0);
    assert(image::encode_jpeg(short, 90, true, 1).len == 0);
    // The JPEG limit is 65535 on a side. The image has a matching buffer.
    let wide = all_gray(65536, 1);
    assert(image::encode_jpeg(wide, 90, false, 2).len == 0);
    let edge = all_gray(65535, 1);
    let eb = image::encode_jpeg(edge, 90, true, 2);
    assert(eb.len > 0);
    let em = image::metadata_bytes(eb);
    assert(em.ok && em.width == 65535 && em.height == 1);
    // A write with a bad option gives false and makes no file.
    assert(!image::write_jpeg_with("jpeg-hostile.jpg", ok, 90, false, 7));
    assert(!image::write_jpeg_with("jpeg-hostile.jpg", empty, 90, true, 0));
    // The calls with the tables of the scan take the same options.
    assert(image::encode_jpeg_opts(ok, 90, true, 3, true).len == 0);
    assert(image::encode_jpeg_opts(ok, 90, false, -1, true).len == 0);
    assert(image::encode_jpeg_opts(empty, 90, false, 0, true).len == 0);
    assert(image::encode_jpeg_opts(neg, 90, false, 0, true).len == 0);
    assert(image::encode_jpeg_opts(huge, 90, true, 2, true).len == 0);
    assert(image::encode_jpeg_opts(short, 90, false, 1, true).len == 0);
    assert(!image::write_jpeg_opts("jpeg-hostile.jpg", ok, 90, false, 7, true));
    assert(!image::write_jpeg_opts("jpeg-hostile.jpg", empty, 90, true, 0, true));
    // The tables of the scan do not change the pixels.
    let plain = image::encode_jpeg(ok, 75, false, 0);
    let opt = image::encode_jpeg_opts(ok, 75, false, 0, true);
    assert(opt.len > 0 && opt.len <= plain.len);
    assert(image::decode(opt).pixels == image::decode(plain).pixels);
}
""")
sys.stdout.write("\n".join(out) + "\n")
sys.stderr.write("cases: %d\n" % len(cases))
shutil.rmtree(work)
