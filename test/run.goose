// Tests for the image package. Run with: goose run test/run.goose
//
// The test makes images, writes them, and reads them back. The pixels of
// the read image must hold the colors that were written.

import std;
import image from "..";

fn test_round_trip() {
    // A 4x3 image: every pixel holds the color of its place.
    var img = image::new_image(4, 3, 0, 0, 0, 255);
    for y: i32 in 0..3 {
        for x: i32 in 0..4 {
            image::set_pixel(img, x, y, (x * 60) as u8, (y * 80) as u8, 128, 255);
        }
    }

    // The size and one pixel.
    assert(img.width == 4);
    assert(img.height == 3);
    let r, g, b, a = image::pixel(img, 2, 1);
    assert(r == 120);
    assert(g == 80);
    assert(b == 128);
    assert(a == 255);

    // A place outside the image gives the empty color.
    let r2, g2, b2, a2 = image::pixel(img, 9, 9);
    assert(r2 == 0 && g2 == 0 && b2 == 0 && a2 == 0);

    // Write the three formats and read each back.
    assert(image::write_png("test-image.png", img));
    assert(image::write_bmp("test-image.bmp", img));
    assert(image::write_jpeg("test-image.jpg", img));

    let from_png = image::load("test-image.png");
    assert(from_png.width == 4 && from_png.height == 3);
    let pr, pg, pb, pa = image::pixel(from_png, 2, 1);
    assert(pr == 120 && pg == 80 && pb == 128 && pa == 255);

    let from_bmp = image::load("test-image.bmp");
    assert(from_bmp.width == 4 && from_bmp.height == 3);
    let br, bg, bb, ba = image::pixel(from_bmp, 3, 2);
    assert(br == 180 && bg == 160 && bb == 128 && ba == 255);

    // JPEG takes losses, so the test checks the size and a close color.
    let from_jpg = image::load("test-image.jpg");
    assert(from_jpg.width == 4 && from_jpg.height == 3);
    let jr, jg, jb, ja = image::pixel(from_jpg, 2, 1);
    assert(ja == 255);
    let jd = jr as i32 - 120;
    if jd < 0 { assert(-jd <= 12); } else { assert(jd <= 12); }
    let jd2 = jg as i32 - 80;
    if jd2 < 0 { assert(-jd2 <= 12); } else { assert(jd2 <= 12); }
}

fn test_info() {
    let w, h, ok = image::info("test-image.png");
    assert(ok);
    assert(w == 4 && h == 3);

    let w2, h2, ok2 = image::info("no-such-file.png");
    assert(!ok2);
}

fn test_load_failure() {
    let empty = image::load("no-such-file.png");
    assert(empty.width == 0);
    assert(empty.height == 0);
    assert(empty.pixels.len == 0);
}

// A 32x32 image with a smooth gradient and a hard edge, so that the
// quality and the level change the size of the output.
fn make_busy() -> image::Image {
    var img = image::new_image(32, 32, 0, 0, 0, 255);
    for y: i32 in 0..32 {
        for x: i32 in 0..32 {
            let v = ((x * 7 + y * 13) % 256) as u8;
            image::set_pixel(img, x, y, v, (x * 8) as u8, (y * 8) as u8, 255);
        }
    }
    return img;
}

fn near(a: u8, b: u8, tol: i32) -> bool {
    let d = a - b as i32;
    return d <= tol && -d <= tol;
}

fn test_memory_round_trip() {
    let img = make_busy();
    // PNG, BMP and TGA keep every pixel.
    for f: i32 in 0..4 {
        if f == image::FORMAT_JPEG { continue; }
        let bytes = image::encode(img, f, 0);
        assert(bytes.len > 0);
        let back = image::decode(bytes);
        assert(back.width == 32 && back.height == 32);
        assert(back.pixels.len == img.pixels.len);
        for i in 0..img.pixels.len { assert(back.pixels[i] == img.pixels[i]); }
    }
    // JPEG keeps the size and a close color.
    let jb = image::encode(img, image::FORMAT_JPEG, 95);
    let jr = image::decode(jb);
    assert(jr.width == 32 && jr.height == 32);
    let r, g, b, a = image::pixel(jr, 16, 16);
    let r0, g0, b0, a0 = image::pixel(img, 16, 16);
    assert(near(r, r0, 25) && near(g, g0, 25) && near(b, b0, 25) && a == 255);
}

fn test_options() {
    let img = make_busy();
    let lo = image::encode(img, image::FORMAT_JPEG, 5);
    let hi = image::encode(img, image::FORMAT_JPEG, 100);
    assert(lo.len > 0 && hi.len > lo.len);
    // The TGA run-length flag: 2 is off, so the output is larger on flat data.
    let flat = image::new_image(32, 32, 9, 9, 9, 255);
    let rle = image::encode(flat, image::FORMAT_TGA, 1);
    let raw = image::encode(flat, image::FORMAT_TGA, 2);
    assert(rle.len < raw.len);
    // Every PNG level decodes to the same pixels.
    for lvl: i32 in 1..10 {
        let p = image::encode(flat, image::FORMAT_PNG, lvl);
        let back = image::decode(p);
        assert(back.width == 32 && back.pixels[0] == 9);
    }
    // A bad image does not encode.
    let none = image::Image { width: 4, height: 4, pixels: [] };
    assert(image::encode(none, image::FORMAT_PNG, 0).len == 0);
}

