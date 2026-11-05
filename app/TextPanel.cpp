#include "TextPanel.hpp"

#include "Css.hpp"

#include <glibmm/main.h>

namespace optik {

TextPanel::TextPanel()
    : widgets::ScrolledWindow("text.scroll"), m_view("text.view") {
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_expand(true);
    m_view.add_css_class("optik-text");
    m_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_view.set_left_margin(14);
    m_view.set_right_margin(14);
    m_view.set_top_margin(12);
    m_view.set_bottom_margin(12);
    m_view.set_pixels_below_lines(2);
    set_child(m_view);

    m_buf = m_view.get_buffer();
    m_buf->set_enable_undo(false);
    m_tag_uncertain = m_buf->create_tag("uncertain");
    m_tag_uncertain->property_background_rgba() =
        Gdk::RGBA(css::kUncertain.r, css::kUncertain.g, css::kUncertain.b, css::kUncertain.a);
    m_tag_current = m_buf->create_tag("current");
    m_tag_current->property_underline() = Pango::Underline::DOUBLE;
    m_tag_current->property_underline_rgba() =
        Gdk::RGBA(css::kCurrent.r, css::kCurrent.g, css::kCurrent.b, 1.0);
    m_tag_chrome = m_buf->create_tag("chrome");
    m_tag_chrome->property_foreground_rgba() = Gdk::RGBA(css::kChrome.r, css::kChrome.g, css::kChrome.b, 1.0);
    m_tag_chrome->property_style() = Pango::Style::ITALIC;
    m_tag_chrome->property_scale() = 0.85;
    m_tag_empty = m_buf->create_tag("empty");
    m_tag_empty->property_foreground_rgba() = Gdk::RGBA(0.6, 0.6, 0.6, 1.0);
    m_tag_empty->property_editable() = false;

    m_buf->signal_changed().connect(sigc::mem_fun(*this, &TextPanel::on_changed));
    m_buf->signal_mark_set().connect(sigc::mem_fun(*this, &TextPanel::on_mark_set));
}

void TextPanel::show(const core::Book* book, const core::Page* page) {
    commit_now();
    m_building = true;
    m_book = book;
    for (auto& s : m_slots) { m_buf->delete_mark(s.a); m_buf->delete_mark(s.b); }
    m_slots.clear();
    m_buf->set_text("");
    m_page_id = page ? page->id : std::string();
    if (page && !page->read) {
        m_buf->insert_with_tag(m_buf->end(), "Not read yet.  Read Page (Ctrl+R) or Read All.", m_tag_empty);
    } else if (page) {
        bool first_area = true;
        struct Span { int a, b; bool chrome; };
        std::vector<Span> offsets;
        for (std::size_t ai = 0; ai < page->areas.size(); ++ai) {
            const auto& a = page->areas[ai];
            const bool chrome = a.kind == ocr::Kind::Header || a.kind == ocr::Kind::Footer;
            if (!ocr::kind_is_text(a.kind) && !chrome) continue;
            for (std::size_t pi = 0; pi < a.paras.size(); ++pi) {
                if (!first_area) m_buf->insert(m_buf->end(), "\n");
                first_area = false;
                const auto& p = a.paras[pi];
                for (std::size_t li = 0; li < p.lines.size(); ++li) {
                    const auto& ws = p.lines[li].words;
                    for (std::size_t wi = 0; wi < ws.size(); ++wi) {
                        if (wi) m_buf->insert(m_buf->end(), " ");
                        // Offsets now, marks after: an end mark sticks RIGHT so
                        // typing at a word's end extends it -- which also means it
                        // would slide along with every insert that follows while
                        // the page is being built.
                        Slot s;
                        s.ref = WordRef{int(ai), int(pi), int(li), int(wi)};
                        s.shown = ws[wi].text;
                        offsets.push_back({m_buf->end().get_offset(), 0, chrome});
                        m_buf->insert(m_buf->end(), ws[wi].text);
                        offsets.back().b = m_buf->end().get_offset();
                        m_slots.push_back(std::move(s));
                    }
                    m_buf->insert(m_buf->end(), "\n");
                }
            }
        }
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            auto& sl = m_slots[i];
            sl.a = m_buf->create_mark(m_buf->get_iter_at_offset(offsets[i].a), true);
            sl.b = m_buf->create_mark(m_buf->get_iter_at_offset(offsets[i].b), false);
            if (offsets[i].chrome)
                m_buf->apply_tag(m_tag_chrome, m_buf->get_iter_at_mark(sl.a), m_buf->get_iter_at_mark(sl.b));
        }
        if (m_slots.empty())
            m_buf->insert_with_tag(m_buf->end(), "No text found on this page.", m_tag_empty);
        retag();
        if (page) {
            // Keep the uncertain marks of the page we were handed.
            for (auto& s : m_slots) {
                const ocr::Word* w = word_at(*page, s.ref);
                // Running heads are not counted as words to check; do not mark them.
                const auto kind = page->areas[std::size_t(s.ref.area)].kind;
                if (w && book && ocr::kind_is_text(kind) && book->uncertain(*w))
                    m_buf->apply_tag(m_tag_uncertain, m_buf->get_iter_at_mark(s.a),
                                     m_buf->get_iter_at_mark(s.b));
            }
        }
    }
    m_buf->place_cursor(m_buf->begin());
    m_current = {};
    m_building = false;
}

