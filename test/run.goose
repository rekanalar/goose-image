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

fn main() {
    test_round_trip();
    test_info();
    test_load_failure();
    print("all tests passed");
    return 0;
}
