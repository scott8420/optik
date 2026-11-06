#pragma once
// core/Format.hpp -- the format bar's verbs (s28): bold, italic, underline,
// small caps, superscript on the words the author names (the selection, or the
// current word). FineReader's text editor, at word grain: a word carries its
// format (ocr::Fmt bits), export carries it to Writer as character styles.
//
// A toggle is the word processor's: if every word named has the format, it is
// taken off; otherwise it is put on all of them. One journal step. A format is
// the author's choice, not a check: a word wanting a look keeps wanting one.

#include "Find.hpp"

#include <vector>

namespace optik::core {

// Does every word named (the stale ones aside) carry `bit`? False for none.
bool all_have(const Book& book, const std::vector<Hit>& words, unsigned bit);

// Sets or clears `bit` on the words named; returns how many changed. Inside
// Journal::run.
int set_format(Edit& e, const std::vector<Hit>& words, unsigned bit, bool on);

// The word a hit names, or nullptr when it has gone stale.
const ocr::Word* word_of(const Book& book, const Hit& h);

}  // namespace optik::core
