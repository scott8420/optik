#pragma once
// core/Audit.hpp -- what must ALWAYS be true of a Book, said once, checked
// anywhere: by the selftest, and by the app's sweep (app/Sweep.cpp) after every
// verb and every poke at the canvas.
//
// audit() names each broken rule in words; an empty list is a sound book. It
// checks STRUCTURE (ids, references, rectangles), never a reading -- the
// sweep runs on any machine's Tesseract.
//
// same_book() compares two books the way a save and an open must agree: equal
// in everything, with a word's confidence allowed the 0.1 that book.json
// rounds it to.

#include "Book.hpp"

#include <string>
#include <vector>

namespace optik::core {

// The page's size in page space: its crop, turned (a quarter turn swaps the sides).
struct Size2i { int w = 0, h = 0; };
Size2i page_size(const Page& p);

std::vector<std::string> audit(const Book& book);

bool same_book(const Book& a, const Book& b, std::string* why = nullptr);

}  // namespace optik::core
