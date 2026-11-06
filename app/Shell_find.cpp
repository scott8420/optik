// Shell_find.cpp -- find and replace across the book (s27, milestone 15).
//
// A bar over the panes (Ctrl+F; Esc closes): Find, Replace with, Match case,
// Whole words, Grep (s30: the query a grep -E pattern), Previous / Next, Replace / Replace All, and "3 of 27". A match is
// shown as the current word in both panes, the way F4 shows a word to check;
// focus stays in the bar, so Enter walks on. Replace All is one step.
#include "Shell.hpp"

#include <gtkmm/eventcontrollerkey.h>

#include <tuple>

namespace optik {

void Shell::build_find() {
    auto* row = Gtk::make_managed<widgets::Box>("find.row", Gtk::Orientation::HORIZONTAL, 6);
    row->add_css_class("optik-find");
    row->set_margin_start(8);
    row->set_margin_end(8);
    row->set_margin_top(4);
    row->set_margin_bottom(4);

    m_find_q.set_placeholder_text("Find in the book");
    m_find_q.set_width_chars(22);
    m_find_with.set_placeholder_text("Replace with");
    m_find_with.set_width_chars(18);
    m_find_n.set_width_chars(12);
    m_find_n.set_xalign(0);

    auto button = [](const char* name, const char* label, const char* tip) {
        auto* b = Gtk::make_managed<widgets::Button>(name);
        b->set_label(label);
        b->set_tooltip_text(tip);
        return b;
    };
    auto* prev = button("find.prev", "Previous", "Shift+Enter, Ctrl+Shift+G");
    auto* next = button("find.next", "Next", "Enter, Ctrl+G");
    auto* one  = button("find.replace", "Replace", "Replace in this word, then find the next (Enter in Replace with)");
    auto* all  = button("find.replace-all", "Replace All", "Every word in the book, as one step (Ctrl+Z takes it back)");
    auto* shut = Gtk::make_managed<widgets::Button>("find.close");
    shut->set_icon_name("window-close-symbolic");
    shut->set_has_frame(false);
    shut->set_tooltip_text("Close (Esc)");

    for (Gtk::Widget* w : std::initializer_list<Gtk::Widget*>{&m_find_q, prev, next, &m_find_with, one, all,
                                                             &m_find_case, &m_find_whole, &m_find_grep, &m_find_n})
        row->append(*w);
    auto* gap = Gtk::make_managed<widgets::Box>("find.gap", Gtk::Orientation::HORIZONTAL, 0);
    gap->set_hexpand(true);
    row->append(*gap);
    row->append(*shut);

    m_find.set_child(*row);
    m_find.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    m_find.set_reveal_child(false);

    prev->signal_clicked().connect([this] { find_step(-1); });
    next->signal_clicked().connect([this] { find_step(+1); });
    one->signal_clicked().connect([this] { find_replace_one(); });
    all->signal_clicked().connect([this] { find_replace_all(); });
    shut->signal_clicked().connect([this] { find_show(false); });
    m_find_q.signal_activate().connect([this] { find_step(+1); });
    m_find_with.signal_activate().connect([this] { find_replace_one(); });
    m_find_q.signal_changed().connect([this] { find_count(); refresh_actions(); });
    m_find_case.signal_toggled().connect([this] { find_count(); });
    m_find_whole.signal_toggled().connect([this] { find_count(); });
    m_find_grep.set_tooltip_text("Find a grep -E pattern in each word: . [a-z] * + ? {2} | ( ) ^ $ \\< \\>.\n"
                                 "Replace with \\1..\\9 for a group, & for the whole match (as sed)");
    m_find_grep.signal_toggled().connect([this] { find_count(); refresh_actions(); });

    // Shift+Enter in Find goes back.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType mods) {
        const bool shift = (mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
        if (shift && (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter)) { find_step(-1); return true; }
        return false;
    }, false);
    m_find_q.add_controller(keys);
}

void Shell::find_show(bool on) {
    m_find.set_reveal_child(on);
    if (on) {
        // The word under the cursor is the likeliest thing to look for.
        if (m_find_q.get_text().empty())
            if (const core::Page* p = current())
                if (const ocr::Word* w = m_word.valid() ? word_at(*p, m_word) : nullptr) m_find_q.set_text(w->text);
        m_find_q.grab_focus();
        m_find_q.select_region(0, -1);
        find_count();
    } else {
        m_text.grab_focus();
    }
}

