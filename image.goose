// image - image decoding and encoding for Goose.
//
// The package reads PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PNM, PIC and WebP. It
// writes PNG, JPEG, BMP, TGA, HDR and WebP, to a file or to memory, and it
// resizes with several filters. The pixels are RGBA, four bytes each.
//
// The C part of the package wraps stb_image, stb_image_write and
// stb_image_resize2. The
// headers are vendored in the vendor directory, so the package needs no
// system library.
//
// The repository is goose-image. The package name is what a program imports
// it by, so the name is "image".
//
// Every file in this package declares "namespace image;". A user calls
// image::load. A user calls image::write_png.
namespace image;

import std;
import os;
import geometry;
import exif;

// The C functions of the package. The definitions are in src/image.c.
// Each one takes eight argument slots or fewer (the TinyCC JIT on arm64).
extern fn img_load(path: const u8[:], out: u8[>..]&, w: i32&, h: i32&) -> bool;
extern fn img_decode(data: const u8[:], out: u8[>..]&, w: i32&, h: i32&) -> bool;
extern fn img_probe_path(path: const u8[:], w: i32&, h: i32&, ch: i32&, kind: i32&) -> bool;
extern fn img_probe(data: const u8[:], w: i32&, h: i32&, ch: i32&, kind: i32&) -> bool;
extern fn img_encode(pixels: const u8[:], out: u8[>..]&, w: i32, h: i32, format: i32, param: i32) -> bool;
extern fn img_encode_webp(pixels: const u8[:], out: u8[>..]&, w: i32, h: i32, quality: i32, lossless: i32) -> bool;
extern fn img_resize(src: const u8[:], out: u8[>..]&, dims: const i32[:]) -> bool;
extern fn img_meta(data: const u8[:], icc: u8[>..]&, exif: u8[>..]&) -> bool;
extern fn img_meta_encode(base: const u8[:], out: u8[>..]&, icc: const u8[:], exif: const u8[:]) -> bool;

// The format numbers that encode and write take.
const FORMAT_PNG = 0;
const FORMAT_JPEG = 1;
const FORMAT_BMP = 2;
const FORMAT_TGA = 3;
const FORMAT_HDR = 4;
const FORMAT_WEBP = 5;

// The kinds of file that metadata names. KIND_UNKNOWN is not a format.
const KIND_UNKNOWN = 0;
const KIND_PNG = 1;
const KIND_JPEG = 2;
const KIND_BMP = 3;
const KIND_GIF = 4;
const KIND_TGA = 5;
const KIND_PSD = 6;
const KIND_HDR = 7;
const KIND_PNM = 8;
const KIND_PIC = 9;
const KIND_WEBP = 10;

// The filters of resize. FILTER_DEFAULT is Mitchell down and Catmull-Rom up.
const FILTER_DEFAULT = 0;
const FILTER_NEAREST = 1;
const FILTER_BOX = 2;
const FILTER_BILINEAR = 3;
const FILTER_BICUBIC = 4;
const FILTER_MITCHELL = 5;
const FILTER_BSPLINE = 6;
const FILTER_LANCZOS = 7;

// Image holds the pixels of an image. The pixels are RGBA, four bytes for
// each pixel. The rows follow each other. icc holds the bytes of the
// ICC profile, and exif holds the TIFF block of the EXIF. Both are
// empty when the file carries none.
struct Image {
    width: i32,
    height: i32,
    pixels: u8[],
    icc: u8[],
    exif: u8[],
}

// Meta is what the header of a file says. ok is false when the file is
// not an image that the package reads. channels is the count in the file.
struct Meta {
    width: i32,
    height: i32,
    channels: i32,
    kind: i32,
    ok: bool,
}

// load reads the image at the path. It returns an empty image when the
// file is not an image that the package reads.
fn load(path: const u8[:]) -> Image {
    var pixels: u8[>..] = [];
    var w: i32 = 0;
    var h: i32 = 0;
    if !img_load(path, pixels, w, h) {
        return Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    }
    // The metadata of the file. A read of the bytes costs one more
    // walk of the file, and the metadata is small beside the pixels.
    var icc: u8[>..] = [];
    var exif: u8[>..] = [];
    var data: u8[>..] = [];
    if read_file(path, data) {
        img_meta(data[..], icc, exif);
    }
    return Image { width: w, height: h, pixels: pixels[..], icc: icc[..], exif: exif[..] };
}

