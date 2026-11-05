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

struct Block {
    enum Kind { Paragraph, Heading } kind = Paragraph;
    std::string text;
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
