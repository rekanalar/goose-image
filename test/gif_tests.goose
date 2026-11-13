// Tests of the GIF encoder and decoder for the image package.
//
namespace gif_tests;

import std;
import os;
import image from "..";

fn solid(w: i32, h: i32, r: u8, g: u8, b: u8, a: u8) -> image::Image {
    var px: u8[>..] = [];
    for i in 0..(w * h) { px.push(r); px.push(g); px.push(b); px.push(a); }
    return image::Image { width: w, height: h, pixels: px[..], icc: [], exif: [] };
}

fn frame(im: image::Image, delay: i32, disposal: i32) -> image::GifFrame {
    return image::GifFrame { image: im, delay: delay, disposal: disposal };
}

// frame_at gives a copy of frame i. Frames hold slices, so they cannot be
// indexed, only iterated.
fn frame_at(g: image::Gif&, i: i64) -> image::GifFrame {
    var k = 0;
    for f in g.frames {
        if k == i { return copy(f); }
        k += 1;
    }
    assert(false);
    return image::GifFrame { image: solid(1, 1, 0, 0, 0, 0), delay: 0, disposal: 0 };
}

// test_gif_round_trip writes frames and reads them again.
fn test_gif_round_trip() {
    // Frames of one palette share the global table. Frames of other colors
    // take a local table. The loop count and the delays survive.
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(solid(5, 4, 255, 0, 0, 255), 7, 1));
    fs.push(frame(solid(5, 4, 0, 255, 0, 255), 11, 2));
    fs.push(frame(solid(5, 4, 0, 0, 255, 0), 13, 3));
    let g = image::Gif { frames: fs[..], width: 5, height: 4, loop_count: 2 };
    let bytes = image::encode_gif(g);
    assert(bytes.len > 0);
    assert(bytes[0] == 71 && bytes[3] == 56 && bytes[4] == 57);
    let back = image::decode_gif(bytes);
    assert(back.frames.len == 3);
    assert(back.width == 5 && back.height == 4 && back.loop_count == 2);
    assert(frame_at(back, 0).delay == 7 && frame_at(back, 1).delay == 11 && frame_at(back, 2).delay == 13);
    assert(frame_at(back, 0).disposal == 1 && frame_at(back, 1).disposal == 2 && frame_at(back, 2).disposal == 3);
    assert(frame_at(back, 0).image.pixels == frame_at(g, 0).image.pixels);
    assert(frame_at(back, 1).image.pixels == frame_at(g, 1).image.pixels);
    // The third frame is clear. Its area was cleared by the disposal 2 of
    // the second frame, so the canvas shows the clear pixels.
    assert(frame_at(back, 2).image.pixels[3] == 0);

    // A frame smaller than the screen sits at the top left.
    var small: image::GifFrame[>..] = [];
    small.push(frame(solid(2, 2, 9, 8, 7, 255), 1, 0));
    let sg = image::Gif { frames: small[..], width: 6, height: 6, loop_count: -1 };
    let sb = image::encode_gif(sg);
    let sback = image::decode_gif(sb);
    assert(sback.frames.len == 1 && sback.width == 6 && sback.loop_count == -1);
    assert(frame_at(sback, 0).image.pixels[0] == 9);
    assert(frame_at(sback, 0).image.pixels[(1 * 6 + 5) * 4 + 3] == 0);

    // More than 256 colors in a frame still gives a valid file.
    var px: u8[>..] = [];
    for i in 0..(32 * 32) { px.push((i % 256) as u8); px.push((i / 4) as u8); px.push(0); px.push(255); }
    var many: image::GifFrame[>..] = [];
    many.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    let mg = image::Gif { frames: many[..], width: 32, height: 32, loop_count: 0 };
    let mb = image::encode_gif(mg);
    assert(image::decode_gif(mb).frames.len == 1);

    // Disposal 1 keeps a clear frame over the old one. Disposal 3 restores the
    // canvas from before the frame, which was empty.
    for d in [1, 3] {
        var ds: image::GifFrame[>..] = [];
        ds.push(frame(solid(3, 3, 200, 0, 0, 255), 1, d as i32));
        ds.push(frame(solid(3, 3, 0, 0, 0, 0), 1, 0));
        let dg = image::Gif { frames: ds[..], width: 3, height: 3, loop_count: 0 };
        let db = image::decode_gif(image::encode_gif(dg));
        assert(db.frames.len == 2);
        let second = frame_at(db, 1);
        // The second frame is clear, so the first frame shows through for 1.
        if d == 1 { assert(second.image.pixels[0] == 200 && second.image.pixels[3] == 255); }
        else { assert(second.image.pixels[0] == 0 && second.image.pixels[3] == 0); }
    }

    // No frame, a bad delay and a bad disposal are faults.
    var none: image::GifFrame[>..] = [];
    let ng = image::Gif { frames: none[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(ng).len == 0);
    var bad: image::GifFrame[>..] = [];
    bad.push(frame(solid(1, 1, 0, 0, 0, 255), 70000, 0));
    let bg = image::Gif { frames: bad[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(bg).len == 0);
    var bad2: image::GifFrame[>..] = [];
    bad2.push(frame(solid(1, 1, 0, 0, 0, 255), 1, 4));
    let bg2 = image::Gif { frames: bad2[..], width: 1, height: 1, loop_count: 0 };
    assert(image::encode_gif(bg2).len == 0);
    assert(image::decode_gif_path("test/no-such-file.gif").frames.len == 0);
    print("gif round trip ok\n");
}

// test_gif_quant writes a frame with more colors than the table of a GIF.
// The writer cuts the colors, so the pixels come back close to the source.
fn test_gif_quant() {
    var px: u8[>..] = [];
    for i in 0..(32 * 32) {
        px.push((i % 256) as u8);
        px.push((i / 4) as u8);
        px.push((i * 3 % 256) as u8);
        px.push(255);
    }
    let img = image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] };
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(copy(img), 0, 0));
    let g = image::Gif { frames: fs[..], width: 32, height: 32, loop_count: 0 };
    let bytes = image::encode_gif(g);
    assert(bytes.len > 0);
    let back = image::decode_gif(bytes);
    assert(back.frames.len == 1);
    let fr = frame_at(back, 0);
    let got = fr.image.pixels;
    // The mean error of the colors, times 1000.
    var sum: i64 = 0;
    var i: i64 = 0;
    while i < 32 * 32 {
        for c in 0..3 {
            var d = got[i * 4 + c] as i64 - px[i * 4 + c] as i64;
            if d < 0 { d = -d; }
            sum += d;
        }
        i += 1;
    }
    assert(sum * 1000 / (32 * 32 * 3) < 12000);
    // The same image again gives the same file.
    assert(image::encode_gif(g) == bytes);
    print("gif quant ok\n");
}

