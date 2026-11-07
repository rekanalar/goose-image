// An example that reads the metadata of a JPEG and writes it back.
//
// The example makes a JPEG with the package, reads it, and prints the
// size of the ICC profile and of the EXIF block. A file made by
// another tool carries its profile and its tags through the round
// trip.

import std;
import os;
import image from "..";

fn main() {
    // A small image with metadata.
    var icc: u8[>..] = [];
    for i in 0..600 { icc.push((i % 251) as u8); }
    // The profile is any byte block. A real profile comes from the
    // file that the caller reads.
    var px: u8[>..] = [];
    for i in 0..8 * 8 * 4 { px.push(if i % 4 == 0 { 10 } else if i % 4 == 1 { 20 } else if i % 4 == 2 { 30 } else { 255 }); }
    let img = image::Image { width: 8, height: 8, pixels: px[..], icc: icc[..], exif: [] };
    let bytes = image::encode(img, image::FORMAT_JPEG, 90);
    assert(bytes.len > 0);

    let back = image::decode(bytes);
    print("icc bytes: ");
    print(back.icc.len);
    print("\nexif bytes: ");
    print(back.exif.len);
    print("\n");
    assert(back.icc[..] == img.icc[..]);
}
