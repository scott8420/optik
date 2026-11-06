#pragma once
// core/Find.hpp -- search and replace across the book (s27, milestone 15).
//
// FineReader's Find and Replace, on the book's words: the misreads an engine
// makes are made the same way everywhere ("tbe", "rn" for "m", "Nancv"), so the
// author fixes one and replaces the rest. A match lies inside one word (a word
// never holds a space); every area with words is searched, running heads too,
// in book order: page, area, paragraph, line, word.
//
// A replace is the author's typing, done for them: the word keeps the engine's
// reading in Word::original and is edited (so it no longer wants a look), and
// Replace All is ONE journal step, however many pages it touches.

#include "Book.hpp"
#include "Undo.hpp"

#include <string>
#include <vector>

namespace optik::core {

struct FindOptions {
    bool match_case  = false;   // off: A-Z and a-z are the same (ASCII)
    bool whole_words = false;   // on: not inside a longer run of letters / digits
};

struct Hit {
    std::string page_id;
    int area = -1, para = -1, line = -1, word = -1;
    bool operator==(const Hit&) const = default;
};

// Where `q` occurs in `text` (byte offsets), by the options. Empty q: nowhere.
std::vector<std::size_t> find_in(const std::string& text, const std::string& q, const FindOptions& o);

// Every word of the book holding `q`, in book order.
std::vector<Hit> find_all(const Book& book, const std::string& q, const FindOptions& o);

// `text` with every occurrence of `q` replaced by `with`; *n = how many.
std::string replace_in(const std::string& text, const std::string& q, const std::string& with,
                       const FindOptions& o, int* n = nullptr);

// Replaces in the words `hits` names (a hit gone stale is skipped), as the
// author's edits. Returns the occurrences replaced. Call inside Journal::run.
int replace_hits(Edit& e, const std::vector<Hit>& hits, const std::string& q,
                 const std::string& with, const FindOptions& o);

}  // namespace optik::core
