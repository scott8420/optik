#pragma once
// ocr/Slant.hpp -- italic, measured from the pixels (s27).
//
// Tesseract's LSTM engine does not say which words are italic (its font
// attributes are the legacy engine's, and empty under LSTM). So Optik measures
// it: an italic word's upright strokes lean. Shear the word's ink by s
// (x' = x + s * (y - yc)) and the column profile is sharpest -- the strokes
// stand in the fewest columns -- when s cancels the lean. The best s IS the
// slant (tan of the angle, positive = leaning right). Roman text measures near
// 0; book italics 0.17 - 0.30 (10 - 17 degrees).
//
// ENGINE-FREE, like Layout.hpp: a grey picture as plain bytes in, numbers and
// flags out. The engine calls it on the pixels it read (Ocr.cpp); the selftest
// calls it on pictures it draws, where the answer is certain.

#include "Layout.hpp"

#include <vector>

namespace optik::ocr {

// An 8-bit grey picture, 0 = black, row after row. Page space.
struct Grey {
    const unsigned char* px = nullptr;
    int w = 0, h = 0, stride = 0;
};

// The slant of the ink inside `box`, or NaN when there is too little to tell (a
// one-letter word, a mark, a box with no contrast). Ink is decided inside the
// box (midway between its darkest and lightest), so uneven lighting across the
// page does not matter.
float slant_of(const Grey& g, const Rect& box);

// How far past the page's own lean a word must lean to be italic. Measured on
// rendered Times (tests/data/italic.png): roman -0.08 .. 0.06, italic 0.15 ..
// 0.19 -- the bar sits in the gap. A short word between two italic ones leans
// less than the long ones do ("is" 0.10 inside an italic line), so between
// italic neighbours the bar drops to kItalicLeanNear.
constexpr float kItalicLean     = 0.10f;
constexpr float kItalicLeanNear = 0.05f;

// Set Word::italic on every word of these paras, measured on `g`:
//   - the page's lean is the median slant of its measurable words, held to
//     +-0.08 (a skewed scan leans every word a little; a page that is all italic
//     must not cancel itself out)
//   - a word leaning kItalicLean past that is italic
//   - a word too small to measure ("a", "I", "--"), or leaning only
//     kItalicLeanNear past it, is italic when the nearest measured words either
//     side of it on the line are both italic
// A word a person has checked or edited keeps its italic as it is.
void measure_italic(const Grey& g, std::vector<Para>& paras);
void measure_italic(const Grey& g, std::vector<Area>& areas);

}  // namespace optik::ocr
