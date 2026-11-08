#!/usr/bin/env python3
"""Make test/gif_cases.goose with Pillow as the oracle.

Run: python3 test/make_gif_cases.py > test/gif_cases.goose
Each case is a GIF that Pillow writes. The expected frames are what Pillow
reads back, as full RGBA canvases.

The writer of the package is the oracle for the cases of the quantizer. The
script runs the package, reads the files that it writes and measures the
error of the colors against the source. The error of Pillow is the bound.
"""
import io
import os
import random
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageSequence

rng = random.Random(20261107)
pkg = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = []
count = 0


def w(s):
    out.append(s)


def hx(b):
    return b.hex()


def frames_of(data):
    im = Image.open(io.BytesIO(data))
    res = []
    for fr in ImageSequence.Iterator(im):
        # Pillow keeps the delay and the disposal of the frame in its info.
        res.append((fr.convert("RGBA").tobytes(), fr.info.get("duration", 0) // 10,
                    getattr(fr, "disposal_method", 0)))
    return im.size, im.info.get("loop", -1), res


def palette_frame(wd, ht, colors):
    im = Image.new("RGB", (wd, ht))
    im.putdata([colors[rng.randrange(len(colors))] for _ in range(wd * ht)])
    return im


def rand_color():
    return (rng.randrange(256), rng.randrange(256), rng.randrange(256))


def make(frames, **kw):
    buf = io.BytesIO()
    frames[0].save(buf, "GIF", save_all=len(frames) > 1, append_images=frames[1:], **kw)
    return buf.getvalue()


def emit(name, data, pixels=True):
    global count
    size, loop, fr = frames_of(data)
    import os
    d = os.environ.get('GIF_DUMP')
    if d:
        open('%s/%03d.gif' % (d, count), 'wb').write(data)
    w("    gif_case(\"%s\", %d, %d, %d, %d);" % (hx(data), len(fr), size[0], size[1], loop))
    for i, (px, delay, disp) in enumerate(fr):
        w("    frame_case(\"%s\", %d, %d, %d, \"%s\");" % (hx(data), i, delay, disp, hx(px) if pixels else ""))
        count += 1
    count += 1


# ---- The quantizer ----
#
# The images below hold more colors than the table of a GIF. The writer must
# cut them to 256 colors. The error of the colors must stay near the error of
# Pillow.

