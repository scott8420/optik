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
    enum Kind { Paragraph, Heading, RunningHead } kind = Paragraph;
    std::string text;         // with emphasis marks (above)
    std::string page_id;      // where it starts
    bool break_before = false;   // s29: a page break before it (ExportOptions::breaks)
};

// The export's own choices (s29, milestone 18): asked when exporting, kept by
// the window for the session -- not the book's settings, not a journal step.
struct ExportOptions {
    int  first = 1;               // pages, 1-based in book order, inclusive
    int  last  = 0;               // 0 = to the book's last page
    bool keep_heads = false;      // running heads and folios kept, each a Running
                                  // Head paragraph where it stands -- a paragraph
                                  // joined over the page turn still joins (the
                                  // heads between follow it)
    enum Breaks { NoBreaks, AtHeadings, AtPages } breaks = NoBreaks;
                                  // AtHeadings: before every heading but the first
                                  // block; AtPages: at every scanned page -- the
                                  // pages are kept as pages, so no page-turn join
    bool operator==(const ExportOptions&) const = default;
};

std::vector<Block> flow(const Book& book);
std::vector<Block> flow(const Book& book, const ExportOptions& opts);

// The pages an export takes (indices into book.pages(), clamped); empty if none.
struct PageSpan { int from = 0, to = -1; bool empty() const { return to < from; } };
PageSpan span_of(const Book& book, const ExportOptions& opts);

// Line join inside a paragraph, by the book's dehyphenation setting. Exposed for
// the selftest; flow() is its only other caller.
std::string join_lines(const std::vector<std::string>& lines, bool dehyphenate);

bool write_text(const std::vector<Block>& blocks, const std::string& path, std::string* why = nullptr);
bool write_odt(const std::vector<Block>& blocks, const std::string& title,
               const std::string& path, std::string* why = nullptr);

}  // namespace optik::core
