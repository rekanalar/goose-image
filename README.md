# goose-image

[![ci](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml/badge.svg)](https://github.com/rekanalar/goose-image/actions/workflows/ci.yml)

Image decoding and encoding for the [Goose](https://github.com/aardappel/goose) programming language.

## Features

- Reads PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PNM, PIC, WebP, TIFF and AVIF.
- Writes PNG, JPEG, BMP, TGA, HDR, GIF, WebP, TIFF and AVIF, to a file or to memory.
- Reads and writes every frame of an animated GIF, with delay, disposal and loop count.
- Writes JPEG with a quality, progressive mode, chroma choice and optimized tables.
- Writes WebP lossy or lossless. The WebP code is libwebp 1.5.0, vendored.
- Reads and writes TIFF with none, PackBits, LZW and Deflate compression.
- Reads and writes AVIF. The AV1 codec is dav1d 1.5.1 for the read and
  libaom 3.12.1 for the write. Both are vendored.
- Reads and writes 16-bit PNG as `Image16`.
- Builds an EXIF block and applies the orientation tag.
- Resizes with seven filters, and fits an image into a box.
- Crops, flips, turns and rotates an image, as Pillow does.
- Pastes one image onto another, as a copy or as an alpha blend.
- Reads the size, the channel count and the format from a header.
- The pixels are RGBA, four bytes for each pixel.
- No system library. The stb headers and libwebp are part of the package.

## Metadata

The image carries two forms of metadata: the ICC profile and the EXIF
block. A `load` or a `decode` fills the fields `icc` and `exif` of the
`Image`. An `encode` or a write puts them back:

- A JPEG takes the profile in APP2 markers of ICCPROFILE. A profile
  of more than one part comes out as more than one marker. The EXIF
  goes into one APP1 marker of Exif.
- A PNG takes the profile in an iCCP chunk, and the EXIF in an eXIf
  chunk.

The `exif` field holds the raw TIFF block. Three calls read it:

| Function | Result |
|---|---|
| `exif_value(block, tag)` | the raw bytes of the value of one tag |
| `exif_text(block, tag)` | the value as a text, when it is ASCII |
| `exif_short(block, tag)` | the value as a number, when it is a SHORT or a LONG |

```goose
let img = image::load("photo.jpg");
let text = image::exif_text(img.exif, 0x010e);  // the description
let orient = image::exif_short(img.exif, 0x0112);  // the orientation
```

A round trip of load and save keeps the bytes of both fields on JPEG
and on PNG. The check ran the round trip against Pillow 13.0.0.

## Limits

- GIF: the writer builds a median-cut palette of 256 colors for each frame.
  It cuts a frame to the box that changed. Disposal 3 is read, and the
  writer keeps whole frames when a frame uses it. The canvases of one GIF
  may hold 64 MiB at most.
- JPEG: the decoder is stb_image. For 4:2:2 files its pixels differ a
  little from libjpeg. The encoder is the package's own.
- WebP: the package reads and writes no ICC profile and no EXIF block in
  WebP. The SIMD code of libwebp is off, so the codec is plain C. An
  animated WebP reads as an empty image.
- TIFF read: the reader decodes the first page. `decode_tiff_pages` counts
  the rest. A 16-bit sample gives its high byte. Pillow clips 16-bit TIFF
  to 255, so Pillow gives no oracle for this.
- TIFF write: the writer makes little-endian RGBA. Its Deflate coder uses
  fixed Huffman codes, so the output is larger than zlib output. It writes
  no ICC profile and no EXIF block.
- AVIF read: the reader takes the first page of a still file. It reads
  8-bit and 10-bit, with or without alpha. A 4:2:0 file upsamples the
  chroma with a bilinear filter. The pixels differ a little from
  libavif near the edges. An animated file gives the empty image.
- AVIF write: the writer makes 8-bit files. It writes 4:2:0, or 4:4:4 with
  the identity matrix when the quality is 100. The pixels then round trip
  exactly. The alpha goes into a second item at the same quality. The
  writer puts no ICC profile and no EXIF block into the file.
- 16-bit: `decode16` reads 16-bit PNG. Other formats come back as 8-bit
  values widened by 257. `to_image8` rounds. stb_image and Go use a shift
  and Rust rounds, so the references differ.
- EXIF: the reader names one tag per call and reads the first list of
  tags. It reads no maker note and no GPS list. The builder writes the first
  list only, in little-endian order, with SHORT, LONG, ASCII and RATIONAL
  values. `apply_orientation` sets the tag to 1 in the copy. Pillow deletes
  the tag. The pixels equal Pillow.
- HDR is written without alpha. It uses a 2.2 gamma, so the 8-bit values
  round trip within a small error.
- PNG level 1 to 9 selects the match search of the stb deflate. The output
  is larger than zlib output. stb_image does not check the CRC of a PNG
  chunk.
- Every read other than `decode16` gives 8-bit RGBA.
- The resize works on whole images. It has no alpha premultiply control. The
  edge mode is clamp.
- The geometry calls follow the rules in the section "Geometry". `paste` has no
  mask, and `rotate` has no center, no translate, no fill color and no filter.
  The sampling of `rotate` is nearest.
- The filters are those of stb_image_resize2. They are not the filters of
  Pillow, so the pixels differ from Pillow output.
- A hostile `Image` never makes a call abort. The width, the height and the
  buffer may disagree. `pixel` then gives 0, 0, 0, 0 and `set_pixel` changes
  nothing. `new_image` gives the empty image for a side at or below zero.
  It does the same when the buffer would pass 2147483647 bytes. `resize`,
  `fit` and `encode` give the empty image or false in that case.
- A path longer than 4095 bytes fails. A file over 2 GiB fails.

## Install

Put this line in the root file of your program:

```goose
import image from "rekanalar/goose-image@v1.0.0";
```

The first run builds the C part of the package. It needs a C compiler on
the machine. It builds once, and the build is shared by every program.

The CI runs the gate on Linux, macOS and Windows. It uses Goose v2026.4
and v2026.3 on each.

## Example

```goose
import std;
import image from "rekanalar/goose-image@v1.0.0";

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
| `encode_jpeg(img, quality, progressive, subsampling)`, `write_jpeg_with` | JPEG with a mode and a chroma choice. `SUBSAMPLING_444`, `_422`, `_420`. |
| `encode_jpeg_opts(..., optimize)`, `write_jpeg_opts` | the same, with Huffman tables built for the image |
| `encode_webp(img, quality, lossless)` | WebP bytes. Quality is 0 to 100. |
| `encode_tiff(img, compression)`, `write_tiff`, `write_tiff_with` | TIFF bytes or file. `TIFF_COMPRESSION_NONE`, `_LZW`, `_DEFLATE`. |
| `decode_tiff_pages(bytes)` | the count of pages in a TIFF |
| `decode_avif(bytes)`, `load_avif(path)` | the image in an AVIF file, or an empty image |
| `decode_avif16(bytes)` | the 16-bit image in an AVIF file |
| `encode_avif(img, quality, speed)` | AVIF bytes. The quality is 0 to 100 and 75 is the default. |
| `write_avif(path, img)`, `write_avif_with(path, img, quality, speed)` | the write of an AVIF file |
| `decode_gif(bytes)`, `decode_gif_path(path)` | a `Gif`: frames, width, height, loop count |
| `encode_gif(gif)` | the bytes of an animated GIF |
| `decode16(bytes)`, `load16(path)` | an `Image16` with 16-bit values |
| `encode16(img16)`, `write16(path, img16)` | a 16-bit PNG |
| `to_image16(img)`, `to_image8(img16)` | the conversions between the two forms |
| `orientation(img)` | the EXIF orientation, 1 to 8, or 1 |
| `apply_orientation(img)` | the image turned upright |
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
| `exif_value(block, tag)`, `exif_text`, `exif_short` | read one tag of the EXIF block |
| `exif_new()`, `exif_set_short`, `exif_set_long`, `exif_set_text`, `exif_set_rational`, `exif_build(b)` | build an EXIF block |
| `exif_with_short(block, tag, v)` | the block with one SHORT value changed |

## Constants

- FORMAT: `FORMAT_PNG`, `FORMAT_JPEG`, `FORMAT_BMP`, `FORMAT_TGA`, `FORMAT_HDR`,
  `FORMAT_WEBP`, `FORMAT_TIFF`, `FORMAT_AVIF`.
- FILTER: `FILTER_DEFAULT` (Mitchell down, Catmull-Rom up), `FILTER_NEAREST`,
  `FILTER_BOX`, `FILTER_BILINEAR`, `FILTER_BICUBIC`, `FILTER_MITCHELL`,
  `FILTER_BSPLINE`, `FILTER_LANCZOS`.
- TRANSPOSE: `TRANSPOSE_FLIP_LEFT_RIGHT` (0), `TRANSPOSE_FLIP_TOP_BOTTOM` (1),
  `TRANSPOSE_ROTATE_90` (2), `TRANSPOSE_ROTATE_180` (3), `TRANSPOSE_ROTATE_270` (4),
  `TRANSPOSE_TRANSPOSE` (5), `TRANSPOSE_TRANSVERSE` (6). The numbers are those of Pillow.
- KIND: `KIND_UNKNOWN`, `KIND_PNG`, `KIND_JPEG`, `KIND_BMP`, `KIND_GIF`,
  `KIND_TGA`, `KIND_PSD`, `KIND_HDR`, `KIND_PNM`, `KIND_PIC`, `KIND_WEBP`, `KIND_TIFF`,
  `KIND_AVIF`.

The `param` of `encode` and `write_with` means one thing for each format.
A value of 0 selects the default.

- JPEG: the quality, 1 to 100. The default is 90.
- PNG: the level, 1 to 9. The default is 8.
- TGA: 1 is run-length on and 2 is off.
- WebP: the lossy quality, 1 to 100. The default is 80.
- TIFF: the compression. 0 means none.
- AVIF: the quality, 1 to 100. The default is 75.
- Other formats ignore it.

## The Image value

```goose
struct Image {
    width: i32,
    height: i32,
    pixels: u8[],   // RGBA, four bytes for each pixel, row by row
    icc: u8[],      // the bytes of the ICC profile, empty when none
    exif: u8[],     // the TIFF block of the EXIF, empty when none
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

Three codecs are vendored with their own license files:

- libwebp 1.5.0 in `vendor/libwebp`. BSD 3-clause.
- dav1d 1.5.1 in `vendor/dav1d`. BSD 2-clause.
- libaom 3.12.1 in `vendor/aom`. BSD 2-clause, with the AOMedia Patent
  License 1.0 in `vendor/aom/PATENTS`.

Each codec builds as plain C. No SIMD code and no thread starts. The
JPEG, GIF and TIFF codecs are the code of this package.

## Test

```bash
goose run test/run.goose
goose run examples/gradient.goose
goose run examples/thumbnail.goose
goose run examples/geometry.goose
goose run examples/webp.goose
goose run examples/tiff.goose
goose run examples/avif.goose
```

## License

MIT. The stb headers are public domain, and stb_image_resize2 is also MIT.
The vendored codecs keep their own licenses. See the section "The C part".
