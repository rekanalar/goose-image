// The geometry of the image package. Pure Goose.
//
// The calls work on a whole image: a crop, a flip or a turn, a paste, a
// blend and a rotation by any angle. They follow Pillow: Image.crop,
// Image.transpose, Image.paste, Image.alpha_composite and Image.rotate.
//
// Every call checks its image first. An image with a side of zero or less,
// or with a short buffer, counts as empty. A call never reads or writes
// outside a buffer.

namespace image;

import std;
import math;

// The operations of transpose. The numbers are the numbers of Pillow.
const TRANSPOSE_FLIP_LEFT_RIGHT = 0;
const TRANSPOSE_FLIP_TOP_BOTTOM = 1;
const TRANSPOSE_ROTATE_90 = 2;
const TRANSPOSE_ROTATE_180 = 3;
const TRANSPOSE_ROTATE_270 = 4;
const TRANSPOSE_TRANSPOSE = 5;
const TRANSPOSE_TRANSVERSE = 6;

// The most bytes that the pixels of a result may hold.
const MAX_RESULT_BYTES: i64 = 1073741824;

// empty_image gives an image with no pixels.
fn empty_image() -> Image {
    return Image { width: 0, height: 0, pixels: [] };
}

// is_whole tells whether the image has a positive size and a full buffer.
fn is_whole(img: Image&) -> bool {
    let w: i64 = img.width;
    let h: i64 = img.height;
    if w <= 0 || h <= 0 { return false; }
    // The count of pixels is compared, not the count of bytes. The product
    // of the sides times four can pass the size of an i64.
    return img.pixels.len / 4 >= w * h;
}

// fits_limit tells whether an image of the size stays within the limit.
fn fits_limit(w: i64, h: i64) -> bool {
    if w <= 0 || h <= 0 { return false; }
    if w > MAX_RESULT_BYTES || h > MAX_RESULT_BYTES { return false; }
    return w * h * 4 <= MAX_RESULT_BYTES;
}

// push_pixel appends the pixel at the place x, y of the image.
fn push_pixel(out: u8[>..]&, img: Image&, x: i64, y: i64) {
    let at = (y * img.width + x) * 4;
    out.push(img.pixels[at]);
    out.push(img.pixels[at + 1]);
    out.push(img.pixels[at + 2]);
    out.push(img.pixels[at + 3]);
}

// push_clear appends one transparent black pixel.
fn push_clear(out: u8[>..]&) {
    out.push(0);
    out.push(0);
    out.push(0);
    out.push(0);
}

// crop gives the box of the image. The box holds the pixels from left up
// to right, and from top up to bottom. A pixel of the box outside the image
// is transparent black. A box without area gives an empty image.
fn crop(img: Image&, left: i32, top: i32, right: i32, bottom: i32) -> Image {
    if !is_whole(img) { return empty_image(); }
    let l: i64 = left;
    let t: i64 = top;
    let w: i64 = right - l;
    let h: i64 = bottom - t;
    if !fits_limit(w, h) { return empty_image(); }
    let sw: i64 = img.width;
    let sh: i64 = img.height;
    var out: u8[>..] = [];
    for y in 0..h {
        for x in 0..w {
            let sx = l + x;
            let sy = t + y;
            if sx < 0 || sy < 0 || sx >= sw || sy >= sh {
                push_clear(out);
            } else {
                push_pixel(out, img, sx, sy);
            }
        }
    }
    return Image { width: w as i32, height: h as i32, pixels: out[..] };
}

// transpose flips or turns the image. The turns are counter-clockwise. An
// unknown operation gives an empty image.
fn transpose(img: Image&, op: i32) -> Image {
    if !is_whole(img) { return empty_image(); }
    if op < 0 || op > 6 { return empty_image(); }
    let w: i64 = img.width;
    let h: i64 = img.height;
    // The operations 2, 4, 5 and 6 swap the sides.
    var rw = w;
    var rh = h;
    if op == 2 || op >= 4 {
        rw = h;
        rh = w;
    }
    // The place in the source is a linear form of the place in the result:
    // sx = cx + ax * x + bx * y, and sy = cy + ay * x + by * y.
    // The six numbers of each operation stand in these tables.
    var cx = 0;
    var ax = 0;
    var bx = 0;
    var cy = 0;
    var ay = 0;
    var by = 0;
    if op == 0 {
        cx = w - 1; ax = -1; by = 1;
    } else if op == 1 {
        ax = 1; cy = h - 1; by = -1;
    } else if op == 2 {
        cx = w - 1; bx = -1; ay = 1;
    } else if op == 3 {
        cx = w - 1; ax = -1; cy = h - 1; by = -1;
    } else if op == 4 {
        bx = 1; cy = h - 1; ay = -1;
    } else if op == 5 {
        bx = 1; ay = 1;
    } else {
        cx = w - 1; bx = -1; cy = h - 1; ay = -1;
    }
    var out: u8[>..] = [];
    for y in 0..rh {
        for x in 0..rw {
            push_pixel(out, img, cx + ax * x + bx * y, cy + ay * x + by * y);
        }
    }
    return Image { width: rw as i32, height: rh as i32, pixels: out[..] };
}

