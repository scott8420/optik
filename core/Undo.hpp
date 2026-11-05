#pragma once
// core/Undo.hpp -- the journal. Every user change to a Book is a step.
//
// CANON "Undo is foundation, not a feature": the journal ships with the model,
// every user choice is undoable, and there is ONE door.
//
// The door is Journal::run(book, label, fn). `fn` gets an Edit, the only thing
// that can write to a Book. The Edit takes the BEFORE picture of a page the
// first time the change touches it; run() takes the AFTER pictures when `fn`
// returns. A step is those pictures, by id and value -- not a per-verb inverse --
// so a verb added later is undoable the day it goes through the door.
//
// What a step holds: the touched pages (before / after, absent = did not exist),
// the page ORDER (a list of ids, cheap), the scans and the meta (small). A
// 300-page book's step is the pages it touched, not the book.

#include "Book.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace optik::core {

class Edit {
public:
    Meta& meta();
    Page& page(const std::string& id);                 // must exist
    Page& insert_page(std::size_t at, Page p);         // mints an id if empty
    void  remove_page(const std::string& id);
    void  move_page(const std::string& id, std::size_t to);
    const Scan& add_scan(Scan s);                      // mints an id if empty
    const Book& book() const { return m_book; }

private:
    friend class Journal;
    explicit Edit(Book& b) : m_book(b) {}
    void touch(const std::string& id);

    Book& m_book;
    std::map<std::string, std::optional<Page>> m_before;   // first-touch pictures
};

class Journal {
public:
    // The door. Returns false (and records nothing) if `fn` changed nothing.
    bool run(Book& book, const std::string& label, const std::function<void(Edit&)>& fn);

    bool undo(Book& book);
    bool redo(Book& book);
    bool can_undo() const { return m_pos > 0; }
    bool can_redo() const { return m_pos < m_steps.size(); }

    std::size_t size()     const { return m_steps.size(); }
    std::size_t position() const { return m_pos; }      // steps applied
    const std::string& label(std::size_t i) const { return m_steps[i].label; }
    std::string undo_label() const { return can_undo() ? m_steps[m_pos - 1].label : ""; }
    std::string redo_label() const { return can_redo() ? m_steps[m_pos].label : ""; }

    // "Unsaved": the position at the last save differs from now.
    void mark()        { m_mark = m_pos; m_mark_valid = true; }
    bool dirty() const { return !m_mark_valid || m_mark != m_pos; }
    void clear()       { m_steps.clear(); m_pos = 0; m_mark = 0; m_mark_valid = true; }

    // The pages the last undo / redo / run touched -- what a view must redraw.
    const std::vector<std::string>& last_touched() const { return m_touched; }

private:
    struct Step {
        std::string label;
        std::map<std::string, std::optional<Page>> before, after;
        std::vector<std::string> order_before, order_after;
        std::vector<Scan> scans_before, scans_after;
        Meta meta_before, meta_after;
    };
    void apply(Book& book, const std::map<std::string, std::optional<Page>>& pics,
               const std::vector<std::string>& order, const std::vector<Scan>& scans,
               const Meta& meta);

    std::vector<Step> m_steps;
    std::size_t m_pos = 0;
    std::size_t m_mark = 0;
    bool m_mark_valid = true;
    std::vector<std::string> m_touched;
};

}  // namespace optik::core
