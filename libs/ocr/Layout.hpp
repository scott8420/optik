#pragma once
// ocr/Layout.hpp -- what a page SAYS, as plain values.
//
// No Tesseract here. These are the shapes the engine's answer is copied into,
// and the shapes the book stores, edits, undoes and exports. Keeping them free
// of the engine's headers means core/ and the app can hold a page's text without
// ever seeing tesseract/baseapi.h, and a second engine (or a test) can fill them.
//
// Coordinates are PAGE space: pixels relative to the page's crop origin on its
// scan, x right, y down, [x0, x1) x [y0, y1). A page is a rectangle on a scan
// (a spread gives two), so page space is what every consumer actually wants.
//
// The hierarchy is FineReader's and Tesseract's alike:
//   Area (a block: text, heading, picture, running head, folio)
//     Para
//       Line
//         Word  (text, box, confidence, and whether a person has checked it)

#include <string>
#include <vector>

namespace optik::ocr {

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    int  width()  const { return x1 - x0; }
    int  height() const { return y1 - y0; }
    bool empty()  const { return x1 <= x0 || y1 <= y0; }
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    bool operator==(const Rect&) const = default;
};

// The smallest rect holding both. An empty side is ignored.
Rect unite(const Rect& a, const Rect& b);

struct Word {
    std::string text;
    Rect        box;
    float       conf = 0.0f;       // the engine's confidence, 0..100
    bool        checked = false;   // a person looked at it and said yes
    bool        edited  = false;   // a person changed the text
    bool        doubt   = false;   // a reading pass doubts it, however confident
                                   // the engine was (a small-caps opener): wants
                                   // a look until checked
    std::string original;          // the engine's reading, kept once edited
    bool operator==(const Word&) const = default;
};

struct Line {
    Rect              box;
    std::vector<Word> words;
    bool operator==(const Line&) const = default;
};

struct Para {
    Rect              box;
    std::vector<Line> lines;
    bool operator==(const Para&) const = default;
};

// What an area IS to the book. Header and Footer are the running heads and
// folios: real ink, read and kept, but not the book's text -- export leaves
// them out unless asked. Ignore is the author's "this is not text at all".
enum class Kind { Text, Heading, Picture, Header, Footer, Ignore };

const char* kind_name(Kind k);                       // "text", "heading", ...
bool        kind_from(const std::string& s, Kind& out);
bool        kind_is_text(Kind k);                    // carries words for export

struct Area {
    Kind              kind = Kind::Text;
    Rect              box;
    std::vector<Para> paras;
    bool              user = false;   // drawn or retyped by a person
    bool operator==(const Area&) const = default;
};

// The plain text of an area / a para, words joined by single spaces and lines
// by '\n'. What the text pane and the exporters build on.
std::string para_text(const Para& p);

}  // namespace optik::ocr