// decode reads an image from encoded bytes in memory. It returns an empty
// image when the bytes are not an image that the package reads.
fn decode(data: const u8[:]) -> Image {
    var pixels: u8[>..] = [];
    var w: i32 = 0;
    var h: i32 = 0;
    if !img_decode(data, pixels, w, h) {
        return Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    }
    var icc: u8[>..] = [];
    var exif: u8[>..] = [];
    img_meta(data, icc, exif);
    return Image { width: w, height: h, pixels: pixels[..], icc: icc[..], exif: exif[..] };
}

// metadata reads the header of the file at the path. It does not decode
// the pixels.
fn metadata(path: const u8[:]) -> Meta {
    var w: i32 = 0;
    var h: i32 = 0;
    var ch: i32 = 0;
    var kind: i32 = 0;
    if !img_probe_path(path, w, h, ch, kind) {
        return Meta { width: 0, height: 0, channels: 0, kind: 0, ok: false };
    }
    return Meta { width: w, height: h, channels: ch, kind: kind, ok: true };
}

// metadata_bytes is metadata for encoded bytes in memory.
fn metadata_bytes(data: const u8[:]) -> Meta {
    var w: i32 = 0;
    var h: i32 = 0;
    var ch: i32 = 0;
    var kind: i32 = 0;
    if !img_probe(data, w, h, ch, kind) {
        return Meta { width: 0, height: 0, channels: 0, kind: 0, ok: false };
    }
    return Meta { width: w, height: h, channels: ch, kind: kind, ok: true };
}

// info gives the width and the height of the image at the path. It returns
// false when the file is not an image that the package reads.
fn info(path: const u8[:]) -> i32, i32, bool {
    let m = metadata(path);
    return m.width, m.height, m.ok;
}

// encode gives the image as bytes in memory. The param is the JPEG quality
// (1 to 100, default 90), the lossy WebP quality (1 to 100, default 80), the
// PNG level (1 to 9, default 8) or the TGA run-length flag (1 on, 2 off,
// default on). The other formats ignore it. The result is empty when the
// encode fails.
fn encode(img: Image&, format: i32, param: i32) -> u8[] {
    var out: u8[>..] = [];
    if !size_ok(img.width, img.height) { return []; }
    if format == FORMAT_WEBP {
        if !img_encode_webp(img.pixels, out, img.width, img.height, if param > 0 { param } else { 80 }, 0) {
            return [];
        }
        return out[..];
    }
    if !img_encode(img.pixels, out, img.width, img.height, format, param) {
        return [];
    }
    if img.icc.len > 0 || img.exif.len > 0 {
        if format != FORMAT_PNG && format != FORMAT_JPEG { return out[..]; }
        var full: u8[>..] = [];
        if !img_meta_encode(out[..], full, img.icc, img.exif) { return out[..]; }
        return full[..];
    }
    return out[..];
}

// encode_webp gives the image as WebP bytes. The quality runs from 0 to 100.
// A lossless write keeps every pixel and ignores the quality for the pixels.
// A side above 16383 gives an empty result. WebP holds no ICC or EXIF here.
fn encode_webp(img: Image&, quality: i32, lossless: bool) -> u8[] {
    var out: u8[>..] = [];
    if !size_ok(img.width, img.height) { return []; }
    if !img_encode_webp(img.pixels, out, img.width, img.height, quality, if lossless { 1 } else { 0 }) {
        return [];
    }
    return out[..];
}

// write_with writes the image to the path. The param is as for encode.
// The metadata of the image rides with the file.
fn write_with(path: const u8[:], img: Image&, format: i32, param: i32) -> bool {
    if !size_ok(img.width, img.height) { return false; }
    let out = encode(img, format, param);
    if out.len == 0 { return false; }
    return write_file(path, out);
}

// write writes the image to the path with the default options.
fn write(path: const u8[:], img: Image&, format: i32) -> bool {
    return write_with(path, img, format, 0);
}

