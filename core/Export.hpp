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
// The writer is then dumb: ODT (Writer's own format, and Composix's way in). Styles are handles, not opinions (DESIGN): Heading 1 and the
// body style, so Writer can restyle the book without a hunt.

#include "Book.hpp"
#include "ocr/Condition.hpp"   // ocr::Look (a forward-declared Pix, no Leptonica)

#include <functional>
#include <string>
#include <vector>

namespace optik::core {

// Character formats ride IN the text (s27 italic; s28 every format): kFmtOn
// and a mask byte ('@' + ocr::Fmt bits) open a run, kFmtOff closes it -- two
// control bytes no reading can contain. A run of words in one format is one
// run; a word dehyphenated across a line keeps one. write_odt turns a run into
// Writer character styles (Emphasis, Strong Emphasis, Underline, Small Caps,
// Superscript); plain() strips. (s31: ODT is the only export -- Writer and
// Composix both read it; plain text went.)
constexpr char kFmtOn  = '\x01';
constexpr char kFmtOff = '\x02';
std::string fmt_open(unsigned mask);     // kFmtOn + the mask byte
std::string plain(const std::string& marked);

struct Block {
    enum Kind { Paragraph, Heading, RunningHead, Picture } kind = Paragraph;
    std::string text;         // with emphasis marks (above); a Picture's is empty
    std::string page_id;      // where it starts
    bool break_before = false;   // s29: a page break before it (ExportOptions::breaks)
    ocr::Rect rect;           // s31: a Picture's box on its page (the page as read:
                              // cropped, turned, straightened)
    int cap_letters = 0;      // s32: a Paragraph opening with a dropcap, its letters
    int cap_lines   = 0;      // and depth -- set only when the book rebuilds dropcaps
                              // (Settings::rebuild_dropcaps); 0 = a plain paragraph

    Block() = default;
    Block(Kind k, std::string t, std::string page, bool brk = false)
        : kind(k), text(std::move(t)), page_id(std::move(page)), break_before(brk) {}
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
    bool pictures = true;         // s31: picture areas exported as pictures, where
                                  // they stand (a paragraph still joins over one)
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


// -- what the ODT carries beyond the text (s31, milestones 22-24) -------------------
//
// The ODT is the book's way into Writer AND into Composix (whose File > Import
// ODT lays it out: pages, a master, chapters on new pages -- its own form is a
// laid-out document, and layout is Composix's, not Optik's). So the ODT says
// what Composix's importer reads: the book's page (trim size and margins), its
// pictures, a chapter's label and title as two paragraphs.

// The book's page in points: its trim (the scanned page at the scan's
// resolution) and its margins (to the text's bounds), inner / outer as a
// bound book's are. Invalid (w == 0) when the scans don't say their resolution.
struct PageSize {
    double w = 0, h = 0;
    double top = 0, bottom = 0, inner = 0, outer = 0;
    bool valid() const { return w > 0 && h > 0; }
    bool operator==(const PageSize&) const = default;
};

// A picture's file: PNG bytes and its size in points.
struct Art {
    std::string png;
    double w = 0, h = 0;
    bool operator==(const Art&) const = default;
};

struct OdtBook {
    std::string title;
    PageSize page;               // invalid: Writer's own default page
    std::vector<Art> art;        // one for each Picture block, in order; a missing
                                 // one (or empty png) leaves that picture out
};

// Where the art and the page size come from: captured on the UI thread (cheap,
// by value -- the work never touches the Book), realised on a worker.
struct ArtSource  { std::string path; ocr::Look look; ocr::Rect rect; };
struct PageSource { std::string path; ocr::Rect crop; int turns = 0; ocr::Rect text; bool verso = false; };
struct ExportPlan {
    std::string title;
    std::vector<ArtSource>  art;     // one for each Picture block
    std::vector<PageSource> pages;   // the body pages measured for the trim (a few)
};
ExportPlan plan_of(const Book& book, const std::vector<Block>& blocks);

// Decode the scans (Leptonica) and make the pictures and the page. Slow: a
// worker's. `cancelled` is polled between pictures.
OdtBook realise(const ExportPlan& plan, const std::function<bool()>& cancelled = {});

// The trim from measured pages: `ppi` the scans' resolution, each page's size
// in pixels and its text's bounds. Pure, for the selftest; realise() calls it.
struct Measured { int w = 0, h = 0; ocr::Rect text; bool verso = false; };
PageSize page_size(const std::vector<Measured>& pages, double ppi);

// A chapter's label line ("CHAPTER II", "Book Three", "XIV", "12.") -- the
// first line of a two-line heading set apart as Heading 1 over a Subtitle.
bool is_chapter_label(const std::string& line);

bool write_odt(const std::vector<Block>& blocks, const OdtBook& book,
               const std::string& path, std::string* why = nullptr);
bool write_odt(const std::vector<Block>& blocks, const std::string& title,
               const std::string& path, std::string* why = nullptr);

}  // namespace optik::core
