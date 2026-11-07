// An example for the image package.
//
// It makes an image, resizes it with two filters, encodes it to memory as
// PNG and JPEG, and reads the header of the bytes without a decode.
//
// Run with: goose run examples/thumbnail.goose

import std;
import image from "..";

fn main() {
    var img = image::new_image(200, 100, 0, 0, 0, 255);
    for y: i32 in 0..100 {
        for x: i32 in 0..200 {
            image::set_pixel(img, x, y, (x as u8) * 1, (y * 2) as u8, 90, 255);
        }
    }

    let thumb = image::fit(img, 64, 64, image::FILTER_LANCZOS);
    print("thumbnail: ", thumb.width, "x", thumb.height);

    let png = image::encode(thumb, image::FORMAT_PNG, 9);
    let jpg = image::encode(thumb, image::FORMAT_JPEG, 60);
    print("png bytes: ", png.len, ", jpeg bytes: ", jpg.len);

    let m = image::metadata_bytes(jpg);
    print("jpeg header: ", m.width, "x", m.height, ", channels ", m.channels,
          ", kind ", m.kind, " (jpeg is ", image::KIND_JPEG, ")");
    return 0;
}