core::FindOptions Shell::find_options() const {
    core::FindOptions o;
    o.match_case  = m_find_case.get_active();
    o.whole_words = m_find_whole.get_active();
    o.grep        = m_find_grep.get_active();
    return o;
}

// "3 of 27": where the current word stands among the matches.
void Shell::find_count() {
    const std::string q = m_find_q.get_text();
    m_find_q.remove_css_class("error");
    if (!m_has_book || q.empty()) { m_find_n.set_text(""); return; }
    std::string why;
    if (!core::pattern_ok(q, find_options(), &why)) {   // s30: say what grep would
        m_find_q.add_css_class("error");
        m_find_n.set_text("pattern: " + why);
        return;
    }
    const auto hits = core::find_all(m_book, q, find_options());
    if (hits.empty()) { m_find_n.set_text("not found"); return; }
    int at = 0;
    for (std::size_t i = 0; i < hits.size(); ++i)
        if (hits[i].page_id == m_page_id && hits[i].area == m_word.area && hits[i].para == m_word.para &&
            hits[i].line == m_word.line && hits[i].word == m_word.word)
            at = int(i) + 1;
    m_find_n.set_text(at ? std::to_string(at) + " of " + std::to_string(hits.size())
                         : std::to_string(hits.size()) + (hits.size() == 1 ? " match" : " matches"));
}

void Shell::find_step(int dir) {
    m_text.commit_now();
    const std::string q = m_find_q.get_text();
    if (!m_has_book || q.empty()) { find_show(true); return; }
    if (std::string why; !core::pattern_ok(q, find_options(), &why)) { say("The pattern will not do: " + why + "."); return; }
    const auto hits = core::find_all(m_book, q, find_options());
    if (hits.empty()) { say("“" + q + "” is not in the book."); find_count(); return; }

    // Book order as a tuple; the current word is the start (before the first
    // word of the current page when none is chosen).
    auto key = [this](const std::string& page, int a, int p, int l, int w) {
        return std::make_tuple(m_book.index_of(page), a, p, l, w);
    };
    const auto here = key(m_page_id, m_word.valid() ? m_word.area : -1, m_word.para, m_word.line, m_word.word);
    std::size_t pick = dir > 0 ? 0 : hits.size() - 1;   // round again
    if (dir > 0) {
        for (std::size_t i = 0; i < hits.size(); ++i)
            if (key(hits[i].page_id, hits[i].area, hits[i].para, hits[i].line, hits[i].word) > here) { pick = i; break; }
    } else {
        for (std::size_t i = hits.size(); i-- > 0;)
            if (key(hits[i].page_id, hits[i].area, hits[i].para, hits[i].line, hits[i].word) < here) { pick = i; break; }
    }
    const core::Hit& h = hits[pick];
    const int pi = m_book.index_of(h.page_id);
    if (pi != m_book.index_of(m_page_id)) go_to(pi);
    const WordRef r{h.area, h.para, h.line, h.word};
    on_word(r, false);
    m_text.set_word(r, false);   // the keyboard stays in the bar
    find_count();
}

void Shell::find_replace_one() {
    m_text.commit_now();
    const std::string q = m_find_q.get_text();
    if (q.empty()) return;
    const core::Page* p = current();
    const ocr::Word* w = p && m_word.valid() ? word_at(*p, m_word) : nullptr;
    if (w && !core::find_in(w->text, q, find_options()).empty()) {
        const core::Hit h{m_page_id, m_word.area, m_word.para, m_word.line, m_word.word};
        const std::string with = m_find_with.get_text();
        const auto o = find_options();
        step("Replace “" + q + "”", [&](core::Edit& e) { core::replace_hits(e, {h}, q, with, o); });
    }
    find_step(+1);
}

void Shell::find_replace_all() {
    m_text.commit_now();
    const std::string q = m_find_q.get_text();
    if (!m_has_book || q.empty()) return;
    const auto o = find_options();
    if (std::string why; !core::pattern_ok(q, o, &why)) { say("The pattern will not do: " + why + "."); return; }
    const auto hits = core::find_all(m_book, q, o);
    if (hits.empty()) { say("“" + q + "” is not in the book."); return; }
    const std::string with = m_find_with.get_text();
    int n = 0;
    step("Replace All “" + q + "”", [&](core::Edit& e) { n = core::replace_hits(e, hits, q, with, o); });
    say("Replaced " + std::to_string(n) + (n == 1 ? " time" : " times") + " in " +
        std::to_string(hits.size()) + (hits.size() == 1 ? " word." : " words."));
    find_count();
}

}  // namespace optik
