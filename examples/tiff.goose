// An example for the image package.
//
// It makes a gradient image, writes it as a TIFF file with each compression,
// reads one back, and prints the sizes.
//
// Run with: goose run examples/tiff.goose

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

    // The writer gives the little endian form. The compression is a choice.
    let none = image::encode_tiff(img, image::TIFF_COMPRESSION_NONE);
    let lzw = image::encode_tiff(img, image::TIFF_COMPRESSION_LZW);
    let deflate = image::encode_tiff(img, image::TIFF_COMPRESSION_DEFLATE);
    print("bytes: none ", none.len, ", lzw ", lzw.len, ", deflate ", deflate.len);

    assert(image::write_tiff_with("gradient.tif", img, image::TIFF_COMPRESSION_DEFLATE));
    let m = image::metadata("gradient.tif");
    print("kind ", m.kind, ", ", m.width, "x", m.height, ", pages ", image::decode_tiff_pages(deflate));

    // A TIFF file reads like any other format. Every pixel comes back.
    let back = image::load("gradient.tif");
    let r, g, b, a = image::pixel(back, 32, 32);
    print("middle pixel: ", r, " ", g, " ", b, " ", a);
    return 0;
}