// test_gif_clear writes a frame with a clear part. The clear pixels keep
// their place and the other pixels keep their color.
fn test_gif_clear() {
    var px: u8[>..] = [];
    for i in 0..(16 * 16) {
        px.push(200); px.push(100); px.push(50); px.push(255);
    }
    for i in 0..(16 * 16) {
        px.push(0); px.push(0); px.push(255); px.push(0);
    }
    let img = image::Image { width: 16, height: 32, pixels: px[..], icc: [], exif: [] };
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(copy(img), 0, 0));
    let g = image::Gif { frames: fs[..], width: 16, height: 32, loop_count: 0 };
    let back = image::decode_gif(image::encode_gif(g));
    assert(back.frames.len == 1);
    let fr = frame_at(back, 0);
    let got = fr.image.pixels;
    // The first pixel keeps its color, the first pixel of the clear part is
    // clear.
    assert(got[0] == 200 && got[1] == 100 && got[2] == 50 && got[3] == 255);
    assert(got[1024] == 0 && got[1027] == 0);
    // A frame with more colors than the table and a clear part. The clear
    // pixels keep their place and the other pixels stay near the source.
    var many: u8[>..] = [];
    for i in 0..(32 * 32) {
        many.push((i % 256) as u8);
        many.push((i / 4) as u8);
        many.push((i * 5 % 256) as u8);
        many.push(if i < 32 * 16 { 255 } else { 0 });
    }
    var fs2: image::GifFrame[>..] = [];
    fs2.push(frame(image::Image { width: 32, height: 32, pixels: many[..], icc: [], exif: [] }, 0, 0));
    let g2 = image::Gif { frames: fs2[..], width: 32, height: 32, loop_count: 0 };
    let back2 = image::decode_gif(image::encode_gif(g2));
    assert(back2.frames.len == 1);
    let fr2 = frame_at(back2, 0);
    let got2 = fr2.image.pixels;
    assert(got2[3] == 255);
    assert(got2[32 * 16 * 4 + 3] == 0);
    print("gif clear ok\n");
}

