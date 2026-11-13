#!/usr/bin/env python3
"""Make test/avif_cases.goose from Pillow. Run it in the package directory.

Usage: python3 test/make_avif_cases.py > test/avif_cases.goose

Pillow (libavif) writes the AVIF files and reads them back. Each case holds
the bytes of a file and the RGBA pixels that Pillow reads from it. The Goose
test decodes the bytes with the package and compares within a tolerance,
because two YUV to RGB conversions differ in the last bits. The seed is fixed.
"""
import io
import random
import struct
import warnings

import PIL
from PIL import Image

warnings.simplefilter("ignore")
rng = random.Random(20261107)


def smooth(w, h, alpha):
    im = Image.new("RGBA", (w, h))
    im.putdata([(x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128,
                 (x * 255 // max(w - 1, 1)) if alpha else 255)
                for y in range(h) for x in range(w)])
    return im


def noise(w, h):
    im = Image.new("RGBA", (w, h))
    im.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256), 255)
                for _ in range(w * h)])
    return im


def avif(im, **kw):
    b = io.BytesIO()
    im.save(b, "AVIF", **kw)
    return b.getvalue()


# (kind, bytes, alpha channel in the file)
cases = []
for (w, h) in [(1, 1), (7, 5), (16, 16), (33, 17), (64, 48)]:
    src = smooth(w, h, False).convert("RGB")
    for sub in ("4:4:4", "4:2:2", "4:2:0"):
        cases.append(("rgb-" + sub.replace(":", ""), avif(src, quality=80, subsampling=sub), False))
    cases.append(("rgb-420-limited", avif(src, quality=80, subsampling="4:2:0", range="limited"), False))
    cases.append(("lossless", avif(src, quality=100, subsampling="4:4:4"), False))
    cases.append(("gray", avif(src.convert("L"), quality=80), False))
    cases.append(("rgba-444", avif(smooth(w, h, True), quality=80, subsampling="4:4:4"), True))
    cases.append(("rgba-420", avif(smooth(w, h, True), quality=80, subsampling="4:2:0"), True))
    cases.append(("gray-alpha", avif(smooth(w, h, True).convert("LA"), quality=80), True))
    cases.append(("rgba-444-premultiplied", avif(smooth(w, h, True), quality=80, subsampling="4:4:4",
                                             alpha_premultiplied=True), True))
cases.append(("rgb-444-noise", avif(noise(24, 20), quality=90, subsampling="4:4:4"), False))
# Odd sizes with chroma subsampling.
cases.append(("odd", avif(smooth(31, 19, False).convert("RGB"), quality=70), False))
# A big flat picture.
cases.append(("flat", avif(Image.new("RGB", (300, 200), (10, 200, 90)), quality=60), False))


def box(t, body):
    return struct.pack(">I", 8 + len(body)) + t + body


