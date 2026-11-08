// Tests for the image package. Run with: goose run test/run.goose
//
// The test makes images, writes them, and reads them back. The pixels of
// the read image must hold the colors that were written.

import std;
import os;
import image from "..";
import geometry_cases;
import gif_cases;
import jpeg_cases;
import webp_cases;
import png16_cases;

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
    let none = image::Image { width: 4, height: 4, pixels: [], icc: [], exif: [] };
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

// is_empty tells whether the image has no pixels.
fn is_empty(img: image::Image) -> bool {
    return img.width == 0 && img.height == 0 && img.pixels.len == 0;
}

// test_geometry_contract checks the rules of the geometry calls that Pillow
// does not state the same way: empty results, limits and hostile input.
fn test_geometry_contract() {
    var img = image::new_image(4, 3, 10, 20, 30, 255);
    image::set_pixel(img, 1, 1, 200, 100, 50, 128);

    // A box without area gives an empty image. Pillow raises an error for an
    // inverted box and gives a side of zero for an empty one.
    assert(is_empty(image::crop(img, 2, 0, 1, 1)));
    assert(is_empty(image::crop(img, 1, 0, 1, 2)));
    assert(is_empty(image::crop(img, 0, 3, 4, 3)));
    // A box that is too large gives an empty image and does not abort.
    assert(is_empty(image::crop(img, 0, 0, 2147483647, 2147483647)));
    assert(is_empty(image::crop(img, -2147483647, -2147483647, 2147483647, 2147483647)));
    assert(is_empty(image::crop(img, 0, 0, 40000, 40000)));
    // A box far outside the image gives transparent black.
    let far = image::crop(img, 1000, 1000, 1002, 1001);
    assert(far.width == 2 && far.height == 1);
    let r, g, b, a = image::pixel(far, 1, 0);
    assert(r == 0 && g == 0 && b == 0 && a == 0);

    // An unknown operation gives an empty image.
    assert(is_empty(image::transpose(img, -1)));
    assert(is_empty(image::transpose(img, 7)));

    // An empty image and a short buffer count as empty.
    let none = image::Image { width: 0, height: 0, pixels: [], icc: [], exif: [] };
    let short = image::Image { width: 4, height: 3, pixels: [1, 2, 3], icc: [], exif: [] };
    assert(is_empty(image::crop(none, 0, 0, 1, 1)));
    assert(is_empty(image::crop(short, 0, 0, 1, 1)));
    assert(is_empty(image::transpose(none, 0)));
    assert(is_empty(image::transpose(short, 2)));
    assert(is_empty(image::rotate(short, 30.0, true)));
    assert(is_empty(image::rotate(none, 30.0, false)));

    // A paste with an empty or short source leaves the destination as it is.
    var dst = image::new_image(4, 3, 1, 2, 3, 4);
    let before = dst.pixels;
    image::paste(dst, none, 0, 0);
    image::paste(dst, short, 0, 0);
    image::alpha_composite(dst, none, 0, 0);
    image::alpha_composite(dst, short, 0, 0);
    assert(dst.pixels == before);
    // A place far outside changes nothing and does not abort.
    var small = image::new_image(2, 2, 9, 9, 9, 255);
    image::paste(dst, small, 2147483647, 2147483647);
    image::paste(dst, small, -2147483647, -2147483647);
    image::alpha_composite(dst, small, 2147483647, -2147483647);
    image::alpha_composite(dst, small, -100, -100);
    assert(dst.pixels == before);

    // An image with a huge size and a short buffer is empty. The count of
    // the pixels overflows when it is computed with the four bytes of each
    // pixel, so the test must not abort.
    let huge = image::Image { width: 2147483647, height: 2147483647, pixels: [1, 2, 3, 4], icc: [], exif: [] };
    assert(is_empty(image::crop(huge, 0, 0, 10, 10)));
    assert(is_empty(image::transpose(huge, 2)));
    assert(is_empty(image::rotate(huge, 30.0, true)));
    var hdst = image::new_image(2, 2, 1, 2, 3, 4);
    let hbefore = hdst.pixels;
    image::paste(hdst, huge, 0, 0);
    image::alpha_composite(hdst, huge, 0, 0);
    assert(hdst.pixels == hbefore);

    // An angle that is not a number gives an empty image.
    let nan_angle = 0.0 / 0.0;
    assert(is_empty(image::rotate(img, nan_angle, false)));
    assert(is_empty(image::rotate(img, nan_angle, true)));

    // An angle that is far too large gives an empty image.
    assert(is_empty(image::rotate(img, 1.0e30, false)));
    assert(is_empty(image::rotate(img, -1.0e30, true)));
    // An angle of a full turn gives the same pixels.
    let full = image::rotate(img, 360.0, false);
    assert(full.width == 4 && full.height == 3 && full.pixels == img.pixels);
    let twice = image::rotate(img, -360.0, true);
    assert(twice.pixels == img.pixels);
}

