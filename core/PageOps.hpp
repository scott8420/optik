#pragma once
// core/PageOps.hpp -- what the author does to the page LIST (s26, milestone 12):
// move a page, delete it, turn it, re-cut the scan it came from.
//
// Each is a function on an Edit, so it runs inside one journal step (the one
// door) and is undone whole. Pure on the book: no files, no engine. A verb
// that changes what a page IS (its crop or its turn) clears its reading -- the
// words were read from other pixels -- and the caller reads it again; the
// clearing and the old reading are both in the step, so undo brings the old
// reading back.
//
// The scans are masters: deleting every page of a scan leaves the scan in the
// book (and on disk), so undo and a later re-cut still have it.

#include "Undo.hpp"

#include <string>
#include <vector>

namespace optik::core {

// One place up (-1) or down (+1), or any distance. False at an end / no page.
bool move_page_by(Edit& e, const std::string& page_id, int delta);

// Gone from the book; its scan stays. False if there is no such page.
bool delete_page(Edit& e, const std::string& page_id);

// A quarter turn clockwise (+1) or anticlockwise (-1), or a half (2). Clears
// the reading. False if no page or no turn (a multiple of 4).
bool rotate_page(Edit& e, const std::string& page_id, int quarter_turns);

// The scan's pages, in book order (a deleted half is simply absent).
std::vector<std::string> pages_of_scan(const Book& b, const std::string& scan_id);

// Re-cut a scan: `full` is the whole scan (its pixel size), `cut` the gutter's
// x on it, or 0 for one page. Two pages tile the scan exactly -- left [0, cut),
// right [cut, width) -- as split:: makes them. Ids survive where a page does:
// the scan's first page becomes the left (or the whole) page, its second the
// right; a page that is not needed any more is deleted, a missing one minted.
// Pages whose crop changed lose their reading and are returned (to read again);
// the turn is kept. Empty when nothing changed (no such scan in the book, a
// cut outside the scan, or the cut it already has).
std::vector<std::string> recut_scan(Edit& e, const std::string& scan_id, const Rect& full, int cut);

// What a page LOOKS like, as a key: id, crop and turn. A picture made for one
// key is stale for another (a re-cut, a turn, or the undo of one) -- the app's
// picture and thumbnail caches are keyed by it.
std::string look_key(const Page& p);

}  // namespace optik::core