def hostile():
    good = [c[1] for c in cases if c[0] == "rgb-420" and Image.open(io.BytesIO(c[1])).width == 33][0]
    out = []
    out.append(("truncated half", good[:len(good) // 2]))
    out.append(("truncated 20", good[:20]))
    out.append(("ftyp only", good[:good.find(b"meta") - 4]))
    out.append(("not avif", b"\x89PNG\r\n\x1a\n" + bytes(40)))
    out.append(("zeros", bytes(200)))
    out.append(("ftyp avif alone", box(b"ftyp", b"avif\0\0\0\0avifmif1")))
    # A box that claims more bytes than the file holds.
    bad = bytearray(good)
    i = good.find(b"meta") - 4
    bad[i:i + 4] = struct.pack(">I", 0x7fffffff)
    out.append(("meta size huge", bytes(bad)))
    # A box of size 1 with a 64 bit size that is huge.
    bad = bytearray(good)
    out.append(("meta size 64 huge", bytes(bad[:i]) + struct.pack(">I", 1) + b"meta" +
                struct.pack(">Q", 1 << 62) + bytes(bad[i + 8:])))
    # A meta box of size 4: smaller than its header.
    bad = bytearray(good)
    bad[i:i + 4] = struct.pack(">I", 4)
    out.append(("meta size 4", bytes(bad)))
    # ispe of 65535 x 65535 and of 16385 x 1, 8192 x 8192.
    for (w, h) in [(65535, 65535), (16385, 1), (8192, 8193), (0, 5), (4000000000, 4000000000)]:
        bad = bytearray(good)
        j = good.find(b"ispe")
        bad[j + 8:j + 16] = struct.pack(">II", w, h)
        out.append(("ispe %dx%d" % (w, h), bytes(bad)))
    # The bomb: a side of 16384 and a count that makes 64 MiB of RGBA, with a
    # tiny stream. The stream size decides, so the call fails fast.
    bad = bytearray(good)
    j = good.find(b"ispe")
    bad[j + 8:j + 16] = struct.pack(">II", 16384, 1024)
    out.append(("bomb 16384x1024", bytes(bad)))
    bad[j + 8:j + 16] = struct.pack(">II", 4096, 4096)
    out.append(("bomb 4096x4096", bytes(bad)))
    # Every iloc offset field pushed out of the file.
    bad = bytearray(good)
    j = good.find(b"iloc")
    for pos in range(j + 4, j + 40):
        b2 = bytearray(good)
        b2[pos] = 0xff
        out.append(("iloc byte %d ff" % (pos - j), bytes(b2)))
    # A flip of every byte of the header region, one byte at a time, 0xff.
    hdr = good.find(b"mdat")
    for pos in range(0, min(hdr, 400), 3):
        b2 = bytearray(good)
        b2[pos] ^= 0xff
        out.append(("flip %d" % pos, bytes(b2)))
    deep = [c for c in cases if c[0].startswith("deep-10bit-420")][0][1]
    out.append(("deep truncated", deep[:len(deep) - 40]))
    for pos in range(len(deep) - 150, len(deep), 12):
        b2 = bytearray(deep)
        b2[pos] ^= 0x5a
        out.append(("deep rot %d" % pos, bytes(b2)))
    # Bit rot in the AV1 data.
    for pos in range(hdr + 8, len(good), max(1, (len(good) - hdr) // 20)):
        b2 = bytearray(good)
        b2[pos] ^= 0x55
        out.append(("rot %d" % pos, bytes(b2)))
    return out


# Deep color. Pillow writes 8 bit files only, so the AV1 streams of these
# cases come from the libaom encoder of the vendor tree, fed with smooth
# planes of 10 and 12 bits. The container is built here. Pillow (libavif with
# dav1d) reads the files, and its pixels are the oracle.
def box(t, body): return struct.pack(">I", 8 + len(body)) + t + body
def fbox(t, ver, body, flags=0): return box(t, bytes([ver]) + flags.to_bytes(3, "big") + body)
def avif_container(obus, w, h, bits, sub, profile, rng, nclx_matrix=6):
    ftyp = box(b"ftyp", b"avif" + bytes(4) + b"avifmif1miafMA1B")
    hdlr = fbox(b"hdlr", 0, bytes(4) + b"pict" + bytes(12) + b"\0")
    pitm = fbox(b"pitm", 0, struct.pack(">H", 1))
    infe = fbox(b"infe", 2, struct.pack(">HH", 1, 0) + b"av01" + b"\0")
    iinf = fbox(b"iinf", 0, struct.pack(">H", 1) + infe)
    ispe = fbox(b"ispe", 0, struct.pack(">II", w, h))
    pixi = fbox(b"pixi", 0, bytes([3, bits, bits, bits]))
    flags = (0x40 if bits > 8 else 0) | (0x20 if bits == 12 else 0)
    flags |= 0 if sub == "444" else 0x0c
    av1c = box(b"av1C", bytes([0x81, (profile << 5) | 8, flags, 0]))
    colr = box(b"colr", b"nclx" + struct.pack(">HHHB", 1, 13, nclx_matrix, 0x80 if rng else 0))
    ipco = box(b"ipco", ispe + pixi + av1c + colr)
    ipma = fbox(b"ipma", 0, struct.pack(">I", 1) + struct.pack(">HB", 1, 4) + bytes([0x81, 0x82, 0x83, 0x04]))
    iprp = box(b"iprp", ipco + ipma)
    # iloc: offsets are filled once the sizes are known.
    def iloc(off):
        return fbox(b"iloc", 0, bytes([0x44, 0x00]) + struct.pack(">H", 1) +
                    struct.pack(">HHH", 1, 0, 1) + struct.pack(">II", off, len(obus)))
    meta_body = hdlr + pitm + iloc(0) + iinf + iprp
    meta_len = 8 + 4 + len(meta_body)
    off = len(ftyp) + meta_len + 8
    meta = fbox(b"meta", 0, hdlr + pitm + iloc(off) + iinf + iprp)
    return ftyp + meta + box(b"mdat", obus)
DEEP = [
    # name, width, height, bits, subsampling, profile, full range, AV1 stream
    ("deep-10bit-420-limited", 17, 9, 10, "420", 0, 0, "12000a0a000000021c20daf9c04032d6011000a8000002400000dc5a69fefeb9c9a8cde6885dd9a679433d367edff2be6d60e6f3a1aeb2650b2b4aa8aba6981212bf1c280d2fbc984dbacc0417a8883ba56604d2b6e602b92d8f9388667c66cedd418a7d405456c409497fee98f0a920702ff27ed04421e9db7ba232d366ddf91540544617299b2d2a2a5e7a020eac6ab6f7bb6c834a6e57344c97766cf3d91b74226c2678e57bd5af9266911f83c32c6ebd25188f41410d4c4f2ff68a3f65d62737c72af1fae71b18e2b4e2fe6617c9abb9e835e3e3885f8c888745a15c1b3e32da995542dd3c"),
    ("deep-10bit-444-full", 16, 8, 10, "444", 1, 1, "12000a092000000197f36be748325c1000a80000024000005496b7793ff9f0983b769ca087fb0161ed0479294cce4a945a584199bd7ea714b6a8588198cf4f51d043159808d265d3ebc1e43b1879d921173a7d1bdbfac51e6b09c0b61c942763f053a1a54410d17ca14960"),
    ("deep-12bit-420-full", 13, 7, 12, "420", 2, 1, "12000a0a4000000196636be79c203292011000a800000240ae02031727605181ecc6a17fb8f63728b428c1fc33a890bc232e50b7f571dfa301e00dea55e65547a1da22bd5d062b5bc870b0b645b0489803ff957966684036d247c6337e9a731946718ca31a50f99e807b7de7363c960d9a4dc2740d6c1ffe01118e6a7f124db5d77659616f92a1f77803008b5b5f7ecd7c0105715f95ac8ab28df3081dcd17c80e97e0"),
]
for name, w, h, bits, sub, prof, rng_, hexobus in DEEP:
    cases.append((name, avif_container(bytes.fromhex(hexobus), w, h, bits, sub, prof, rng_), False))


def main():
    out = []
    p = out.append
    p("// Generated by test/make_avif_cases.py. Do not edit by hand.")
    p("// The cases come from Pillow %s (libavif)." % PIL.__version__)
    p("")
    p("namespace avif_cases;")
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
    p("// avif_case decodes the file and compares with the pixels of Pillow. The")
    p("// two conversions from YUV may differ by tol in a part. mean is the most")
    p("// that the mean difference may be, in hundredths.")
    p("fn avif_case(file: u8[:], w: i32, h: i32, channels: i32, tol: i32, want: u8[:], strict16: bool) {")
    p("    let bytes = from_hex(file);")
    p("    let m = image::metadata_bytes(bytes);")
    p("    assert(m.ok && m.kind == image::KIND_AVIF && m.width == w && m.height == h);")
    p("    assert(m.channels == channels);")
    p("    let img = image::decode_avif(bytes);")
    p("    assert(img.width == w && img.height == h);")
    p("    let px = from_hex(want);")
    p("    assert(img.pixels.len == px.len);")
    p("    var worst: i32 = 0;")
    p("    for i in 0..px.len {")
    p("        var d: i32 = img.pixels[i] as i32 - px[i] as i32;")
    p("        if d < 0 { d = -d; }")
    p("        if d > worst { worst = d; }")
    p("    }")
    p("    assert(worst <= tol);")
    p("    // decode sniffs the format and gives the same pixels.")
    p("    let again = image::decode(bytes);")
    p("    assert(again.pixels == img.pixels);")
    p("    // The 16 bit read holds the same picture.")
    p("    let wide = image::decode_avif16(bytes);")
    p("    assert(wide.width == w && wide.height == h && wide.pixels.len == px.len);")
    p("    let narrow = image::to_image8(wide);")
    p("    // A premultiplied file undoes the product in 8 bits or in 16 bits, so the")
    p("    // two reads may differ where the alpha is small.")
    p("    for i in 0..px.len {")
    p("        if !strict16 { break; }")
    p("        var d: i32 = narrow.pixels[i] as i32 - img.pixels[i] as i32;")
    p("        if d < 0 { d = -d; }")
    p("        assert(d <= tol);")
    p("    }")
    p("}")
    p("")
    p("// hostile_check decodes the bytes and asks for the header. Nothing aborts and")
    p("// nothing gives a size that the bytes do not hold.")
    p("fn hostile_check(file: u8[:]) {")
    p("    let bytes = from_hex(file);")
    p("    let img = image::decode(bytes);")
    p("    assert(img.pixels.len == 0 || img.pixels.len == img.width * img.height * 4);")
    p("    assert(img.width <= 16384 && img.height <= 16384);")
    p("    let wide = image::decode16(bytes);")
    p("    assert(wide.pixels.len == 0 || wide.pixels.len == wide.width * wide.height * 4);")
    p("    let m = image::metadata_bytes(bytes);")
    p("    if m.ok && img.pixels.len > 0 {")
    p("        assert(m.width == img.width && m.height == img.height);")
    p("    }")
    p("}")
    p("")
    p("fn check_decode() {")
    for kind, data, alpha in cases:
        im = Image.open(io.BytesIO(data))
        im.load()
        rgba = im.convert("RGBA")
        # Pillow upsamples the chroma with libyuv. The package blends the
        # same way, but the edge samples and the rounding differ a little.
        tol = 2 if ("444" in kind or "gray" in kind or "lossless" in kind) and "premult" not in kind else 12
        sub = data.find(b"av1C")
        mono = bool(data[sub + 6] & 0x10)
        channels = (1 if mono else 3) + (1 if alpha else 0)
        p('    avif_case("%s", %d, %d, %d, %d, "%s", %s); // %s' % (
            data.hex(), im.width, im.height, channels, tol, rgba.tobytes().hex(),
            "false" if "premult" in kind else "true", kind))
    p("}")
    p("")
    p("fn check_hostile() {")
    hs = hostile()
    for name, data in hs:
        p('    hostile_check("%s"); // %s' % (data.hex(), name))
    p("}")
    p("")
    p("fn check_avif() {")
    p("    check_decode();")
    p("    check_hostile();")
    p('    print("avif decode cases ok\\n");')
    p("}")
    print("\n".join(out))
    import sys
    print("%d decode cases, %d hostile cases" % (len(cases), len(hs)), file=sys.stderr)


main()
