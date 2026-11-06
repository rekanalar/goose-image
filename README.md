# goose-image

[![ci](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml/badge.svg)](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml)

Image decoding and encoding for the Goose programming language.

## Features

- Reads PNG, JPEG, BMP, GIF, and TGA.
- Writes PNG, JPEG, and BMP.
- The pixels are RGBA, four bytes for each pixel.
- No system library. The stb headers are part of the package.

## Install

Put this line in the root file of your program:

```goose
import image from "rekanalar/goose-image@v0.1.1";
```

The first run builds the C part of the package. It needs a C compiler on
the machine. It builds once, and the build is shared by every program.

## Example

```goose
import std;
import image from "rekanalar/goose-image@v0.1.1";

fn main() {
    var img = image::load("photo.png");
    print(img.width, "x", img.height);
    image::write_jpeg("photo.jpg", img);
    return 0;
}
```

## Functions

| Function | Result |
|---|---|
| `load(path)` | the image at the path, or an empty image |
| `info(path)` | the width, the height, and whether the file is an image |
| `write(path, img, format)` | writes the image; the format is a FORMAT constant |
| `write_png`, `write_jpeg`, `write_bmp` | the writes for one format |
| `pixel(img, x, y)` | the color of one pixel, as four parts |
| `set_pixel(img, x, y, r, g, b, a)` | writes one color into the image |
| `new_image(w, h, r, g, b, a)` | makes an image of the size in one color |

## The Image value

```goose
struct Image {
    width: i32,
    height: i32,
    pixels: u8[],   // RGBA, four bytes for each pixel, row by row
}
```

A place outside the image gives the empty color. A write to it does
nothing.

## The C part

The package wraps stb_image and stb_image_write. The two headers are in
the vendor directory. They are public domain. The package reads the image
files itself, so a path holds bytes with a length, as every Goose path
does.

## Test

```bash
goose run test/run.goose
goose run examples/gradient.goose
```

## License

MIT. The stb headers are public domain.