// test_gif_offsets writes frames where only a part of the canvas changes.
// The writer cuts the frame to the part that changed.
fn test_gif_offsets() {
    // The first frame holds one color. The second one holds a small square
    // of another color.
    var px: u8[>..] = [];
    for i in 0..(32 * 32) {
        px.push(10); px.push(20); px.push(30); px.push(255);
    }
    var sq: u8[>..] = [];
    for i in 0..(32 * 32) {
        let y = i / 32;
        let x = i % 32;
        if y >= 5 && y < 9 && x >= 6 && x < 10 {
            sq.push(200); sq.push(10); sq.push(10); sq.push(255);
        } else {
            sq.push(10); sq.push(20); sq.push(30); sq.push(255);
        }
    }
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    fs.push(frame(image::Image { width: 32, height: 32, pixels: sq[..], icc: [], exif: [] }, 0, 0));
    let g = image::Gif { frames: fs[..], width: 32, height: 32, loop_count: 0 };
    let bytes = image::encode_gif(g);
    let back = image::decode_gif(bytes);
    assert(back.frames.len == 2);
    // The square of the second frame shows up.
    let fr = frame_at(back, 1);
    let got = fr.image.pixels;
    let at = (6 * 32 + 7) * 4;
    assert(got[at] == 200 && got[at + 1] == 10 && got[at + 2] == 10 && got[at + 3] == 255);
    // The rest of the canvas keeps the color of the first frame.
    let out = (0 * 32 + 0) * 4;
    assert(got[out] == 10 && got[out + 1] == 20 && got[out + 2] == 30 && got[out + 3] == 255);
    // A frame that changes the whole canvas needs more room than a frame
    // that changes a small part of it.
    var all: u8[>..] = [];
    for i in 0..(32 * 32) {
        all.push(90); all.push(80); all.push(70); all.push(255);
    }
    var fs2: image::GifFrame[>..] = [];
    fs2.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    fs2.push(frame(image::Image { width: 32, height: 32, pixels: all[..], icc: [], exif: [] }, 0, 0));
    let g2 = image::Gif { frames: fs2[..], width: 32, height: 32, loop_count: 0 };
    assert(image::encode_gif(g2).len > bytes.len);
    // A frame that holds the same pixels as the frame before it keeps its
    // place in the file.
    var fs3: image::GifFrame[>..] = [];
    fs3.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    fs3.push(frame(image::Image { width: 32, height: 32, pixels: px[..], icc: [], exif: [] }, 0, 0));
    let g3 = image::Gif { frames: fs3[..], width: 32, height: 32, loop_count: 0 };
    let same = image::decode_gif(image::encode_gif(g3));
    assert(same.frames.len == 2);
    let sfr = frame_at(same, 1);
    assert(sfr.image.pixels[0] == 10 && sfr.image.pixels[3] == 255);
    print("gif offsets ok\n");
}

// test_gif_hostile feeds broken files. Nothing aborts and each one ends fast.
fn test_gif_hostile() {
    var fs: image::GifFrame[>..] = [];
    fs.push(frame(solid(6, 5, 1, 2, 3, 255), 4, 0));
    fs.push(frame(solid(6, 5, 4, 5, 6, 255), 4, 0));
    let hg = image::Gif { frames: fs[..], width: 6, height: 5, loop_count: 0 };
    let good = image::encode_gif(hg);
    assert(image::decode_gif(good).frames.len == 2);
    // Every cut is a fault or a shorter animation. Only the trailer may be missing.
    for n in 0..(good.len - 1) {
        let cut = image::decode_gif(good[..n]);
        assert(cut.frames.len < 2);
    }
    assert(image::decode_gif([]).frames.len == 0);
    // A bad LZW size byte: find the image descriptor and break the byte.
    var bad: u8[>..] = [];
    bad.append(good);
    var at = 0;
    while at < bad.len && bad[at] != 44 { at += 1; }
    // Descriptor is 10 bytes. A local table may follow.
    var k = at + 10;
    if (bad[at + 9] & 128) != 0 { k += 3 * (1 << ((bad[at + 9] & 7) + 1)); }
    bad[k] = 0;
    assert(image::decode_gif(bad[..]).frames.len == 0);
    bad[k] = 9;
    assert(image::decode_gif(bad[..]).frames.len == 0);
    // A screen of 65535 by 65535 in a file of 13 bytes.
    var huge: u8[>..] = [71, 73, 70, 56, 57, 97, 255, 255, 255, 255, 0, 0, 0, 59];
    assert(image::decode_gif(huge[..]).frames.len == 0);
    // A huge count of tiny frames gives a fault at the size cap or a long
    // list. Either way it ends fast.
    var many: u8[>..] = [71, 73, 70, 56, 57, 97, 200, 0, 200, 0, 128, 0, 0, 0, 0, 0, 255, 255, 255];
    for i in 0..3000 {
        many.append([44, 0, 0, 0, 0, 200, 0, 200, 0, 0, 2, 2, 68, 1, 0]);
    }
    many.push(59);
    let big = image::decode_gif(many[..]);
    assert(big.frames.len < 3000);
    print("gif hostile ok\n");
}

// test_hostile_size checks the calls that take a size or a place from the
// caller. Nothing aborts and nothing touches a wrong pixel.