// write_png writes the image to the path as a PNG file.
fn write_png(path: const u8[:], img: Image&) -> bool {
    return write(path, img, FORMAT_PNG);
}

// write_jpeg writes the image to the path as a JPEG file.
fn write_jpeg(path: const u8[:], img: Image&) -> bool {
    return write(path, img, FORMAT_JPEG);
}

// write_bmp writes the image to the path as a BMP file.
fn write_bmp(path: const u8[:], img: Image&) -> bool {
    return write(path, img, FORMAT_BMP);
}

// write_tga writes the image to the path as a TGA file.
fn write_tga(path: const u8[:], img: Image&) -> bool {
    return write(path, img, FORMAT_TGA);
}

// write_hdr writes the image to the path as a Radiance HDR file. The alpha
// part is lost.
fn write_hdr(path: const u8[:], img: Image&) -> bool {
    return write(path, img, FORMAT_HDR);
}

// resize scales the image to the size with the filter. It returns an empty
// image when the size is below one or the filter is not a FILTER constant.
fn resize(img: Image&, width: i32, height: i32, filter: i32) -> Image {
    if !size_ok(img.width, img.height) || !size_ok(width, height) { return empty_image(); }
    var out: u8[>..] = [];
    let dims: i32[5] = [img.width, img.height, width, height, filter];
    if !img_resize(img.pixels, out, dims) {
        return Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    }
    return Image { width: width, height: height, pixels: out[..], icc: [], exif: [] };
}

// fit scales the image down or up to sit inside the box and keeps the
// aspect ratio. A side never gets below one pixel.
fn fit(img: Image&, max_width: i32, max_height: i32, filter: i32) -> Image {
    if img.width <= 0 || img.height <= 0 || max_width <= 0 || max_height <= 0 {
        return Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    }
    let mw: i64 = max_width;
    let mh: i64 = max_height;
    let iw: i64 = img.width;
    let ih: i64 = img.height;
    var w = max_width;
    var h: i32 = (mw * ih / iw) as i32;
    if mw * ih > mh * iw {
        h = max_height;
        w = (mh * iw / ih) as i32;
    }
    if w < 1 { w = 1; }
    if h < 1 { h = 1; }
    return resize(img, w, h, filter);
}

// pixel gives the color of one pixel. The color parts run from 0 to 255.
// The alpha part runs from 0 to 255. It returns 0, 0, 0, 0 when the place
// is outside the image.
fn pixel(img: Image&, x: i32, y: i32) -> u8, u8, u8, u8 {
    if x < 0 || y < 0 || x >= img.width || y >= img.height { return 0, 0, 0, 0; }
    // The index is computed in 64 bits. A hostile size cannot wrap it.
    let w: i64 = img.width;
    let n = y * w + x;
    // Compare the pixel number before the times four, which can overflow.
    if n >= img.pixels.len / 4 { return 0, 0, 0, 0; }
    let at = n * 4;
    return img.pixels[at], img.pixels[at + 1], img.pixels[at + 2], img.pixels[at + 3];
}

// set_pixel writes one color into the image. The color parts run from 0 to
// 255. It does nothing when the place is outside the image.
fn set_pixel(img: Image&, x: i32, y: i32, r: u8, g: u8, b: u8, a: u8) {
    if x < 0 || y < 0 || x >= img.width || y >= img.height { return; }
    let w: i64 = img.width;
    let n = y * w + x;
    if n >= img.pixels.len / 4 { return; }
    let at = n * 4;
    img.pixels[at] = r;
    img.pixels[at + 1] = g;
    img.pixels[at + 2] = b;
    img.pixels[at + 3] = a;
}

// new_image makes an image of the size, filled with one color. The color
// parts run from 0 to 255.
fn new_image(width: i32, height: i32, r: u8, g: u8, b: u8, a: u8) -> Image {
    if !size_ok(width, height) { return empty_image(); }
    var pixels: u8[>..] = [];
    for i in 0..width * height * 4 {
        pixels.push(if i % 4 == 0 { r } else if i % 4 == 1 { g } else if i % 4 == 2 { b } else { a });
    }
    return Image { width: width, height: height, pixels: pixels[..], icc: [], exif: [] };
}
