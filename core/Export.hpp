#pragma once
// core/Export.hpp -- the book out, as running text.
//
// flow() turns pages of areas into the book's paragraphs, which is where the
// book-specific work lives:
//   - running heads and folios (Header / Footer areas) are left out
//   - a paragraph broken by the page turn is joined back (Settings::join_pages):
//     the page's last paragraph does not end a sentence and the next page's
//     first one starts in lower case
//   - a word hyphenated at a line end is mended (Settings::dehyphenate)
//
// The writers are then dumb: ODT (Writer's own format, the deliverable) and
// plain text. Styles are handles, not opinions (DESIGN): Heading 1 and the
// body style, so Writer can restyle the book without a hunt.

#include "Book.hpp"

#include <string>
#include <vector>

namespace optik::core {

// Character formats ride IN the text (s27 italic; s28 every format): kFmtOn
// and a mask byte ('@' + ocr::Fmt bits) open a run, kFmtOff closes it -- two
// control bytes no reading can contain. A run of words in one format is one
// run; a word dehyphenated across a line keeps one. write_odt turns a run into
// Writer character styles (Emphasis, Strong Emphasis, Underline, Small Caps,
// Superscript, nested); write_text drops them; plain() strips.
constexpr char kFmtOn  = '\x01';
constexpr char kFmtOff = '\x02';
std::string fmt_open(unsigned mask);     // kFmtOn + the mask byte
std::string plain(const std::string& marked);

struct Block {
    enum Kind { Paragraph, Heading } kind = Paragraph;
    std::string text;         // with emphasis marks (above)
    std::string page_id;      // where it starts
};

std::vector<Block> flow(const Book& book);

// Line join inside a paragraph, by the book's dehyphenation setting. Exposed for
// the selftest; flow() is its only other caller.
std::string join_lines(const std::vector<std::string>& lines, bool dehyphenate);

bool write_text(const std::vector<Block>& blocks, const std::string& path, std::string* why = nullptr);
bool write_odt(const std::vector<Block>& blocks, const std::string& title,
               const std::string& path, std::string* why = nullptr);

}  // namespace optik::core
