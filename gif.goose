// The animated GIF codec of the image package.
//
// decode_gif reads every frame of a GIF. encode_gif writes a GIF of many
// frames. The C part is in src/gif.c.
namespace image;

import std;
import os;

extern fn img_gif_decode(data: const u8[:], out: u8[>..]&, meta: u8[>..]&) -> bool;
extern fn img_gif_encode(pixels: const u8[:], out: u8[>..]&, meta: const i32[:]) -> bool;

// GifFrame is one frame. The image is the whole canvas as it shows after
// the frame is drawn, with the disposal of the frames before applied. The
// delay is in hundredths of a second. The disposal is the method of the
// file: 0 and 1 keep the frame, 2 clears its area, 3 restores the canvas.
struct GifFrame {
    image: Image,
    delay: i32,
    disposal: i32,
}

// Gif holds the frames and the size of the screen. loop is the count of
// repeats from the Netscape extension: 0 loops for ever, and -1 means the
// file has no extension, so the animation plays once. (loop is a keyword.)
struct Gif {
    frames: GifFrame[],
    width: i32,
    height: i32,
    loop_count: i32,
}

fn empty_gif() -> Gif {
    return Gif { frames: [], width: 0, height: 0, loop_count: 0 };
}

fn le32(b: const u8[:], at: i64) -> i32 {
    let v: i64 = b[at] as i64 + (b[at + 1] as i64) * 256 + (b[at + 2] as i64) * 65536
        + (b[at + 3] as i64) * 16777216;
    if v >= 2147483648 { return (v - 4294967296) as i32; }
    return v as i32;
}

// decode_gif reads every frame of the GIF in the bytes. It returns a Gif
// with no frames when the bytes are not a whole GIF.
fn decode_gif(data: const u8[:]) -> Gif {
    var pixels: u8[>..] = [];
    var meta: u8[>..] = [];
    if !img_gif_decode(data, pixels, meta) { return empty_gif(); }
    let w = le32(meta[..], 0);
    let h = le32(meta[..], 4);
    let loops = le32(meta[..], 8);
    let size: i64 = w as i64 * h as i64 * 4;
    let count = (meta.len - 12) / 8;
    var frames: GifFrame[>..] = [];
    for i in 0..count {
        var px: u8[>..] = [];
        px.append(pixels[i * size..(i + 1) * size]);
        let delay = le32(meta[..], 12 + i * 8);
        let img = Image { width: w, height: h, pixels: px[..], icc: [], exif: [] };
        frames.push(GifFrame { image: copy(img), delay: delay, disposal: le32(meta[..], 16 + i * 8) });
    }
    return Gif { frames: frames[..], width: w, height: h, loop_count: loops };
}

// decode_gif_path reads every frame of the GIF file at the path.
fn decode_gif_path(path: const u8[:]) -> Gif {
    var data: u8[>..] = [];
    if !read_file(path, data) { return empty_gif(); }
    return decode_gif(data[..]);
}

// encode_gif writes the frames as a GIF89a. A frame may be smaller than
// the screen and sits at the top left. The result is empty when there is
// no frame, a frame does not fit the screen, a pixel count is wrong, a
// delay is outside 0 to 65535 or a disposal is outside 0 to 3.
fn encode_gif(gif: Gif&) -> u8[] {
    var pixels: u8[>..] = [];
    var meta: i32[>..] = [];
    meta.push(gif.width);
    meta.push(gif.height);
    meta.push(gif.loop_count);
    meta.push(gif.frames.len as i32);
    for f in gif.frames {
        let need: i64 = f.image.width as i64 * f.image.height as i64 * 4;
        if f.image.width < 1 || f.image.height < 1 || f.image.pixels.len != need { return []; }
        meta.push(f.image.width);
        meta.push(f.image.height);
        meta.push(f.delay);
        meta.push(f.disposal);
        pixels.append(f.image.pixels);
    }
    var out: u8[>..] = [];
    if !img_gif_encode(pixels[..], out, meta[..]) { return []; }
    return out[..];
}
