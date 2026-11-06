// Shell_format.cpp -- the format bar over the text pane (s28, Scott: "a format
// bar should allow to set common formatting along with hotkeys").
//
//   B  I  U  Sc  x²  |  Heading
//
// Bold Ctrl+B, Italic Ctrl+I, Underline Ctrl+U, Small Caps Ctrl+Shift+K,
// Superscript Ctrl+Shift+P (a footnote mark), and Heading Ctrl+Shift+H -- the
// selected area between Heading and Text. A character format applies to the
// words the selection in the text pane touches, or to the current word; it is
// a word processor's toggle (all on -> off, else on) and one step. The buttons
// show the current word's formats. Export carries them as Writer's character
// styles (Emphasis, Strong Emphasis, Underline, Small Caps, Superscript).
#include "Shell.hpp"

#include <gtkmm/editable.h>

namespace optik {

namespace {

const char* fmt_name(unsigned bit) {
    switch (bit) {
        case ocr::kBold:        return "Bold";
        case ocr::kItalic:      return "Italic";
        case ocr::kUnderline:   return "Underline";
        case ocr::kSmallCaps:   return "Small caps";
        case ocr::kSuperscript: return "Superscript";
    }
    return "Format";
}

}  // namespace

void Shell::build_format_bar() {
    m_fmtbar.add_css_class("optik-format");
    m_fmtbar.set_margin_start(8);
    m_fmtbar.set_margin_end(8);
    m_fmtbar.set_margin_top(4);
    m_fmtbar.set_margin_bottom(4);

    struct Def { unsigned bit; const char* name; const char* markup; const char* tip; };
    const Def defs[] = {
        {ocr::kBold,        "format.bold",      "<b>B</b>",       "Bold (Ctrl+B)"},
        {ocr::kItalic,      "format.italic",    "<i>I</i>",       "Italic (Ctrl+I)"},
        {ocr::kUnderline,   "format.underline", "<u>U</u>",       "Underline (Ctrl+U)"},
        {ocr::kSmallCaps,   "format.smallcaps", "<span font_variant=\"small-caps\">Sc</span>", "Small caps (Ctrl+Shift+K)"},
        {ocr::kSuperscript, "format.super",     "x<sup>2</sup>",  "Superscript (Ctrl+Shift+P)"},
    };
    for (const Def& d : defs) {
        auto* b = Gtk::make_managed<widgets::ToggleButton>(d.name);
        auto* l = Gtk::make_managed<widgets::Label>(std::string(d.name) + ".label");
        l->set_markup(d.markup);
        b->set_child(*l);
        b->set_has_frame(false);
        b->set_focus_on_click(false);   // the keyboard stays in the text
        b->set_size_request(34, -1);
        b->set_tooltip_text(std::string(d.tip) + " -- the selected words, or the current word");
        const unsigned bit = d.bit;
        b->signal_clicked().connect([this, bit] { if (!m_fmt_syncing) format_words(bit); });
        m_fmtbar.append(*b);
        m_fmt_buttons.push_back({bit, b});
    }
    auto* sep = Gtk::make_managed<widgets::Separator>("format.sep", Gtk::Orientation::VERTICAL);
    sep->set_margin_start(6);
    sep->set_margin_end(6);
    m_fmtbar.append(*sep);
    m_fmt_heading = Gtk::make_managed<widgets::ToggleButton>("format.heading");
    m_fmt_heading->set_label("Heading");
    m_fmt_heading->set_has_frame(false);
    m_fmt_heading->set_focus_on_click(false);
    m_fmt_heading->set_tooltip_text("The selected area is a heading (Ctrl+Shift+H) -- Heading 1 in Writer");
    m_fmt_heading->signal_clicked().connect([this] { if (!m_fmt_syncing) format_heading(); });
    m_fmtbar.append(*m_fmt_heading);
}

void Shell::sync_format_bar() {
    if (m_fmt_buttons.empty()) return;
    m_fmt_syncing = true;
    const core::Page* p = current();
    const ocr::Word* w = p && m_word.valid() ? word_at(*p, m_word) : nullptr;
    const unsigned f = w ? ocr::fmt_of(*w) : 0u;
    for (auto& [bit, b] : m_fmt_buttons) {
        b->set_active(f & bit);
        b->set_sensitive(w != nullptr);
    }
    const bool area = p && m_area >= 0 && m_area < int(p->areas.size());
    m_fmt_heading->set_active(area && p->areas[std::size_t(m_area)].kind == ocr::Kind::Heading);
    m_fmt_heading->set_sensitive(area);
    m_fmt_syncing = false;
}

void Shell::format_words(unsigned bit) {
    m_text.commit_now();
    const core::Page* p = current();
    if (!p) { sync_format_bar(); return; }
    std::vector<WordRef> refs = m_text.selected_words();
    const bool selection = !refs.empty();
    if (!selection && m_word.valid() && word_at(*p, m_word)) refs.push_back(m_word);
    if (refs.empty()) { say("Select words in the text, or click a word, to format it."); sync_format_bar(); return; }

    std::vector<core::Hit> hits;
    for (const auto& r : refs) hits.push_back(core::Hit{m_page_id, r.area, r.para, r.line, r.word});
    const bool on = !core::all_have(m_book, hits, bit);
    std::string label = on ? fmt_name(bit) : std::string("Not ") + fmt_name(bit);
    if (!on) label[4] = char(label[4] - 'A' + 'a');   // "Not bold"
    if (hits.size() > 1) label += " (" + std::to_string(hits.size()) + " words)";
    step(label, [&](core::Edit& e) { core::set_format(e, hits, bit, on); });

    // The text pane was rebuilt: put the cursor -- and the selection -- back.
    const core::Page* now = current();
    if (now && word_at(*now, refs.front())) {
        const WordRef keep = selection ? refs.front() : m_word;
        if (word_at(*now, keep)) { on_word(keep, false); m_text.set_word(keep, true); }
        if (selection && word_at(*now, refs.back())) m_text.select_words(refs.front(), refs.back());
    }
    sync_format_bar();
}

void Shell::format_heading() {
    const core::Page* p = current();
    if (!p || m_area < 0 || m_area >= int(p->areas.size())) {
        say("Click an area on the page to make it a heading.");
        sync_format_bar();
        return;
    }
    const bool is = p->areas[std::size_t(m_area)].kind == ocr::Kind::Heading;
    set_area_kind(is ? ocr::Kind::Text : ocr::Kind::Heading);
    sync_format_bar();
}

// The arrows are the text's (and a field's) while the keyboard is there.
bool Shell::keyboard_in_text() {
    Gtk::Widget* f = get_focus();
    if (!f) return false;
    if (f == &m_text || f->is_ancestor(m_text)) return true;
    if (f->is_ancestor(m_find)) return true;
    return dynamic_cast<const Gtk::Editable*>(f) != nullptr;
}

}  // namespace optik
