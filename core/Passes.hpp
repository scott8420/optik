#pragma once
// core/Passes.hpp -- the reading passes that run after the engine, each one a
// Book setting the author may decline (DESIGN: a checklist, not a pipeline).
//
// Pure functions on a page's areas, page space. Read on a worker (Reading.cpp)
// in this order: running heads (Book.hpp), headings, quotes, small caps.
//
// The common rules:
//   - an area the author set (Area::user) is never re-marked
//   - a word the author checked or edited is never touched
//   - a pass never makes a word MORE certain: confidence is the engine's, and a
//     word wanting a look keeps wanting one until the author checks it. A pass
//     may add doubt (Word::doubt), never take it away.

#include "ocr/Layout.hpp"

#include <functional>
#include <string>
#include <vector>

namespace optik::core {

// Headings (s25). A short, centred line -- or a run of up to four of them --
// with at least a blank line's space below it is a heading: "CHAPTER II" /
// "AN IVORY CHARM". Centred and short are measured against the page's body
// column (the lines of its multi-line text areas), not the scan's edges, since
// scans carry uneven margins. Lines with no letter or digit ("* * *") are scene
// breaks, not headings. A heading run inside a text area is split off into its
// own Heading area; an area that is all heading becomes one.
void mark_headings(std::vector<ocr::Area>& areas);

// Hot-lead quotes (s25). Letterpress quotes are often read as two marks --
// ``Stop, ''Stop, ""Stop, Stop'' -- or as a mark standing alone as a word.
// Position decides what a mark is: at a word's start it opens, at its end it
// closes, between letters it is an apostrophe. A run of marks becomes ONE
// typographic quote (single or double by its weight: ` ' = 1, " = 2), a lone
// mark is joined to its word, and anything the pass is not sure of (a run too
// heavy to be one quote, marks split by other punctuation) is doubted.
void mend_quotes(std::vector<ocr::Area>& areas);

// One word's quotes, as mend_quotes sees them. Exposed for the selftest.
std::string mend_quote_word(const std::string& text, bool* doubt = nullptr);

// Small caps (s25). The engine reads small capitals as a muddle of cases --
// "AN IVORY CHARM" as "Aw Ivory CHarM", the opener "THE" as "Tue", confidently.
// - A heading line that reads mostly in capitals, or has a capital inside a
//   word ("CHarM"), or a word of capitals that is not a roman numeral ("AN"),
//   is in caps: its other words are upper-cased AND doubted (the
//   case is mended; the letters may still be wrong -- "Aw").
// - A chapter's opener -- the first word of the first text line under a
//   heading, and the words after it still in capitals -- is doubted, however
//   confident the engine was: that is where a confident misread hides.
void mark_small_caps(std::vector<ocr::Area>& areas);

// Italic (s27). The engine measures it (ocr/Slant.hpp) on every read; this is
// the book's say over what that means. On (Settings::mark_italic): an italic
// word is doubted -- the measure is a lean, and a lean can be a swash capital or
// a smudge, so each one wants a look until checked; export sets it as emphasis.
// Off: the measure is dropped (no word italic) and nothing is doubted for it.
// A word the author checked or edited is not touched either way.
void mark_italic(std::vector<ocr::Para>& paras, bool on);
void mark_italic(std::vector<ocr::Area>& areas, bool on);

// Dropcaps (s32, core/Dropcaps.cpp). A chapter opener's large initial -- at the
// left edge, two to four lines deep -- comes back from the engine as its own
// area (a letter, or a picture), as a tall first word, or not at all (the lines
// beside it indented, the paragraph often cut in two). Found by its shape, read
// (`read`: one rectangle as one word, the engine's read_word), and spliced back
// into the first word ("T" + "HE" -> "THE"), which is doubted; the paragraph
// keeps the cap's letters and depth (Para::cap_letters / cap_lines) for export,
// and a paragraph the engine cut beside the cap is one again. Only one to three
// letters is a cap: a gap that reads as anything else is left alone. Runs first
// after running heads, so headings and small caps see the whole first word.
// Returns how many it found.
using DropcapReader = std::function<bool(const ocr::Rect& where, ocr::Word& out)>;
int mark_dropcaps(std::vector<ocr::Area>& areas, const DropcapReader& read);

}  // namespace optik::core
