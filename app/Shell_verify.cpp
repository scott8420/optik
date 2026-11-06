// Shell_verify.cpp -- proofing (next to check, accept, text edits) and areas.
#include "Shell.hpp"

#include "core/Reading.hpp"
#include "core/Spell.hpp"

namespace optik {

void Shell::on_word(const WordRef& w, bool from_text) {
    m_word = w;
    m_area = w.area;
    m_view.set_area(m_area);
    m_view.set_word(w);
    if (!from_text) m_text.set_word(w, false);
    refresh_actions();
}

// F4: the next word wanting a look, after the current one, through the book and
// round again. FineReader's verification, without the dialog: the word is shown
// in both panes, the cursor is in it, and typing fixes it.
void Shell::next_to_check() {
    if (!m_has_book || m_book.pages().empty()) return;
    m_text.commit_now();
    const int n = int(m_book.pages().size());
    const int start = std::max(0, m_book.index_of(m_page_id));
    for (int k = 0; k <= n; ++k) {
        const int pi = (start + k) % n;
        const core::Page& p = m_book.pages()[std::size_t(pi)];
        const bool same_page = k == 0;
        for (std::size_t a = 0; a < p.areas.size(); ++a) {
            if (!ocr::kind_is_text(p.areas[a].kind)) continue;
            for (std::size_t q = 0; q < p.areas[a].paras.size(); ++q)
                for (std::size_t l = 0; l < p.areas[a].paras[q].lines.size(); ++l) {
                    const auto& ws = p.areas[a].paras[q].lines[l].words;
                    for (std::size_t w = 0; w < ws.size(); ++w) {
                        const WordRef r{int(a), int(q), int(l), int(w)};
                        if (!m_book.uncertain(ws[w])) continue;
                        // On the current page, only words AFTER the current one --
                        // unless we have come all the way round.
                        if (same_page && k < n && m_word.valid() &&
                            std::tie(r.area, r.para, r.line, r.word) <=
                            std::tie(m_word.area, m_word.para, m_word.line, m_word.word))
                            continue;
                        if (pi != m_book.index_of(m_page_id)) go_to(pi);
                        on_word(r, false);
                        m_text.set_word(r, true);
                        return;
                    }
                }
        }
    }
    say("Nothing left to check.");
}

void Shell::accept_word() {
    m_text.commit_now();
    const core::Page* p = current();
    if (!p || !m_word.valid() || !word_at(*p, m_word)) return;
    const WordRef r = m_word;
    step("Accept word", [&](core::Edit& e) {
        auto& w = e.page(m_page_id).areas[std::size_t(r.area)].paras[std::size_t(r.para)]
                      .lines[std::size_t(r.line)].words[std::size_t(r.word)];
        w.checked = true;
    });
    next_to_check();
}

// Ctrl+Shift+Enter (s27): this word is right, and so is every word made of the
// same parts, in this book -- now and on every later read. The parts go into the
// book's words; every word the dictionary marked that is made only of book
// words loses the mark; this one is accepted. One step: Ctrl+Z takes it all back.
void Shell::add_book_word() {
    m_text.commit_now();
    const core::Page* p = current();
    const ocr::Word* cw = p && m_word.valid() ? word_at(*p, m_word) : nullptr;
    if (!cw) return;
    const auto parts = core::spell_parts(cw->text);
    if (parts.empty()) { say("Nothing in that word to remember."); return; }
    std::string shown;
    for (const auto& s : parts) shown += (shown.empty() ? "" : "-") + s;
    const WordRef r = m_word;
    const std::string here = m_page_id;
    step("Add \u201C" + shown + "\u201D to the book's words", [&](core::Edit& e) {
        for (const auto& s : parts) core::add_book_word(e.meta().words, s);
        const auto words = e.meta().words;
        for (const auto& pg : e.book().pages()) {
            // Touch only the pages that change (a step's pictures are per page).
            bool any = false;
            for (const auto& a : pg.areas)
                for (const auto& q : a.paras)
                    for (const auto& l : q.lines)
                        for (const auto& w : l.words) any = any || w.unknown;
            if (!any) continue;
            std::vector<ocr::Area> trial = pg.areas;
            if (core::clear_book_words(trial, words) > 0) e.page(pg.id).areas = std::move(trial);
        }
        auto& w = e.page(here).areas[std::size_t(r.area)].paras[std::size_t(r.para)]
                      .lines[std::size_t(r.line)].words[std::size_t(r.word)];
        w.checked = true;
    });
    next_to_check();
}

// Ctrl+I (s27): the author's say on italic -- s28: one of the format bar's
// verbs (Shell_format.cpp), on the selection or the current word.
void Shell::toggle_italic() { format_words(ocr::kItalic); }

void Shell::on_text_edited(const std::string& page_id, const std::vector<WordEdit>& edits) {
    if (!m_book.page(page_id)) return;
    // The text pane already shows the edit; rebuilding it under the author's
    // cursor would throw the cursor away. Write the book, redraw the rest.
    m_journal.run(m_book, edits.size() == 1 ? "Edit word" : "Edit text", [&](core::Edit& e) {
        core::Page& p = e.page(page_id);
        for (const auto& ed : edits) {
            if (!word_at(p, ed.ref)) continue;
            auto& w = p.areas[std::size_t(ed.ref.area)].paras[std::size_t(ed.ref.para)]
                          .lines[std::size_t(ed.ref.line)].words[std::size_t(ed.ref.word)];
            if (!w.edited) w.original = w.text;
            w.text = ed.text;
            w.edited = w.text != w.original;
            if (!w.edited) w.original.clear();
        }
    });
    m_pages.refresh(m_book, page_id);
    if (page_id == m_page_id) m_view.show(&m_book, current());
    refresh_status();
}

void Shell::set_area_kind(ocr::Kind k) {
    const core::Page* p = current();
    if (!p || m_area < 0 || m_area >= int(p->areas.size())) return;
    const int a = m_area;
    step(std::string("Area: ") + ocr::kind_name(k), [&](core::Edit& e) {
        auto& area = e.page(m_page_id).areas[std::size_t(a)];
        area.kind = k;
        area.user = true;
    });
    // A picture or ignored area that becomes text has no words yet: read it.
    if (ocr::kind_is_text(k) && current() && current()->areas[std::size_t(a)].paras.empty())
        read_area(a, current()->areas[std::size_t(a)].box, false);
}

void Shell::delete_area() {
    const core::Page* p = current();
    if (!p || m_area < 0 || m_area >= int(p->areas.size())) return;
    const int a = m_area;
    m_area = -1;
    m_word = {};
    step("Delete area", [&](core::Edit& e) {
        auto& v = e.page(m_page_id).areas;
        v.erase(v.begin() + a);
    });
}

void Shell::on_area_drawn(const core::Rect& r) {
    if (!current()) return;
    read_area(int(current()->areas.size()), r, true);
}

void Shell::on_area_resized(int area, const core::Rect& r) {
    read_area(area, r, false);
}

}  // namespace optik
