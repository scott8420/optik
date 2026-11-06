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
    int         turns = 0;      // s26
    Settings    settings;
    std::vector<std::string> book_words;   // s27: the spelling pass's own words
    // Set for an area re-read: which area, and the rectangle to read it from.
    int         area = -1;
    Rect        area_box;
    Rect        area_was;   // the area's box when asked: its identity at landing (s26)
};

struct Reading {
    std::string page_id;
    Rect        crop;               // the page's look when asked (s26): a page
    int         turns = 0;          // re-cut or turned since gets nothing
    bool        ok = false;
    std::string why;
    std::vector<ocr::Area> areas;   // whole page
    int         area = -1;          // area re-read: its index...
    Rect        area_box;
    Rect        area_was;           // ...the box it had when asked (who it is)...
    std::vector<ocr::Para> paras;   // ...and what it now says
};

ReadRequest request_for(const Book& book, const Page& page);
ReadRequest request_for_area(const Book& book, const Page& page, int area, const Rect& box);

Reading read(ocr::Engine& engine, const ReadRequest& r);

// Writes the reading into its page. A page that has gone (undone away while the
// worker ran) is skipped -- returns false so the caller can say so. An area
// re-read lands in the area that still has the box it had when asked (by index
// if that one does, else the one that does); if none does -- deleted, moved or
// undone while the worker read it -- the reading is dropped (s26: the sweep
// caught a read landing in the area that took a deleted one's index). A page
// re-cut or turned since the read was asked is skipped too: its words were
// read from other pixels.
bool land(Edit& e, const Reading& r);

}  // namespace optik::core