// grid makes an image with a different value in every pixel.
fn grid(w: i32, h: i32) -> image::Image {
    var img = image::new_image(w, h, 0, 0, 0, 255);
    for y: i32 in 0..h {
        for x: i32 in 0..w {
            image::set_pixel(img, x, y, (y * w + x) as u8, (x * 7) as u8, (y * 11) as u8, 255);
        }
    }
    return img;
}

// test_paste_values checks one copy and one blend by hand.
fn test_paste_values() {
    var dst = image::new_image(3, 2, 10, 20, 30, 200);
    var src = image::new_image(1, 1, 200, 100, 50, 128);
    image::paste(dst, src, 1, 1);
    let r, g, b, a = image::pixel(dst, 1, 1);
    assert(r == 200 && g == 100 && b == 50 && a == 128);
    let r0, g0, b0, a0 = image::pixel(dst, 0, 0);
    assert(r0 == 10 && g0 == 20 && b0 == 30 && a0 == 200);
    // The blend of the probe of the research file.
    var base = image::new_image(1, 1, 10, 20, 30, 200);
    image::alpha_composite(base, src, 0, 0);
    let r1, g1, b1, a1 = image::pixel(base, 0, 0);
    assert(r1 == 117 && g1 == 65 && b1 == 41 && a1 == 228);
    // A paste of an image onto itself reads the source as it was before.
    var overlap = grid(3, 3);
    image::paste(overlap, overlap, 1, 1);
    let p0, _, _, _ = image::pixel(overlap, 1, 1);
    let p1, _, _, _ = image::pixel(overlap, 2, 1);
    let p2, _, _, _ = image::pixel(overlap, 1, 2);
    let p3, _, _, _ = image::pixel(overlap, 2, 2);
    assert(p0 == 0 && p1 == 1 && p2 == 3 && p3 == 4);
    // A source with no alpha leaves the destination as it is.
    var clear = image::new_image(1, 1, 255, 255, 255, 0);
    image::alpha_composite(base, clear, 0, 0);
    let r2, g2, b2, a2 = image::pixel(base, 0, 0);
    assert(r2 == 117 && g2 == 65 && b2 == 41 && a2 == 228);
}

// inverse_of gives the operation that undoes the operation. The turns of
// 90 and 270 degrees undo each other. The other operations undo themselves.
fn inverse_of(op: i32) -> i32 {
    if op == image::TRANSPOSE_ROTATE_90 { return image::TRANSPOSE_ROTATE_270; }
    if op == image::TRANSPOSE_ROTATE_270 { return image::TRANSPOSE_ROTATE_90; }
    return op;
}

// check_inverse applies each operation and then its inverse to the image.
fn check_inverse(img: image::Image&) {
    for op: i32 in 0..7 {
        var once = image::transpose(img, op);
        let back = image::transpose(once, inverse_of(op));
        assert(back.width == img.width && back.height == img.height);
        assert(back.pixels == img.pixels);
    }
}