def grad_source(wd, ht):
    return [(x * 255 // max(1, wd - 1), y * 255 // max(1, ht - 1), (x * y) % 256, 255)
            for y in range(ht) for x in range(wd)]


def noise_source(wd, ht):
    return [(rng.randrange(256), rng.randrange(256), rng.randrange(256), 255)
            for _ in range(wd * ht)]


def cluster_source(wd, ht):
    px = []
    for y in range(ht):
        for x in range(wd):
            c = [(10, 10, 10), (250, 250, 250), (120, 30, 200)][(x + y) % 3]
            px.append((min(255, c[0] + rng.randrange(-3, 4)),
                       min(255, c[1] + rng.randrange(-3, 4)),
                       min(255, c[2] + rng.randrange(-3, 4)), 255))
    return px


def alpha_source(wd, ht):
    # A part of the image is clear. The writer gives it a clear index.
    px = grad_source(wd, ht)
    for y in range(ht // 3, 2 * ht // 3):
        for x in range(wd // 3, 2 * wd // 3):
            r, g, b, _ = px[y * wd + x]
            px[y * wd + x] = (r, g, b, 0)
    return px


def px_bytes(px):
    """The bytes of the pixels, four for each one."""
    return bytes(b for p in px for b in p)


def mean_error(src, got):
    """The mean difference of the colors, times 1000. The clear pixels of the
    source do not count."""
    total = 0
    n = 0
    for a, b in zip(src, got):
        if a[3] < 128:
            continue
        for c in range(3):
            total += abs(a[c] - b[c])
            n += 1
    if not n:
        return 0
    return total * 1000 // n


def pillow_error(src, wd, ht):
    """The error of Pillow when it cuts the image to 256 colors."""
    im = Image.new("RGB", (wd, ht))
    im.putdata([p[:3] for p in src])
    q = im.quantize(256)
    return mean_error(src, list(q.convert("RGBA").getdata()))


def pillow_gif_error(src, wd, ht):
    """The error of the GIF that Pillow writes."""
    im = Image.new("RGBA", (wd, ht))
    im.putdata(src)
    buf = io.BytesIO()
    im.save(buf, "GIF")
    got = Image.open(io.BytesIO(buf.getvalue())).convert("RGBA")
    return mean_error(src, list(got.getdata()))


def write_driver(work, jobs):
    """Make a program that writes a GIF for every job and run it."""
    lines = ["import std;", "import os;", 'import image from "%s";' % pkg,
             "fn hexval(c: u8) -> i64 {", "    if c >= 97 { return c - 87; }", "    return c - 48; }",
             "fn bytes_of(s: u8[:]) -> u8[] {", "    var px: u8[>..] = [];", "    var i = 0;",
             "    while i + 1 < s.len {", "        px.push((hexval(s[i]) * 16 + hexval(s[i + 1])) as u8);",
             "        i += 2;", "    }", "    return px[..];", "}", "fn main() {"]
    for name, wd, ht, frames in jobs:
        for k, px in enumerate(frames):
            lines.append('    let p%d = bytes_of("%s");' % (k, px_bytes(px).hex()))
            lines.append('    let i%d = image::Image { width: %d, height: %d, pixels: copy(p%d), icc: [], exif: [] };'
                         % (k, wd, ht, k))
        lines.append("    var fs: image::GifFrame[>..] = [];")
        for k in range(len(frames)):
            lines.append('    fs.push(image::GifFrame { image: copy(i%d), delay: 10, disposal: 0 });' % k)
        lines.append('    let g = image::Gif { frames: copy(fs), width: %d, height: %d, loop_count: 0 };' % (wd, ht))
        lines.append('    assert(write_file("%s/%s.gif", image::encode_gif(g)));' % (work, name))
    lines.append("    return 0;")
    lines.append("}")
    open(os.path.join(work, "driver.goose"), "w").write("\n".join(lines) + "\n")
    subprocess.run([shutil.which("goose"), "run", "driver.goose"], cwd=work, check=True)


w("// Cases made by test/make_gif_cases.py with Pillow %s." % Image.__version__)
w("// Do not edit by hand. Make the file again with the script.")
w("")
w("namespace gif_cases;")
w("")
w("import std;")
w("import image from \"..\";")
w("")
w("fn hexval(c: u8) -> i64 {")
w("    if c >= 97 { return c - 87; }")
w("    return c - 48;")
w("}")
w("")
w("fn from_hex(s: u8[:]) -> u8[] {")
w("    var b: u8[>..] = [];")
w("    var i = 0;")
w("    while i + 1 < s.len {")
w("        let v: i64 = hexval(s[i]) * 16 + hexval(s[i + 1]);")
w("        b.push(v as u8);")
w("        i += 2;")
w("    }")
w("    return b[..];")
w("}")
w("")
w("fn gif_case(file: u8[:], n: i64, gw: i32, gh: i32, loops: i32) {")
w("    let g = image::decode_gif(from_hex(file));")
w("    assert(g.frames.len == n);")
w("    assert(g.width == gw && g.height == gh);")
w("    assert(g.loop_count == loops);")
w("}")
w("")
w("// frame_at gives a copy of frame i. Frames cannot be indexed.")
w("fn frame_at(g: image::Gif&, i: i64) -> image::GifFrame {")
w("    var k = 0;")
w("    for f in g.frames {")
w("        if k == i { return copy(f); }")
w("        k += 1;")
w("    }")
w("    assert(false);")
w("    let e = image::Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };")
w("    return image::GifFrame { image: e, delay: 0, disposal: 0 };")
w("}")
w("")
w("fn frame_case(file: u8[:], i: i64, delay: i32, disp: i32, want: u8[:]) {")
w("    let g = image::decode_gif(from_hex(file));")
w("    let f = frame_at(g, i);")
w("    assert(f.delay == delay);")
w("    assert(f.disposal == disp);")
w("    assert(f.image.width == g.width);")
w("    assert(f.image.height == g.height);")
w("    // An empty string means that the pixels are checked elsewhere.")
w("    assert(want.len == 0 || f.image.pixels == from_hex(want));")
w("}")
w("")
w("// gif_quant_case writes the image as a GIF and reads it back. The mean")
w("// error of the colors must stay under the bound. The clear pixels of the")
w("// source do not count.")
w("fn gif_quant_case(w: i32, h: i32, src: u8[:], bound: i64) {")
w("    let want = from_hex(src);")
w("    let img = image::Image { width: w, height: h, pixels: copy(want), icc: [], exif: [] };")
w("    var fs: image::GifFrame[>..] = [];")
w("    fs.push(image::GifFrame { image: copy(img), delay: 0, disposal: 0 });")
w("    let g = image::Gif { frames: copy(fs), width: w, height: h, loop_count: 0 };")
w("    let bytes = image::encode_gif(g);")
w("    assert(bytes.len > 0);")
w("    let back = image::decode_gif(bytes);")
w("    assert(back.frames.len == 1);")
w("    let fr = frame_at(back, 0);")
w("    let got = fr.image.pixels;")
w("    var sum: i64 = 0;")
w("    var n: i64 = 0;")
w("    var i: i64 = 0;")
w("    while i < w * h {")
w("        if want[i * 4 + 3] >= 128 {")
w("            for c in 0..3 {")
w("                var d = got[i * 4 + c] as i64 - want[i * 4 + c] as i64;")
w("                if d < 0 { d = -d; }")
w("                sum += d;")
w("                n += 1;")
w("            }")
w("        }")
w("        i += 1;")
w("    }")
w("    assert(n > 0);")
w("    assert(sum * 1000 / n <= bound);")
w("}")
w("")
w("// gif_anim_case writes the frames as a GIF and reads them back. The mean")
w("// error of the colors of every frame must stay under the bound. The")
w("// frames follow each other in the bytes.")
w("fn gif_anim_case(w: i32, h: i32, n: i64, src: u8[:], bound: i64) {")
w("    let want = from_hex(src);")
w("    var fs: image::GifFrame[>..] = [];")
w("    var k = 0;")
w("    while k < n {")
w("        var px: u8[>..] = [];")
w("        px.append(want[k * w * h * 4..(k + 1) * w * h * 4]);")
w("        let img = image::Image { width: w, height: h, pixels: px[..], icc: [], exif: [] };")
w("        fs.push(image::GifFrame { image: copy(img), delay: 10, disposal: 0 });")
w("        k += 1;")
w("    }")
w("    let g = image::Gif { frames: copy(fs), width: w, height: h, loop_count: 0 };")
w("    let bytes = image::encode_gif(g);")
w("    assert(bytes.len > 0);")
w("    let back = image::decode_gif(bytes);")
w("    assert(back.frames.len == n);")
w("    var f = 0;")
w("    while f < n {")
w("        let fr = frame_at(back, f);")
w("        let got = fr.image.pixels;")
w("        assert(fr.delay == 10);")
w("        var sum: i64 = 0;")
w("        var cnt: i64 = 0;")
w("        var i: i64 = 0;")
w("        while i < w * h {")
w("            if want[(f * w * h + i) * 4 + 3] >= 128 {")
w("                for c in 0..3 {")
w("                    var d = got[i * 4 + c] as i64 - want[(f * w * h + i) * 4 + c] as i64;")
w("                    if d < 0 { d = -d; }")
w("                    sum += d;")
w("                    cnt += 1;")
w("                }")
w("            }")
w("            i += 1;")
w("        }")
w("        assert(cnt > 0);")
w("        assert(sum * 1000 / cnt <= bound);")
w("        f += 1;")
w("    }")
w("}")
w("")

w("fn check_gif() {")
# single frame, sizes that exercise the interlace passes and the code widths
for (wd, ht) in [(1, 1), (2, 2), (8, 6), (17, 9), (3, 40), (40, 3), (64, 48)]:
    cols = [rand_color() for _ in range(rng.choice([2, 3, 5, 16, 100, 255]))]
    f = palette_frame(wd, ht, cols)
    emit("single", make([f]))
    emit("single-noninterlaced", make([f], interlace=False))
# many frames with delays and loop counts
for nf, loops in [(3, 0), (5, 3), (2, 1), (12, 0)]:
    cols = [rand_color() for _ in range(8)]
    fr = [palette_frame(9, 7, cols) for _ in range(nf)]
    dur = [rng.randrange(1, 60) * 10 for _ in range(nf)]
    emit("many", make(fr, duration=dur, loop=loops, optimize=False))
# frames with different colors: local palettes
fr = [palette_frame(10, 10, [rand_color() for _ in range(rng.choice([4, 30, 200]))]) for _ in range(4)]
emit("local", make(fr, duration=[70, 80, 90, 100], loop=0))
# disposal methods
for d in (0, 1, 2, 3):
    cols = [rand_color() for _ in range(6)]
    fr = [palette_frame(12, 8, cols) for _ in range(3)]
    # Pillow shows the frame before a disposal 3 frame wrongly when the first frame
    # has disposal 3, so its pixels are checked by a hand case in run.goose.
    emit("disposal", make(fr, duration=50, loop=0, disposal=d), pixels=d != 3)
# a long run that fills the LZW table
im = Image.new("RGB", (120, 90))
im.putdata([rand_color() if rng.random() < 0.5 else (1, 2, 3) for _ in range(120 * 90)])
emit("bigtable", make([im.quantize(255)]))

# ---- the cases of the quantizer and of the frames that change ----
#
# The writer of the package writes the files. Pillow reads them and measures
# the error of the colors. The error of Pillow gives the bound.

work = tempfile.mkdtemp(dir=os.environ["TMPDIR"], prefix="gifcases-")
sources = [
    ("grad", 48, 40, grad_source(48, 40)),
    ("noise", 48, 40, noise_source(48, 40)),
    ("cluster", 48, 40, cluster_source(48, 40)),
    ("alpha", 48, 40, alpha_source(48, 40)),
]
# An animation of three frames. The last frames change a small part of the
# canvas only.
base = grad_source(32, 32)
second = list(base)
for y in range(4, 12):
    for x in range(2, 10):
        second[y * 32 + x] = (250, 10, 10, 255)
third = list(second)
for y in range(20, 28):
    for x in range(20, 30):
        third[y * 32 + x] = (10, 250, 10, 255)
anims = [("anim", 32, 32, [base, second, third])]
write_driver(work, [(n, wd, ht, [px]) for n, wd, ht, px in sources] + anims)

for name, wd, ht, px in sources:
    got = Image.open(os.path.join(work, name + ".gif")).convert("RGBA")
    ours = mean_error(px, list(got.getdata()))
    theirs = pillow_gif_error(px, wd, ht)
    quant = pillow_error(px, wd, ht)
    bound = max(1, theirs * 13 // 10) + 2
    sys.stderr.write("%-8s ours=%5d pillow_gif=%5d pillow_quantize=%5d bound=%5d\n"
                     % (name, ours, theirs, quant, bound))
    assert ours <= bound, (name, ours, bound)
    # The error of the quantizer of Pillow is the other bound.
    assert ours <= max(1, quant * 13 // 10) + 2, (name, ours, quant)
    w('    gif_quant_case(%d, %d, "%s", %d);' % (wd, ht, px_bytes(px).hex(), bound))

for name, wd, ht, frames in anims:
    got = Image.open(os.path.join(work, name + ".gif"))
    ours = 0
    theirs = 0
    for k, fr in enumerate(ImageSequence.Iterator(got)):
        e = mean_error(frames[k], list(fr.convert("RGBA").getdata()))
        ours = max(ours, e)
    for k, fr in enumerate(frames):
        e = pillow_gif_error(fr, wd, ht)
        theirs = max(theirs, e)
    bound = max(1, theirs * 13 // 10) + 2
    sys.stderr.write("%-8s ours=%5d pillow=%5d bound=%5d\n" % (name, ours, theirs, bound))
    assert ours <= bound, (name, ours, bound)
    allpx = b"".join(px_bytes(f) for f in frames)
    w('    gif_anim_case(%d, %d, %d, "%s", %d);' % (wd, ht, len(frames), allpx.hex(), bound))

shutil.rmtree(work)
w("}")

sys.stdout.write("\n".join(out) + "\n")
sys.stderr.write("cases: %d\n" % count)
