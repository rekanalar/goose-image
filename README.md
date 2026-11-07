# goose-image

[![ci](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml/badge.svg)](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml)

Image decoding and encoding for the [Goose](https://github.com/aardappel/goose) programming language.

## Features

- Reads PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PNM and PIC.
- Writes PNG, JPEG, BMP, TGA and HDR, to a file or to memory.
- Sets the JPEG quality, the PNG level and the TGA run-length flag.
- Resizes with seven filters, and fits an image into a box.
- Crops, flips, turns and rotates an image, as Pillow does.
- Pastes one image onto another, as a copy or as an alpha blend.
- Reads the size, the channel count and the format from a header.
- The pixels are RGBA, four bytes for each pixel.
- No system library. The stb headers are part of the package.

## Limits

- No WebP. The stb headers cannot write it or read it. The package
  vendors no WebP code.
- No AVIF, TIFF, GIF write or animation. A GIF read gives the first frame.
- No EXIF, no ICC profile and no orientation. The package does not read them.
- Every read gives 8-bit RGBA. A 16-bit PNG loses its low byte.
- HDR is written without alpha. It uses a 2.2 gamma, so the 8-bit values
  round trip within a small error.
- JPEG writes have no progressive mode, no chroma choice and no optimize.
  The stb encoder fixes them.
- PNG level 1 to 9 selects the match search of the stb deflate. The output
  is larger than zlib output.
- The resize works on whole images. It has no alpha premultiply control. The
  edge mode is clamp.
- The geometry calls follow the rules in the section "Geometry". `paste` has no
  mask, and `rotate` has no center, no translate, no fill color and no filter.
  The sampling of `rotate` is nearest.
- The filters are those of stb_image_resize2. They are not the filters of
  Pillow, so the pixels differ from Pillow output.
- A path longer than 4095 bytes fails. A file over 2 GiB fails.

## Install

Put this line in the root file of your program:

```goose
import image from "rekanalar/goose-image@v0.3.0";
```

The first run builds the C part of the package. It needs a C compiler on
the machine. It builds once, and the build is shared by every program.

## Example

```goose
import std;
import image from "rekanalar/goose-image@v0.3.0";

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
| `decode(bytes)` | the image in encoded bytes, or an empty image |
| `info(path)` | the width, the height, and whether the file is an image |
| `metadata(path)`, `metadata_bytes(bytes)` | a `Meta`: width, height, channels, kind, ok; no pixel decode |
| `encode(img, format, param)` | the encoded bytes in memory; empty on failure |
| `write(path, img, format)` | writes the image; the format is a FORMAT constant |
| `write_with(path, img, format, param)` | the same, with an option |
| `write_png`, `write_jpeg`, `write_bmp`, `write_tga`, `write_hdr` | the writes for one format |
| `resize(img, w, h, filter)` | the scaled image; empty for a bad size or filter |
| `fit(img, max_w, max_h, filter)` | the scaled image inside the box, same aspect ratio |
| `crop(img, left, top, right, bottom)` | the box of the image. A pixel outside the image is `0, 0, 0, 0`. |
| `transpose(img, op)` | the flipped or turned image. `op` is a TRANSPOSE constant. |
| `rotate(img, degrees, expand)` | the image turned counter-clockwise, with nearest sampling |
| `paste(dst, src, x, y)` | copies `src` into `dst` at the place. It clips at the edges. |
| `alpha_composite(dst, src, x, y)` | blends `src` over `dst` at the place. It clips at the edges. |
| `pixel(img, x, y)` | the color of one pixel, as four parts |
| `set_pixel(img, x, y, r, g, b, a)` | writes one color into the image |
| `new_image(w, h, r, g, b, a)` | makes an image of the size in one color |

## Constants

- FORMAT: `FORMAT_PNG`, `FORMAT_JPEG`, `FORMAT_BMP`, `FORMAT_TGA`, `FORMAT_HDR`.
- FILTER: `FILTER_DEFAULT` (Mitchell down, Catmull-Rom up), `FILTER_NEAREST`,
  `FILTER_BOX`, `FILTER_BILINEAR`, `FILTER_BICUBIC`, `FILTER_MITCHELL`,
  `FILTER_BSPLINE`, `FILTER_LANCZOS`.
- TRANSPOSE: `TRANSPOSE_FLIP_LEFT_RIGHT` (0), `TRANSPOSE_FLIP_TOP_BOTTOM` (1),
  `TRANSPOSE_ROTATE_90` (2), `TRANSPOSE_ROTATE_180` (3), `TRANSPOSE_ROTATE_270` (4),
  `TRANSPOSE_TRANSPOSE` (5), `TRANSPOSE_TRANSVERSE` (6). The numbers are those of Pillow.
- KIND: `KIND_UNKNOWN`, `KIND_PNG`, `KIND_JPEG`, `KIND_BMP`, `KIND_GIF`,
  `KIND_TGA`, `KIND_PSD`, `KIND_HDR`, `KIND_PNM`, `KIND_PIC`.

The `param` of `encode` and `write_with` means one thing for each format.
For JPEG it is the quality, 1 to 100, and 90 is the default. For PNG it
is the level, 1 to 9, and 8 is the default. For TGA, 1 is run-length on
and 2 is off. Other formats ignore it. A value of 0 selects the default.

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

## Geometry

The geometry calls work on a whole image. They are pure Goose. They follow
Pillow `Image.crop`, `Image.transpose`, `Image.rotate`, `Image.paste` and
`Image.alpha_composite`.

```goose
var part = image::crop(img, 1, 1, 3, 3);                       // the middle 2x2 pixels
var turned = image::transpose(img, image::TRANSPOSE_ROTATE_90);
var tilted = image::rotate(img, 45.0, true);
image::paste(canvas, part, 2, 1);
image::alpha_composite(canvas, dot, 0, 0);
```

- The box of `crop` holds the pixels from `left` up to `right`, and from `top`
  up to `bottom`. The right and bottom edges are not part of the box.
- The turns of `transpose` are counter-clockwise.
- `rotate` fills a pixel that has no source with `0, 0, 0, 0`. With `expand`,
  the result is large enough to hold the whole image.
- `paste` and `alpha_composite` change `dst`. They read the source first, so an
  image may be pasted onto itself.
- No call changes its source image. No call reads or writes outside a buffer.

An empty result has a width and a height of 0. A call gives it for these cases:

- a box without area
- an unknown operation
- an image with no pixels, or with a short buffer
- a result of more than 1073741824 bytes
- an angle above 1000000000 degrees
- an angle that is not a number

The differences from Pillow:

- `crop` with an inverted box gives an empty image. Pillow raises an error. An
  empty box gives a side of 0 in Pillow, and a width and height of 0 here.
- `alpha_composite` changes `dst`. Pillow gives a new image.
- `paste` takes no mask.
- `rotate` takes no `center`, `translate`, `fillcolor` or `resample` option.

The check against Pillow 12.3.0 ran on small images from a fixed seed. The
script `test/make_geometry_cases.py` made 516 cases: 67 crops, 42 transposes, 30
pastes, 41 blends and 336 rotations.

Each pixel equals Pillow. The blend uses
the integer formula of Pillow `AlphaComposite.c`. The rotation uses the 16.16
fixed point sampling of Pillow. A float sampling differs at the edge of a
pixel. The file `test/geometry_cases.goose` holds the cases.

## The C part

The package wraps stb_image, stb_image_write and stb_image_resize2. The
three headers are in the vendor directory. The first two are public
domain. The third is public domain or MIT. The package reads the image
files itself, so a path holds bytes with a length, as every Goose path
does.

## Test

```bash
goose run test/run.goose
goose run examples/gradient.goose
goose run examples/thumbnail.goose
goose run examples/geometry.goose
```

## License

MIT. The stb headers are public domain, and stb_image_resize2 is also MIT.
