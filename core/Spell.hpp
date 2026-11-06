#pragma once
// core/Spell.hpp -- the spelling pass (s27): a word neither the dictionary nor
// the book's own words know is marked to check (Word::unknown).
//
// The dictionary is Hunspell's -- the one LibreOffice uses, from the distro's
// hunspell-<lang> package. A book written in a language with no dictionary
// installed simply has no spelling pass; nothing fails.
//
// The pass itself is pure (mark_spelling takes "is this known?" as a function),
// so the selftest decides what is known and the outcome is certain on any
// machine. Speller is the Hunspell seam; one per worker thread (Hunspell is not
// shareable), made on first use by Reading.cpp.
//
// The book's own words (Meta::words) are the author's: "Add to Book's Words" on
// a word adds its parts, clears the mark on every word in the book made only of
// book words, and later reads do not mark them. One journal step; undoable.

#include "Book.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace optik::core {

// The installed dictionary for a Tesseract language ("eng" -> ".../en_US"), as
// the path without .dic / .aff; empty if none. "eng+fra" looks for the first.
std::string dictionary_for(const std::string& language);

class Speller {
public:
    Speller();
    ~Speller();
    Speller(const Speller&) = delete;
    Speller& operator=(const Speller&) = delete;

    bool open(const std::string& language, std::string* why = nullptr);
    bool ready() const;
    const std::string& language() const;
    bool known(const std::string& part);     // one part, as spell_parts gives it

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// What of a word gets checked: its letters, without the quotes and punctuation
// around it, split at hyphens and dashes ("well-known" -> well, known), with a
// typographic apostrophe made plain. Nothing for a number, a single letter, a
// roman numeral in capitals, or a word with no letters.
std::vector<std::string> spell_parts(const std::string& word);

// Book words, matched without regard to (ASCII) case.
bool is_book_word(const std::vector<std::string>& words, const std::string& part);
// Adds, keeping the list sorted and one each. False if it was there already.
bool add_book_word(std::vector<std::string>& words, const std::string& part);

// Sets Word::unknown on the text areas' words (headings included; running
// heads, pictures and ignored areas are not text). A word the author checked or
// edited is left as it is. A word hyphenated at a line end is checked joined to
// the next line's first word ("exam-" / "ple"): both are fine if the join is,
// or if each half is ("well-" / "known").
using KnownFn = std::function<bool(const std::string&)>;
void mark_spelling(std::vector<ocr::Area>& areas, const KnownFn& known,
                   const std::vector<std::string>& book_words);
void mark_spelling(std::vector<ocr::Para>& paras, const KnownFn& known,
                   const std::vector<std::string>& book_words);

// After "Add to Book's Words": clears Word::unknown on every word of these areas
// whose parts are all book words. Returns how many it cleared.
int clear_book_words(std::vector<ocr::Area>& areas, const std::vector<std::string>& words);

}  // namespace optik::core
