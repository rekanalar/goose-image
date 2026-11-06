// image - image decoding and encoding for Goose.
//
// The package reads PNG, JPEG, BMP, GIF and TGA files. It writes PNG, JPEG
// and BMP files. The pixels are RGBA, four bytes each.
//
// The C part of the package wraps stb_image and stb_image_write. The
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

// The C functions of the package. The definitions are in src/image.c.
extern fn img_load(path: const u8[:], out: u8[>..]&, w: i32&, h: i32&) -> bool;
extern fn img_info(path: const u8[:], w: i32&, h: i32&) -> bool;
extern fn img_write(path: const u8[:], pixels: const u8[:], w: i32, h: i32, format: i32) -> bool;

// The format numbers that img_write takes.
const FORMAT_PNG = 0;
const FORMAT_JPEG = 1;
const FORMAT_BMP = 2;

// Image holds the pixels of an image. The pixels are RGBA, four bytes for
// each pixel. The rows follow each other.
struct Image {
    width: i32,
    height: i32,
    pixels: u8[],
}

// load reads the image at the path. It returns an empty image when the
// file is not an image that the package reads.
fn load(path: const u8[:]) -> Image {
    var pixels: u8[>..] = [];
    var w: i32 = 0;
    var h: i32 = 0;
    if !img_load(path, pixels, w, h) {
        return Image { width: 0, height: 0, pixels: [] };
    }
    return Image { width: w, height: h, pixels: pixels[..] };
}

// info gives the width and the height of the image at the path. It returns
// false when the file is not an image that the package reads.
fn info(path: const u8[:]) -> i32, i32, bool {
    var w: i32 = 0;
    var h: i32 = 0;
    if !img_info(path, w, h) { return 0, 0, false; }
    return w, h, true;
}

// write writes the image to the path. The format is FORMAT_PNG,
// FORMAT_JPEG or FORMAT_BMP. It returns false when the write fails.
fn write(path: const u8[:], img: Image&, format: i32) -> bool {
    return img_write(path, img.pixels, img.width, img.height, format);
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

// pixel gives the color of one pixel. The color parts run from 0 to 255.
// The alpha part runs from 0 to 255. It returns 0, 0, 0, 0 when the place
// is outside the image.
fn pixel(img: Image&, x: i32, y: i32) -> u8, u8, u8, u8 {
    if x < 0 || y < 0 || x >= img.width || y >= img.height { return 0, 0, 0, 0; }
    let at = (y * img.width + x) * 4;
    if at + 3 >= img.pixels.len { return 0, 0, 0, 0; }
    return img.pixels[at], img.pixels[at + 1], img.pixels[at + 2], img.pixels[at + 3];
}

// set_pixel writes one color into the image. The color parts run from 0 to
// 255. It does nothing when the place is outside the image.
fn set_pixel(img: Image&, x: i32, y: i32, r: u8, g: u8, b: u8, a: u8) {
    if x < 0 || y < 0 || x >= img.width || y >= img.height { return; }
    let at = (y * img.width + x) * 4;
    if at + 3 >= img.pixels.len { return; }
    img.pixels[at] = r;
    img.pixels[at + 1] = g;
    img.pixels[at + 2] = b;
    img.pixels[at + 3] = a;
}

// new_image makes an image of the size, filled with one color. The color
// parts run from 0 to 255.
fn new_image(width: i32, height: i32, r: u8, g: u8, b: u8, a: u8) -> Image {
    var pixels: u8[>..] = [];
    for i in 0..width * height * 4 {
        pixels.push(if i % 4 == 0 { r } else if i % 4 == 1 { g } else if i % 4 == 2 { b } else { a });
    }
    return Image { width: width, height: height, pixels: pixels[..] };
}
