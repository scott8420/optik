#pragma once
// ocr/Condition.hpp -- phase 0 (s29): the page as the engine and the screen see
// it. Cut from the scan, turned, then the book's conditioning passes, each one
// the book may decline:
//
//   decolour   the page in grey (yellowed paper, foxing, a colour cast gone)
//   level      the paper evened to white and the ink to black (a gutter's
//              shadow, a pale scan); Leptonica's background normalisation,
//              then a stretch
//   deskew     the page turned by its measured skew so the lines run level
//
// The skew is MEASURED when a page is read and kept on the page (core::Page::
// skew): every box the engine reports is in the straightened page, so the
// picture must be straightened by the same angle -- not re-measured -- or the
// boxes would slide off their words. Decolour and level move no pixel, so they
// are the book's settings and apply to the next picture and the next read.
//
// The engine conditions the full-resolution page; the screen conditions after
// scaling (fast; the rotation is about the centre, so the geometry is the same).
//
// Leptonica types appear only as a forward declaration; Condition.cpp holds the
// calls. Engine-free: no Tesseract here.

#include "Layout.hpp"

#include <string>

struct Pix;

namespace optik::ocr {

struct Look {
    Rect   crop;              // on the scan (empty = the whole scan)
    int    turns    = 0;      // quarter turns clockwise after the cut, 0..3
    double skew     = 0.0;    // degrees: the straightening applied (0 = none)
    bool   decolour = false;
    bool   level    = false;
    bool operator==(const Look&) const = default;
};

// The largest skew the measure looks for, and the smallest it acts on
// (Leptonica's own floor: under a tenth of a degree a turn only blurs the type).
inline constexpr double kMaxSkew = 7.0;
inline constexpr double kMinSkew = 0.1;

// Cut and turn (no conditioning). Null with a reason on failure. The caller
// owns the result (pixDestroy).
Pix* cut_page(const std::string& path, const Rect& crop, int turns, std::string* why = nullptr);

// Decolour and level as the look says (no turn). Returns a new Pix, or a clone
// when there is nothing to do. Never null for a valid input: a pass that fails
// (a page too small to level, say) is skipped.
Pix* tone(Pix* page, const Look& look);

// Straighten by `degrees` about the centre, the same size, white brought in.
// A clone when |degrees| < kMinSkew.
Pix* straighten(Pix* page, double degrees);

// The page's skew in degrees -- the angle straighten() takes to level its
// lines -- rounded to a hundredth; 0 when there is too little text to be sure
// or the lean is under kMinSkew.
double find_skew(Pix* page);

// All of it: cut, turn, tone, straighten by look.skew.
Pix* load_page(const std::string& path, const Look& look, std::string* why = nullptr);

}  // namespace optik::ocr
