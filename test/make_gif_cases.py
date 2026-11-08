#!/usr/bin/env python3
"""Make test/gif_cases.goose with Pillow as the oracle.

Run: python3 test/make_gif_cases.py > test/gif_cases.goose
Each case is a GIF that Pillow writes. The expected frames are what Pillow
reads back, as full RGBA canvases.
"""
import io
import random
import sys

from PIL import Image, ImageSequence

rng = random.Random(20261108)
out = []
count = 0


def w(s):
    out.append(s)


def hx(b):
    return "".join("\\x%02x" % c for c in b) if False else b.hex()


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
w("}")

sys.stdout.write("\n".join(out) + "\n")
sys.stderr.write("cases: %d\n" % count)