// test_transpose_inverse checks that an operation and its inverse give the
// source image back.
fn test_transpose_inverse() {
    var small = grid(3, 2);
    check_inverse(small);
    var square = grid(4, 4);
    check_inverse(square);
}

// test_geometry runs the cases that Pillow made. The file
// test/geometry_cases.goose holds them.
fn test_geometry() {
    test_geometry_contract();
    test_transpose_inverse();
    test_paste_values();
    geometry_cases::check_crop();
    geometry_cases::check_transpose();
    geometry_cases::check_paste();
    geometry_cases::check_blend();
    geometry_cases::check_rotate();
}


fn solid(w: i32, h: i32, r: u8, g: u8, b: u8, a: u8) -> image::Image {
    var px: u8[>..] = [];
    for i in 0..(w * h) { px.push(r); px.push(g); px.push(b); px.push(a); }
    return image::Image { width: w, height: h, pixels: px[..], icc: [], exif: [] };
}

fn frame(im: image::Image, delay: i32, disposal: i32) -> image::GifFrame {
    return image::GifFrame { image: im, delay: delay, disposal: disposal };
}

// frame_at gives a copy of frame i. Frames hold slices, so they cannot be
// indexed, only iterated.
fn frame_at(g: image::Gif&, i: i64) -> image::GifFrame {
    var k = 0;
    for f in g.frames {
        if k == i { return copy(f); }
        k += 1;
    }
    assert(false);
    return image::GifFrame { image: solid(1, 1, 0, 0, 0, 0), delay: 0, disposal: 0 };
}