void TextPanel::retag() {
    m_buf->remove_tag(m_tag_current, m_buf->begin(), m_buf->end());
    for (const auto& s : m_slots)
        if (s.ref == m_current)
            m_buf->apply_tag(m_tag_current, m_buf->get_iter_at_mark(s.a), m_buf->get_iter_at_mark(s.b));
}

void TextPanel::set_word(const WordRef& w, bool focus) {
    m_current = w;
    retag();
    for (const auto& s : m_slots) {
        if (!(s.ref == w)) continue;
        auto it = m_buf->get_iter_at_mark(s.a);
        const bool was = m_building;
        m_building = true;   // our own cursor move is not the author's
        m_buf->place_cursor(it);
        m_building = was;
        m_view.scroll_to(s.a, 0.2);
        if (focus) m_view.grab_focus();
        return;
    }
}

int TextPanel::slot_at(int off) const {
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const int a = m_buf->get_iter_at_mark(m_slots[i].a).get_offset();
        const int b = m_buf->get_iter_at_mark(m_slots[i].b).get_offset();
        if (off >= a && off <= b) return int(i);
    }
    return -1;
}

void TextPanel::on_mark_set(const Gtk::TextBuffer::iterator& it, const Glib::RefPtr<Gtk::TextMark>& m) {
    if (m_building || !m || m->get_name() != "insert") return;
    const int i = slot_at(it.get_offset());
    if (i < 0) return;
    const WordRef r = m_slots[std::size_t(i)].ref;
    if (r == m_current) return;
    m_current = r;
    retag();
    signal_word_focused.emit(r);
}

void TextPanel::on_changed() {
    if (m_building) return;
    m_pending = true;
    m_timer.disconnect();
    m_timer = Glib::signal_timeout().connect([this] { commit_now(); return false; }, 700);
}

bool TextPanel::commit_now() {
    m_timer.disconnect();
    if (!m_pending) return false;
    m_pending = false;
    std::vector<WordEdit> edits;
    for (auto& s : m_slots) {
        std::string now = m_buf->get_text(m_buf->get_iter_at_mark(s.a), m_buf->get_iter_at_mark(s.b), false);
        if (now != s.shown) {
            edits.push_back(WordEdit{s.ref, now});
            s.shown = now;
            m_buf->remove_tag(m_tag_uncertain, m_buf->get_iter_at_mark(s.a), m_buf->get_iter_at_mark(s.b));
        }
    }
    if (edits.empty()) return false;
    signal_edited.emit(m_page_id, std::move(edits));
    return true;
}

}  // namespace optik
