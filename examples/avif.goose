// An example for the image package.
//
// It makes a gradient image, writes it as an AVIF file, reads it back, and
// prints the size.
//
// Run with: goose run examples/avif.goose

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

    // The write takes the quality. A quality of 100 keeps every pixel.
    assert(image::write_avif_with("gradient.avif", img, 100, 6));
    let w, h, ok = image::info("gradient.avif");
    print("wrote gradient.avif: ", w, "x", h, " ok=", ok);

    let bytes = image::encode_avif(img, 100, 6);
    print("encoded bytes: ", bytes.len);

    var back = image::load_avif("gradient.avif");
    let r, g, b, a = image::pixel(back, 32, 32);
    print("middle pixel: ", r, " ", g, " ", b, " ", a);
    return 0;
}