fn test_files() {
    let img = make_busy();
    assert(image::write_tga("test-image.tga", img));
    assert(image::write_hdr("test-image.hdr", img));
    let t = image::load("test-image.tga");
    assert(t.width == 32 && t.pixels[0] == img.pixels[0]);
    // HDR keeps 8 bits of mantissa shared by the three parts, so it is close.
    let hd = image::load("test-image.hdr");
    assert(hd.width == 32 && hd.height == 32);
    let hr, hg, hb, ha = image::pixel(hd, 20, 3);
    let r0, g0, b0, a0 = image::pixel(img, 20, 3);
    assert(near(hr, r0, 40) && near(hg, g0, 40) && near(hb, b0, 40));
}

fn test_metadata() {
    let img = make_busy();
    let png = image::encode(img, image::FORMAT_PNG, 0);
    let m = image::metadata_bytes(png);
    assert(m.ok && m.width == 32 && m.height == 32 && m.kind == image::KIND_PNG);
    assert(m.channels == 3 || m.channels == 4);
    let kinds: i32[5] = [image::KIND_PNG, image::KIND_JPEG, image::KIND_BMP, image::KIND_TGA, image::KIND_HDR];
    for f: i32 in 0..5 {
        let mm = image::metadata_bytes(image::encode(img, f, 0));
        assert(mm.ok && mm.width == 32 && mm.height == 32 && mm.kind == kinds[f]);
    }
    let fm = image::metadata("test-image.png");
    assert(fm.ok && fm.width == 4 && fm.height == 3);
    assert(!image::metadata("no-such-file.png").ok);
    assert(!image::metadata_bytes("not an image").ok);
    assert(image::decode("not an image").width == 0);
}

fn test_resize() {
    let img = make_busy();
    for f: i32 in 0..8 {
        let small = image::resize(img, 8, 16, f);
        assert(small.width == 8 && small.height == 16);
        assert(small.pixels.len == 8 * 16 * 4);
        let big = image::resize(img, 50, 40, f);
        assert(big.width == 50 && big.height == 40 && big.pixels.len == 50 * 40 * 4);
    }
    // A flat image stays flat, in every filter.
    let flat = image::new_image(10, 10, 100, 150, 200, 255);
    for f: i32 in 0..8 {
        let s = image::resize(flat, 3, 7, f);
        for y: i32 in 0..7 {
            for x: i32 in 0..3 {
                let r, g, b, a = image::pixel(s, x, y);
                assert(near(r, 100, 1) && near(g, 150, 1) && near(b, 200, 1) && a == 255);
            }
        }
    }
    // Nearest takes source pixels, so a 2x1 image doubled gives two pairs.
    var two = image::new_image(2, 1, 10, 0, 0, 255);
    image::set_pixel(two, 1, 0, 200, 0, 0, 255);
    let dbl = image::resize(two, 4, 1, image::FILTER_NEAREST);
    let p0, _g0, _b0, _a0 = image::pixel(dbl, 0, 0);
    let p1, _g1, _b1, _a1 = image::pixel(dbl, 1, 0);
    let p2, _g2, _b2, _a2 = image::pixel(dbl, 2, 0);
    let p3, _g3, _b3, _a3 = image::pixel(dbl, 3, 0);
    assert(p0 == 10 && p1 == 10 && p2 == 200 && p3 == 200);
    // A box filter on a 2x1 halved gives the mean.
    let avg = image::resize(two, 1, 1, image::FILTER_BOX);
    let ar, _ag, _ab, _aa = image::pixel(avg, 0, 0);
    assert(near(ar, 105, 1));
    // Bad input gives the empty image.
    assert(image::resize(img, 0, 5, 0).width == 0);
    assert(image::resize(img, 5, 5, 99).width == 0);
    // fit keeps the aspect ratio.
    let wide = image::new_image(40, 20, 1, 2, 3, 255);
    let f1 = image::fit(wide, 10, 10, image::FILTER_DEFAULT);
    assert(f1.width == 10 && f1.height == 5);
    let f2 = image::fit(wide, 100, 100, image::FILTER_DEFAULT);
    assert(f2.width == 100 && f2.height == 50);
    // A resized image survives a write and a read.
    let small = image::resize(img, 8, 8, image::FILTER_BICUBIC);
    let rt = image::decode(image::encode(small, image::FORMAT_PNG, 0));
    assert(rt.width == 8 && rt.height == 8);
}

fn main() {
    test_round_trip();
    test_info();
    test_load_failure();
    test_memory_round_trip();
    test_options();
    test_files();
    test_metadata();
    test_resize();
    print("all tests passed");
    return 0;
}