// test_gif_round_trip writes frames and reads them again.
fn test_gif_round_trip() {
    // Frames of one palette share the global table. Frames of other colors
    // take a local table. The loop count and the delays survive.
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(solid(5, 4, 255, 0, 0, 255), 7, 1));
    fs.push(frame(solid(5, 4, 0, 255, 0, 255), 11, 2));
    fs.push(frame(solid(5, 4, 0, 0, 255, 0), 13, 3));
    let g = image::Gif { frames: fs[..], width: 5, height: 4, loop_count: 2 };
    let bytes = image::encode_gif(g);
    assert(bytes.len > 0);
    assert(bytes[0] == 71 && bytes[3] == 56 && bytes[4] == 57);
    let back = image::decode_gif(bytes);
    assert(back.frames.len == 3);
    assert(back.width == 5 && back.height == 4 && back.loop_count == 2);
    assert(frame_at(back, 0).delay == 7 && frame_at(back, 1).delay == 11 && frame_at(back, 2).delay == 13);
    assert(frame_at(back, 0).disposal == 1 && frame_at(back, 1).disposal == 2 && frame_at(back, 2).disposal == 3);
    assert(frame_at(back, 0).image.pixels == frame_at(g, 0).image.pixels);
    assert(frame_at(back, 1).image.pixels == frame_at(g, 1).image.pixels);
    // The third frame is clear. Its area was cleared by the disposal 2 of
    // the second frame, so the canvas shows the clear pixels.
    assert(frame_at(back, 2).image.pixels[3] == 0);

    // A frame smaller than the screen sits at the top left.
    var small: image::GifFrame[>..] = [];
    small.push(frame(solid(2, 2, 9, 8, 7, 255), 1, 0));
    let sg = image::Gif { frames: small[..], width: 6, height: 6, loop_count: -1 };
    let sb = image::encode_gif(sg);
    let sback = image::decode_gif(sb);
    assert(sback.frames.len == 1 && sback.width == 6 && sback.loop_count == -1);
    assert(frame_at(sback, 0).image.pixels[0] == 9);
    assert(frame_at(sback, 0).image.pixels[(1 * 6 + 5) * 4 + 3] == 0);

    // More than 256 colors in a frame still gives a valid file.
    var px: u8[>..] = [];
    for i in 0..(32 * 32) { px.push((i % 256) as u8); px.push((i / 4) as u8); px.push(0); px.push(255); }
    var many: image::GifFrame[>..] = [];
    many.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    let mg = image::Gif { frames: many[..], width: 32, height: 32, loop_count: 0 };
    let mb = image::encode_gif(mg);
    assert(image::decode_gif(mb).frames.len == 1);

    // Disposal 1 keeps a clear frame over the old one. Disposal 3 restores the
    // canvas from before the frame, which was empty.
    for d in [1, 3] {
        var ds: image::GifFrame[>..] = [];
        ds.push(frame(solid(3, 3, 200, 0, 0, 255), 1, d as i32));
        ds.push(frame(solid(3, 3, 0, 0, 0, 0), 1, 0));
        let dg = image::Gif { frames: ds[..], width: 3, height: 3, loop_count: 0 };
        let db = image::decode_gif(image::encode_gif(dg));
        assert(db.frames.len == 2);
        let second = frame_at(db, 1);
        // The second frame is clear, so the first frame shows through for 1.
        if d == 1 { assert(second.image.pixels[0] == 200 && second.image.pixels[3] == 255); }
        else { assert(second.image.pixels[0] == 0 && second.image.pixels[3] == 0); }
    }

    // No frame, a bad delay and a bad disposal are faults.
    var none: image::GifFrame[>..] = [];
    let ng = image::Gif { frames: none[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(ng).len == 0);
    var bad: image::GifFrame[>..] = [];
    bad.push(frame(solid(1, 1, 0, 0, 0, 255), 70000, 0));
    let bg = image::Gif { frames: bad[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(bg).len == 0);
    var bad2: image::GifFrame[>..] = [];
    bad2.push(frame(solid(1, 1, 0, 0, 0, 255), 1, 4));
    let bg2 = image::Gif { frames: bad2[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(bg2).len == 0);
    assert(image::decode_gif_path("test/no-such-file.gif").frames.len == 0);
    print("gif round trip ok\n");
}

// test_gif_hostile feeds broken files. Nothing aborts and each one ends fast.
fn test_gif_hostile() {
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(solid(6, 5, 1, 2, 3, 255), 4, 0));
    fs.push(frame(solid(6, 5, 4, 5, 6, 255), 4, 0));
    let hg = image::Gif { frames: fs[..], width: 6, height: 5, loop_count: 0 };
    let good = image::encode_gif(hg);
    assert(image::decode_gif(good).frames.len == 2);
    // Every cut is a fault or a shorter animation. Only the trailer may be missing.
    for n in 0..(good.len - 1) {
        let cut = image::decode_gif(good[..n]);
        assert(cut.frames.len < 2);
    }
    assert(image::decode_gif([]).frames.len == 0);
    // A bad LZW size byte: find the image descriptor and break the byte.
    var bad: u8[>..] = [];
    bad.append(good);
    var at = 0;
    while at < bad.len && bad[at] != 44 { at += 1; }
    // Descriptor is 10 bytes. A local table may follow.
    var k = at + 10;
    if (bad[at + 9] & 128) != 0 { k += 3 * (1 << ((bad[at + 9] & 7) + 1)); }
    bad[k] = 0;
    assert(image::decode_gif(bad[..]).frames.len == 0);
    bad[k] = 9;
    assert(image::decode_gif(bad[..]).frames.len == 0);
    // A screen of 65535 by 65535 in a file of 13 bytes.
    var huge: u8[>..] = [71, 73, 70, 56, 57, 97, 255, 255, 255, 255, 0, 0, 0, 59];
    assert(image::decode_gif(huge[..]).frames.len == 0);
    // A huge count of tiny frames gives a fault at the size cap or a long
    // list. Either way it ends fast.
    var many: u8[>..] = [71, 73, 70, 56, 57, 97, 200, 0, 200, 0, 128, 0, 0, 0, 0, 0, 255, 255, 255];
    for i in 0..3000 {
        many.append([44, 0, 0, 0, 0, 200, 0, 200, 0, 0, 2, 2, 68, 1, 0]);
    }
    many.push(59);
    let big = image::decode_gif(many[..]);
    assert(big.frames.len < 3000);
    print("gif hostile ok\n");
}

// test_hostile_size checks the calls that take a size or a place from the
// caller. Nothing aborts and nothing touches a wrong pixel.
fn test_hostile_size() {
    // The limit of the buffer: 2147483647 bytes.
    assert(image::size_ok(23170, 23170));
    assert(!image::size_ok(23171, 23170));
    assert(image::size_ok(536870911, 1));
    assert(!image::size_ok(536870912, 1));
    assert(!image::size_ok(0, 5));
    assert(!image::size_ok(5, 0));
    assert(!image::size_ok(-5, 3));
    assert(!image::size_ok(2147483647, 2147483647));

    // pixel and set_pixel on an image whose size does not match the buffer.
    var huge = image::Image { width: 2147483647, height: 2147483647, pixels: [1, 2, 3, 4], icc: [], exif: [] };
    let r, g, b, a = image::pixel(huge, 1, 1);
    assert(r == 0 && g == 0 && b == 0 && a == 0);
    let r0, g0, b0, a0 = image::pixel(huge, 0, 0);
    assert(r0 == 1 && g0 == 2 && b0 == 3 && a0 == 4);
    let r1, g1, b1, a1 = image::pixel(huge, 2147483646, 2147483646);
    assert(r1 == 0 && g1 == 0 && b1 == 0 && a1 == 0);
    image::set_pixel(huge, 1, 1, 9, 9, 9, 9);
    image::set_pixel(huge, 2147483646, 2147483646, 9, 9, 9, 9);
    image::set_pixel(huge, -1, 0, 9, 9, 9, 9);
    let first: u8[4] = [1, 2, 3, 4];
    assert(huge.pixels == first);
    image::set_pixel(huge, 0, 0, 5, 6, 7, 8);
    let second: u8[4] = [5, 6, 7, 8];
    assert(huge.pixels == second);

    // new_image gives the empty image for a bad size.
    let bad_w: i32[4] = [2147483647, -5, 0, 3];
    let bad_h: i32[4] = [2147483647, 3, 3, 0];
    for i in 0..4 {
        let e = image::new_image(bad_w[i], bad_h[i], 1, 2, 3, 4);
        assert(e.width == 0 && e.height == 0 && e.pixels.len == 0);
    }
    let big = image::new_image(536870912, 1, 1, 2, 3, 4);
    assert(big.width == 0 && big.height == 0 && big.pixels.len == 0);
    // A size that worked before gives the same image.
    let ok = image::new_image(3, 2, 1, 2, 3, 4);
    assert(ok.width == 3 && ok.height == 2 && ok.pixels.len == 24);
    assert(ok.pixels[0] == 1 && ok.pixels[1] == 2 && ok.pixels[2] == 3 && ok.pixels[3] == 4);
    assert(ok.pixels[23] == 4);

    // A buffer longer than the size needs: the size rules, the extra bytes stay.
    var longer = image::new_image(2, 4, 7, 7, 7, 7);
    longer.height = 2;
    image::set_pixel(longer, 1, 1, 9, 9, 9, 9);
    let lr, lg, lb, la = image::pixel(longer, 1, 1);
    assert(lr == 9 && lg == 9 && lb == 9 && la == 9);
    let nr, ng, nb, na = image::pixel(longer, 2, 0);
    assert(nr == 0 && ng == 0 && nb == 0 && na == 0);
    assert(longer.pixels.len == 32 && longer.pixels[16] == 7 && longer.pixels[31] == 7);

    // resize, fit, encode and write_with refuse the mismatched image.
    let rs = image::resize(huge, 4, 4, image::FILTER_BOX);
    assert(rs.width == 0 && rs.pixels.len == 0);
    let ft = image::fit(huge, 4, 4, image::FILTER_BOX);
    assert(ft.width == 0 && ft.pixels.len == 0);
    let small = image::new_image(2, 2, 1, 2, 3, 4);
    let rt = image::resize(small, 536870912, 1, image::FILTER_BOX);
    assert(rt.width == 0 && rt.pixels.len == 0);
    assert(image::encode(huge, image::FORMAT_PNG, 0).len == 0);
    assert(!image::write_with("tmp-hostile.png", huge, image::FORMAT_PNG, 0));
}


// The metadata cases: the ICC profile and the EXIF block ride with
// the file, on JPEG and on PNG.
fn meta_cases() {
    var jpg: u8[>..] = [];
    assert(read_file("test/meta-audit.jpg", jpg));
    assert(jpg.len > 0);
    let img = image::decode(jpg);
    assert(img.width == 4);
    assert(img.icc.len == 2048);
    assert(img.exif.len == 62);
    assert(img.exif[0] == 0x4d);

    // The tag reader: the description and the orientation.
    let text = image::exif_text(img.exif[..], 0x010e);
    assert(text == "goose image description");
    let orient = image::exif_short(img.exif[..], 0x0112);
    assert(orient == 1);

    // A round trip through encode keeps both fields, on JPEG and on
    // PNG.
    let jb = image::encode(img, image::FORMAT_JPEG, 90);
    let jback = image::decode(jb);
    assert(jback.icc[..] == img.icc[..]);
    assert(jback.exif[..] == img.exif[..]);
    let pb = image::encode(img, image::FORMAT_PNG, 8);
    let pback = image::decode(pb);
    assert(pback.icc[..] == img.icc[..]);
    assert(pback.exif[..] == img.exif[..]);

    // A write without metadata makes no marker.
    let plain = image::new_image(4, 4, 1, 2, 3, 255);
    let jb2 = image::encode(plain, image::FORMAT_JPEG, 90);
    let plainback = image::decode(jb2);
    assert(plainback.icc.len == 0);
    assert(plainback.exif.len == 0);

    // A truncated APP part never aborts and gives an empty field.
    var cut: u8[>..] = [];
    cut.append(jpg[..40]);
    let cback = image::decode(cut[..]);
    assert(cback.icc.len == 0);

    print("metadata cases ok\n");
}

// test_webp checks the codec: the files of Pillow, the writer of the package
// and the hostile bytes.
fn test_webp() {
    webp_cases::check_decode();
    webp_cases::check_hostile();

    // A lossless write keeps every pixel, also under a clear alpha.
    var img = make_busy();
    image::set_pixel(img, 3, 3, 12, 34, 56, 0);
    let ll = image::encode_webp(img, 100, true);
    assert(ll.len > 12 && ll[0] == 'R' && ll[8] == 'W');
    let back = image::decode(ll);
    assert(back.width == 32 && back.height == 32 && back.pixels == img.pixels);
    // The same bytes come out of a second write, and a second round is stable.
    assert(image::encode_webp(img, 100, true) == ll);
    assert(image::encode_webp(back, 100, true) == ll);

    // A lossy write keeps the size and a close color. The quality changes the size.
    let flat = image::new_image(64, 64, 90, 160, 30, 255);
    let lo = image::encode_webp(img, 10, false);
    let hi = image::encode_webp(img, 100, false);
    assert(lo.len > 0 && hi.len > lo.len);
    let lb = image::decode(hi);
    assert(lb.width == 32 && lb.height == 32);
    let r, g, b, a = image::pixel(lb, 16, 16);
    let r0, g0, b0, a0 = image::pixel(img, 16, 16);
    assert(near(r, r0, 30) && near(g, g0, 30) && near(b, b0, 30) && a == 255);
    let fb = image::decode(image::encode_webp(flat, 90, false));
    let fr, fg, fbl, fa = image::pixel(fb, 40, 40);
    assert(near(fr, 90, 6) && near(fg, 160, 6) && near(fbl, 30, 6) && fa == 255);
    // The lossy alpha survives.
    let tr = image::new_image(8, 8, 200, 100, 50, 77);
    let ta = image::decode(image::encode_webp(tr, 90, false));
    let ar, ag, ab, aa = image::pixel(ta, 4, 4);
    assert(aa == 77);

    // encode and write take the format. The param is the quality.
    let via = image::encode(img, image::FORMAT_WEBP, 50);
    assert(via.len > 0 && image::decode(via).width == 32);
    // The file that the Python check reads back with Pillow. It holds the
    // plain image, without the clear pixel of the test above.
    var plain = make_busy();
    assert(image::write("test-image.webp", plain, image::FORMAT_WEBP));
    let fl = image::load("test-image.webp");
    assert(fl.width == 32 && fl.height == 32);
    let fm = image::metadata("test-image.webp");
    assert(fm.ok && fm.kind == image::KIND_WEBP && fm.width == 32 && fm.height == 32);
    let mm = image::metadata_bytes(ll);
    assert(mm.ok && mm.kind == image::KIND_WEBP && mm.width == 32);
    // The old format numbers keep their values.
    assert(image::FORMAT_PNG == 0 && image::FORMAT_JPEG == 1 && image::FORMAT_BMP == 2);
    assert(image::FORMAT_TGA == 3 && image::FORMAT_HDR == 4 && image::FORMAT_WEBP == 5);
    assert(image::KIND_PIC == 9 && image::KIND_WEBP == 10);

    // Bad input to the writer gives the empty result.
    let none = image::Image { width: 4, height: 4, pixels: [], icc: [], exif: [] };
    assert(image::encode_webp(none, 80, false).len == 0);
    assert(image::encode_webp(none, 80, true).len == 0);
    let wide = image::Image { width: 16384, height: 1, pixels: [], icc: [], exif: [] };
    assert(image::encode_webp(wide, 80, true).len == 0);
    let big = image::new_image(16384, 1, 1, 2, 3, 4);
    assert(image::encode_webp(big, 80, true).len == 0);
    // A quality out of range does not abort.
    assert(image::encode_webp(img, -5, false).len > 0);
    assert(image::encode_webp(img, 900, false).len > 0);

    // Hostile bytes: a cut file, a wrong size, a header with no body.
    for n in 0..ll.len {
        if n % 7 != 0 { continue; }
        var cut: u8[>..] = [];
        cut.append(ll[..n]);
        let c = image::decode(cut[..]);
        assert(c.pixels.len == 0 || c.pixels.len == 32 * 32 * 4);
        image::metadata_bytes(cut[..]);
    }
    var cut2: u8[>..] = [];
    cut2.append(hi[..hi.len / 2]);
    assert(image::decode(cut2[..]).pixels.len == 0 || image::decode(cut2[..]).width == 32);
    assert(image::decode("RIFF").width == 0);
    assert(image::decode("RIFF\x04\x00\x00\x00WEBP").width == 0);
    assert(!image::metadata_bytes("RIFF\x04\x00\x00\x00WEBPVP8 ").ok);
    // A lossless header that claims 16384 x 16384 pixels and holds no data.
    var lie: u8[>..] = [];
    lie.append(ll[..30]);
    lie[4] = 0xff; lie[5] = 0xff; lie[6] = 0xff; lie[7] = 0x00;
    assert(image::decode(lie[..]).pixels.len == 0 || image::decode(lie[..]).width == 32);
    // The same file with a flipped dimension byte decodes to nothing or to a size
    // that matches the bytes.
    var bent: u8[>..] = [];
    bent.append(ll[..ll.len]);
    bent[21] = 0xff;
    bent[22] = 0xff;
    let bi = image::decode(bent[..]);
    assert(bi.pixels.len == bi.width * bi.height * 4);
    print("webp cases ok\n");
}

fn test_png16() {
    png16_cases::check_read16();
    png16_cases::check_exif_build();
    png16_cases::check_turn();

    // A round trip of 16 bits keeps every value, and the file is a PNG of depth 16.
    var px: u16[>..] = [];
    for i in 0..5 * 3 * 4 { px.push(((i * 4099 + 17) % 65536) as u16); }
    let img = image::Image16 { width: 5, height: 3, pixels: px[..] };
    let png = image::encode16(img);
    assert(png.len > 33 && png[24] == 16 && png[25] == 6);
    let back = image::decode16(png[..]);
    assert(back.width == 5 && back.height == 3 && back.pixels == img.pixels);
    assert(image::write16("test-image16.png", img));
    assert(image::load16("test-image16.png").pixels == img.pixels);
    // The 8-bit reader sees the high byte of a 16-bit file.
    let low = image::decode(png[..]);
    assert(low.width == 5 && low.pixels[0] == (img.pixels[0] >> 8) as u8);

    // The conversions: widen by 257, narrow with rounding, and a round trip.
    let e8 = image::new_image(2, 1, 0, 128, 255, 7);
    let e16 = image::to_image16(e8);
    assert(e16.pixels[0] == 0 && e16.pixels[1] == 32896 && e16.pixels[2] == 65535 && e16.pixels[3] == 1799);
    assert(image::to_image8(e16).pixels == e8.pixels);
    let ones = image::Image16 { width: 1, height: 1, pixels: [128, 129, 385, 65535] };
    let n8 = image::to_image8(ones);
    assert(n8.pixels[0] == 0 && n8.pixels[1] == 1 && n8.pixels[2] == 1 && n8.pixels[3] == 255);

    // Hostile input: nothing aborts and the answer is empty.
    assert(image::decode16([]).width == 0);
    assert(image::decode16(png[..20]).width == 0);
    let cutp = image::decode16(png[..png.len - 20]);
    assert(cutp.pixels.len == cutp.width * cutp.height * 4);
    var bad: u8[>..] = [];
    bad.append(png[..]);
    bad[40] = bad[40] ^ 255;
    let crc = image::decode16(bad[..]);
    assert(crc.pixels.len == crc.width * crc.height * 4);
    var big: u8[>..] = [];
    big.append(png[..33]);
    big[19] = 0x7f; big[20] = 0xff;
    assert(image::decode16(big[..]).width == 0);
    assert(image::load16("no-such-file.png").width == 0);
    let none = image::Image16 { width: 4, height: 4, pixels: [1, 2] };
    assert(image::encode16(none).len == 0);
    let short8 = image::Image { width: 4, height: 4, pixels: [1], icc: [], exif: [] };
    assert(image::to_image8(none).width == 0 && image::to_image16(short8).width == 0);
    let huge = image::Image16 { width: 2147483647, height: 2147483647, pixels: [1] };
    assert(image::encode16(huge).len == 0);

    // The orientation: none, out of range, a hostile block.
    let plain = image::new_image(2, 3, 1, 2, 3, 4);
    assert(image::orientation(plain) == 1);
    assert(image::apply_orientation(plain).pixels == plain.pixels);
    var wb = image::exif_new();
    image::exif_set_short(wb, 0x0112, 9);
    let odd = image::Image { width: 2, height: 3, pixels: copy(plain.pixels), icc: [], exif: image::exif_build(wb) };
    assert(image::orientation(odd) == 1);
    let junk = image::Image { width: 2, height: 3, pixels: copy(plain.pixels), icc: [], exif: [0x49, 0x49, 42, 0, 8, 0, 0, 0, 255, 255, 1, 2] };
    assert(image::apply_orientation(junk).pixels == plain.pixels);
    assert(image::exif_with_short([0x4d, 0x4d, 0, 42], 0x0112, 1).len == 4);
    var none_b = image::exif_new();
    assert(image::exif_build(none_b).len == 14);
    print("png16 cases ok\n");
}

fn main() {
    meta_cases();
    test_round_trip();
    test_info();
    test_load_failure();
    test_memory_round_trip();
    test_options();
    test_files();
    test_metadata();
    test_resize();
    test_geometry();
    test_hostile_size();
    test_gif_round_trip();
    test_gif_hostile();
    gif_cases::check_gif();
    jpeg_cases::check_jpeg();
    jpeg_cases::check_jpeg_hostile();
    test_webp();
    test_png16();
    print("all tests passed");
    return 0;
}
