// An example for the image package.
//
// It makes a gradient image, writes it as a WebP file, reads it back, and
// prints the size.
//
// Run with: goose run examples/webp.goose

import std;
import image from "..";

fn main() {
    // A 64x64 gradient: red grows to the right, green grows down.
    var img = image::new_image(64, 64, 0, 0, 0, 255);
    for y: i32 in 0..64 {
        for x: i32 in 0..64 {
            image::set_pixel(img, x, y, (x * 4) as u8, (y * 4) as u8, 128, 255);
        }
    }

    // The write takes the quality. A lossless write keeps every pixel.
    assert(image::write_with("gradient.webp", img, image::FORMAT_WEBP, 90));
    let w, h, ok = image::info("gradient.webp");
    print("wrote gradient.webp: ", w, "x", h, " ok=", ok);

    let bytes = image::encode_webp(img, 90, false);
    print("encoded bytes: ", bytes.len);

    var back = image::load("gradient.webp");
    let r, g, b, a = image::pixel(back, 32, 32);
    print("middle pixel: ", r, " ", g, " ", b, " ", a);
    return 0;
}
