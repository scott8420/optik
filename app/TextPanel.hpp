#pragma once
// TextPanel -- what the page says, beside the page. Line for line as printed,
// paragraphs apart, uncertain words in amber; running heads and folios dimmed.
//
// It is editable, and an edit is to WORDS: each word on show is held between
// two marks (start sticks left, end sticks right), so typing inside or at the
// edge of a word changes that word. A burst of typing is committed as ONE
// journal step after a short pause, or at once when the Shell asks (page
// change, undo, save) -- commit_now(). The TextView's own undo is off: there is
// one undo, the journal's.

#include "PageView.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/textbuffer.h>
#include <sigc++/signal.h>

#include <string>
#include <vector>

namespace optik {

struct WordEdit {
    WordRef     ref;
    std::string text;
};

class TextPanel : public widgets::ScrolledWindow {
public:
    TextPanel();

    void show(const core::Book* book, const core::Page* page);
    void set_word(const WordRef& w, bool focus);   // highlight + scroll + cursor
    bool commit_now();                              // true if an edit went out

    sigc::signal<void(WordRef)>                                  signal_word_focused;
    sigc::signal<void(std::string, std::vector<WordEdit>)>      signal_edited;

private:
    struct Slot {
        WordRef ref;
        Glib::RefPtr<Gtk::TextMark> a, b;
        std::string shown;
    };

    void on_changed();
    void on_mark_set(const Gtk::TextBuffer::iterator& it, const Glib::RefPtr<Gtk::TextMark>& m);
    int  slot_at(int offset) const;
    void retag();

    widgets::TextView m_view;
    Glib::RefPtr<Gtk::TextBuffer> m_buf;
    Glib::RefPtr<Gtk::TextTag> m_tag_uncertain, m_tag_current, m_tag_chrome, m_tag_empty;

    const core::Book* m_book = nullptr;
    std::string       m_page_id;
    std::vector<Slot> m_slots;
    WordRef           m_current;
    bool              m_building = false;
    bool              m_pending = false;
    sigc::connection  m_timer;
};

}  // namespace optik
