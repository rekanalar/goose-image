// An example for the image package.
//
// It crops, turns, rotates, pastes and blends a small image. It prints
// the size and one pixel of each result.
//
// Run with: goose run examples/geometry.goose

import std;
import image from "..";

// show prints the size of the image and the red part of its first pixel.
fn show(name: u8[:], img: image::Image&) {
    let r, g, b, a = image::pixel(img, 0, 0);
    print(name, ": ", img.width, "x", img.height, " first red=", r);
}

fn main() {
    // A 4x3 image. The red part is the index of the pixel, 0 to 11.
    var img = image::new_image(4, 3, 0, 0, 0, 255);
    for y: i32 in 0..3 {
        for x: i32 in 0..4 {
            image::set_pixel(img, x, y, (y * 4 + x) as u8, 0, 0, 255);
        }
    }

    // A crop of the middle 2x2 pixels.
    var part = image::crop(img, 1, 1, 3, 3);
    show("crop", part);

    // A flip and a turn. The turn is counter-clockwise.
    var flipped = image::transpose(img, image::TRANSPOSE_FLIP_LEFT_RIGHT);
    show("flip", flipped);
    var turned = image::transpose(img, image::TRANSPOSE_ROTATE_90);
    show("turn", turned);

    // A rotation by 45 degrees. The expand flag makes room for the corners.
    var tilted = image::rotate(img, 45.0, true);
    show("rotate", tilted);

    // A copy, and a blend of a half transparent pixel.
    var canvas = image::new_image(4, 3, 10, 20, 30, 255);
    var dot = image::new_image(1, 1, 200, 100, 50, 128);
    image::paste(canvas, part, 2, 1);
    show("paste", canvas);
    image::alpha_composite(canvas, dot, 0, 0);
    let r, g, b, a = image::pixel(canvas, 0, 0);
    print("blend: ", r, " ", g, " ", b, " ", a);
    return 0;
}
