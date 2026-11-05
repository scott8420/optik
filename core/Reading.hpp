#pragma once
// core/Reading.hpp -- reading a page, in three parts so a worker can do the slow
// one without touching the Book:
//
//   request_for(book, page)   UI thread: copy out what the engine needs
//   read(engine, request)     ANY thread: the engine, then the reading passes
//   land(edit, reading)       UI thread, inside a journal step: write it in
//
// The Book is the UI thread's; a worker holds only a request (plain values) and
// returns only a reading (plain values). Nothing is shared, so nothing locks.

#include "Book.hpp"
#include "Undo.hpp"
#include "ocr/Ocr.hpp"

#include <string>
#include <vector>

namespace optik::core {

struct ReadRequest {
    std::string page_id;
    std::string scan_path;
    Rect        crop;
    Settings    settings;
    // Set for an area re-read: which area, and the rectangle to read it from.
    int         area = -1;
    Rect        area_box;
};

struct Reading {
    std::string page_id;
    bool        ok = false;
    std::string why;
    std::vector<ocr::Area> areas;   // whole page
    int         area = -1;          // area re-read: its index...
    Rect        area_box;
    std::vector<ocr::Para> paras;   // ...and what it now says
};

ReadRequest request_for(const Book& book, const Page& page);
ReadRequest request_for_area(const Book& book, const Page& page, int area, const Rect& box);

Reading read(ocr::Engine& engine, const ReadRequest& r);

// Writes the reading into its page. A page that has gone (undone away while the
// worker ran) is skipped -- returns false so the caller can say so.
bool land(Edit& e, const Reading& r);

}  // namespace optik::core