// clip_part copies the part of the source that lands inside the destination
// into part, row by row. It gives the first column and row, and the end
// column and row, of that part in the destination. The part is empty when the
// end is not above the first.
//
// The copy lets the caller write into the destination while it reads the
// source, even when the two are the same image.
fn clip_part(part: u8[>..]&, dst: Image&, src: Image&, x: i32, y: i32) -> i64, i64, i64, i64 {
    let px: i64 = x;
    let py: i64 = y;
    let x0 = max(0, px);
    let y0 = max(0, py);
    let dw: i64 = dst.width;
    let dh: i64 = dst.height;
    let sw: i64 = src.width;
    let sh: i64 = src.height;
    let x1 = min(dw, px + sw);
    let y1 = min(dh, py + sh);
    if x1 > x0 && y1 > y0 {
        for dy in y0..y1 {
            for dx in x0..x1 {
                push_pixel(part, src, dx - px, dy - py);
            }
        }
    }
    return x0, y0, x1, y1;
}

// paste copies the source into the destination at the place x, y. It clips
// at the edges of the destination. It does nothing when one image is empty.
//
// The result stays right when the two images are the same image.
fn paste(dst: Image&, src: Image&, x: i32, y: i32) {
    if !is_whole(dst) || !is_whole(src) { return; }
    var part: u8[>..] = [];
    let x0, y0, x1, y1 = clip_part(part, dst, src, x, y);
    var i = 0;
    for dy in y0..y1 {
        for dx in x0..x1 {
            let at = (dy * dst.width + dx) * 4;
            dst.pixels[at] = part[i];
            dst.pixels[at + 1] = part[i + 1];
            dst.pixels[at + 2] = part[i + 2];
            dst.pixels[at + 3] = part[i + 3];
            i += 4;
        }
    }
}

// div255 divides by 255 with the rounding of Pillow.
fn div255(a: i64) -> i64 {
    return ((a / 256) + a) / 256;
}

// blend_part gives one color part of the blend of a source pixel over a
// destination pixel. The coefficients come from the alpha of the two.
fn blend_part(s: i64, d: i64, coef1: i64, coef2: i64) -> i64 {
    let t = s * coef1 + d * coef2;
    return div255(t + 128 * 128) / 128;
}

// alpha_composite blends the source over the destination at the place x, y.
// The formula is the integer formula of Pillow Image.alpha_composite. The
// call clips at the edges of the destination. It does nothing when one image
// is empty.
fn alpha_composite(dst: Image&, src: Image&, x: i32, y: i32) {
    if !is_whole(dst) || !is_whole(src) { return; }
    var part: u8[>..] = [];
    let x0, y0, x1, y1 = clip_part(part, dst, src, x, y);
    var i = 0;
    for dy in y0..y1 {
        for dx in x0..x1 {
            let at = (dy * dst.width + dx) * 4;
            let sa: i64 = part[i + 3];
            // A source with no alpha leaves the destination as it is.
            if sa != 0 {
                let da: i64 = dst.pixels[at + 3];
                let blend = da * (255 - sa);
                // The alpha of the result times 255. It is above 0 here.
                let outa255 = sa * 255 + blend;
                let coef1 = sa * 255 * 255 * 128 / outa255;
                let coef2 = 255 * 128 - coef1;
                dst.pixels[at] = blend_part(part[i], dst.pixels[at], coef1, coef2) as u8;
                dst.pixels[at + 1] = blend_part(part[i + 1], dst.pixels[at + 1], coef1, coef2) as u8;
                dst.pixels[at + 2] = blend_part(part[i + 2], dst.pixels[at + 2], coef1, coef2) as u8;
                dst.pixels[at + 3] = div255(outa255 + 128) as u8;
            }
            i += 4;
        }
    }
}

// The most degrees that rotate accepts. Beyond it the angle gives an empty
// image, because the conversion to an integer would overflow.
const MAX_DEGREES: f64 = 1000000000.0;

// ffloor gives the greatest integer that is not above v. It uses the
// conversion to an integer, and no call of floor. The JIT of macOS arm64
// mis-evaluates floor.
fn ffloor(v: f64) -> i64 {
    // The cast is the unchecked form. The checked form aborts in a debug
    // build when the value has a fraction.
    let t: i64 = v as! i64;
    if t > v { return t - 1; }
    return t;
}

// fceil gives the least integer that is not below v.
fn fceil(v: f64) -> i64 {
    return -ffloor(-v);
}

