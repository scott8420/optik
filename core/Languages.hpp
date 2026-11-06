#pragma once
// core/Languages.hpp -- the languages a book can be read in (s30, milestone 19).
//
// Tesseract reads in whatever languages have a <code>.traineddata installed;
// the book's language setting is those codes joined with '+' ("eng+fra"), the
// first being the book's own (its spelling dictionary follows the first --
// core/Spell). The Book popover lists what is installed so the author picks,
// not types; a code the book names that is not installed here (a book from
// another machine) is listed too, marked, so it can be taken out.
//
// GTK-free and engine-free: the installed list is a directory scan, so it costs
// nothing and needs no engine; the selftest scans a folder it makes.

#include <string>
#include <vector>

namespace optik::core {

struct Language {
    std::string code;            // Tesseract's: eng, fra, chi_sim, ...
    std::string name;            // English, French, Chinese (Simplified) -- or the code
    bool        installed = false;
    std::string dictionary;      // core::dictionary_for(code): the Hunspell base, or empty
};

// Where Tesseract keeps its languages: TESSDATA_PREFIX (and its tessdata/),
// then the distros' places (Fedora /usr/share/tesseract/tessdata, Ubuntu
// /usr/share/tesseract-ocr/5/tessdata, ...). Only folders that exist.
std::vector<std::string> tessdata_dirs();

// The codes with a .traineddata in any of `dirs`, one each, sorted by name;
// not osd (orientation) or equ (equations), which are not languages to read.
std::vector<std::string> installed_codes(const std::vector<std::string>& dirs);
std::vector<std::string> installed_codes();      // tessdata_dirs()

// A person's name for a code ("deu" -> "German"); the code itself if unknown.
std::string language_name(const std::string& code);

// The setting as codes ("eng+fra" -> eng, fra), empty parts dropped; and back.
std::vector<std::string> split_languages(const std::string& setting);
std::string join_languages(const std::vector<std::string>& codes);

// The setting with `code` added (at the end: the first stays the book's) or
// taken out. Never empty: taking out the last returns the setting as it was.
std::string with_language(const std::string& setting, const std::string& code, bool on);

// What the popover lists: the installed codes, then any the setting names that
// are not installed -- every one with its name and dictionary. Sorted by name
// within each group.
std::vector<Language> languages_for(const std::string& setting, const std::vector<std::string>& installed);

}  // namespace optik::core