// round15 rounds v to 15 decimals, as Python round(v, 15) does.
fn round15(v: f64) -> f64 {
    let scaled = v * 1000000000000000.0;
    var r: f64 = 0.5;
    if scaled < 0.0 { r = -0.5; }
    return ((scaled + r) as! i64) / 1000000000000000.0;
}

// fix16 gives v as a 16.16 fixed point number, rounded as Pillow rounds.
fn fix16(v: f64) -> i64 {
    return ffloor(v * 65536.0 + 0.5);
}

// div16 shifts a 16.16 number right by 16 bits. It rounds toward minus
// infinity, as the shift of Pillow does.
fn div16(v: i64) -> i64 {
    if v >= 0 { return v / 65536; }
    return -((-v + 65535) / 65536);
}

// rotate turns the image counter-clockwise by the angle in degrees. It
// samples the nearest pixel, as Pillow does with Image.rotate. A pixel
// with no source is transparent black.
//
// With expand the result is large enough to hold the whole image. Without
// it the result has the size of the image.
fn rotate(img: Image&, degrees: f64, expand: bool) -> Image {
    if !is_whole(img) { return empty_image(); }
    // The test is written so that a NaN fails it. A NaN compares false
    // with every number.
    if !(degrees >= -MAX_DEGREES && degrees <= MAX_DEGREES) { return empty_image(); }
    let w: i64 = img.width;
    let h: i64 = img.height;
    // The angle in the range from 0 up to 360.
    var angle = degrees - 360.0 * ((degrees / 360.0) as! i64);
    if angle < 0.0 { angle += 360.0; }
    // The turns that are exact. They give the same pixels as transpose.
    if angle == 0.0 { return crop(img, 0, 0, img.width, img.height); }
    if angle == 180.0 { return transpose(img, TRANSPOSE_ROTATE_180); }
    if angle == 90.0 && (expand || w == h) { return transpose(img, TRANSPOSE_ROTATE_90); }
    if angle == 270.0 && (expand || w == h) { return transpose(img, TRANSPOSE_ROTATE_270); }

    // The matrix goes from a pixel of the result to a place in the source.
    let rad = -radians(angle);
    let m0 = round15(cos(rad));
    let m1 = round15(sin(rad));
    // The rounding of round15 keeps the sign, so the other two follow.
    let m3 = -m1;
    let m4 = m0;
    let cx = (w as f64) / 2.0;
    let cy = (h as f64) / 2.0;
    var m2 = m0 * (-cx) + m1 * (-cy) + cx;
    var m5 = m3 * (-cx) + m4 * (-cy) + cy;

    var rw = w;
    var rh = h;
    if expand {
        // The corners of the image give the size of the result.
        let fw = w as f64;
        let fh = h as f64;
        let x1 = m1 * fh + m2;
        let x2 = m0 * fw + m1 * fh + m2;
        let x3 = m0 * fw + m2;
        let y1 = m4 * fh + m5;
        let y2 = m3 * fw + m4 * fh + m5;
        let y3 = m3 * fw + m5;
        let xmin = min(min(m2, x1), min(x2, x3));
        let xmax = max(max(m2, x1), max(x2, x3));
        let ymin = min(min(m5, y1), min(y2, y3));
        let ymax = max(max(m5, y1), max(y2, y3));
        rw = fceil(xmax) - ffloor(xmin);
        rh = fceil(ymax) - ffloor(ymin);
        let tx = -((rw - w) as f64) / 2.0;
        let ty = -((rh - h) as f64) / 2.0;
        let n2 = m0 * tx + m1 * ty + m2;
        let n5 = m3 * tx + m4 * ty + m5;
        m2 = n2;
        m5 = n5;
    }
    if !fits_limit(rw, rh) { return empty_image(); }

    // Pillow samples with 16.16 fixed point numbers. The pixels at the edge
    // of a place depend on this rounding, so the package does the same.
    let f0 = fix16(m0);
    let f1 = fix16(m1);
    let f3 = fix16(m3);
    let f4 = fix16(m4);
    let xo = fix16(m2 + m1 * 0.5 + m0 * 0.5);
    let yo = fix16(m5 + m4 * 0.5 + m3 * 0.5);
    var out: u8[>..] = [];
    for y in 0..rh {
        // The part of the place that depends on the row only.
        let rx = xo + f1 * y;
        let ry = yo + f4 * y;
        for x in 0..rw {
            let sx = div16(rx + f0 * x);
            let sy = div16(ry + f3 * x);
            if sx < 0 || sy < 0 || sx >= w || sy >= h {
                push_clear(out);
            } else {
                push_pixel(out, img, sx, sy);
            }
        }
    }
    return Image { width: rw as i32, height: rh as i32, pixels: out[..] };
}
