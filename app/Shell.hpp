#pragma once
// Shell -- the window. FineReader's three panes:
//
//   | Pages | the page, with its areas and uncertain words | its text |
//   status line: where you are, what is left to check, what is running
//
// The Shell OWNS the book, the journal and the worker pool, and is the only
// place user changes become journal steps. The panes only draw and report.
//
// Split class (CANON "the header is the index"). Where things live:
//   Shell.cpp          zones: construction, actions, keys, refresh, status
//   Shell_files.cpp    files: open, new from scans, save, export, book settings
//   Shell_read.cpp     reading: the pool, pictures, read page / all / area
//   Shell_verify.cpp   proofing: next to check, accept, text edits, areas

#include "PagesPanel.hpp"
#include "PageView.hpp"
#include "TextPanel.hpp"
#include "core/Book.hpp"
#include "core/Jobs.hpp"
#include "core/Recent.hpp"
#include "core/Undo.hpp"
#include "widgets/Widgets.hpp"

#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <glibmm/dispatcher.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/popover.h>

#include <map>
#include <memory>
#include <set>

namespace optik {

class App;

class Shell : public Gtk::ApplicationWindow {
public:
    explicit Shell(App& app);
    ~Shell() override;

    void open_path(const std::string& path);                 // files: book or scans folder
    void reopen_last();                                      // files: a plain start (s25)

private:
    // -- zones (Shell.cpp)
    void build_header();                                     // zone: header bar
    void build_body();                                       // zone: panes + status
    void build_actions(App& app);                            // zone: actions + accels
    bool on_key(guint key, guint code, Gdk::ModifierType mods);   // handler: keys (capture)
    bool on_close();                                         // handler: window close
    void refresh_all();                                      // helper: everything from the book
    void refresh_page(const std::string& id);               // helper: one page changed
    void refresh_status();                                   // helper: status line + title
    void refresh_actions();                                  // helper: what is enabled
    void say(const std::string& msg);                        // helper: status message
    void go_to(int index);                                   // helper: show page
    const core::Page* current() const;                       // helper
    bool step(const std::string& label, const std::function<void(core::Edit&)>& fn);  // the door
    void undo();                                             // handler: undo
    void redo();                                             // handler: redo

    // -- files (Shell_files.cpp)
    void on_open();                                          // handler: Open Book
    void on_new_from_scans();                                // handler: New from Scans
    void new_from_scans(const std::string& folder);          // files
    void open_book(const std::string& folder);               // files
    bool save();                                             // files
    void on_save_as();                                       // handler: Save As (s25)
    void refresh_recent_menu();                              // helper: the Recent submenu
    void on_about();                                         // handler: About
    void on_export();                                        // handler: Export
    void build_settings_popover();                           // zone: book settings
    void sync_settings_popover();                            // helper

    // -- reading (Shell_read.cpp)
    void read_pages(const std::vector<std::string>& ids, const std::string& label);  // reading
    void read_area(int area, const core::Rect& box, bool new_area);                 // reading
    void request_picture(const std::string& page_id);       // reading: page view
    void request_thumbs();                                   // reading: pages pane
    void batch_begin(const std::string& label, int total);  // reading: progress
    void batch_tick();                                       // reading
    void on_cancel();                                        // handler: Cancel

    // -- proofing (Shell_verify.cpp)
    void next_to_check();                                    // proofing: F4
    void accept_word();                                      // proofing: Ctrl+Enter
    void on_text_edited(const std::string& page_id, const std::vector<WordEdit>& edits);
    void on_word(const WordRef& w, bool from_text);          // proofing: either pane
    void set_area_kind(ocr::Kind k);                         // areas
    void delete_area();                                      // areas
    void on_area_drawn(const core::Rect& r);                 // areas
    void on_area_resized(int area, const core::Rect& r);     // areas

    // -- state
    core::Book    m_book;
    bool          m_has_book = false;
    core::Journal m_journal;
    core::Jobs    m_jobs;
    core::Recent  m_recent;                                  // the app's memory, not the book's
    Glib::RefPtr<Gio::Menu> m_recent_menu;
    std::unique_ptr<Gtk::Window> m_about;                   // made once, hidden on close
    Glib::Dispatcher m_dispatch;

    std::string m_page_id;
    WordRef     m_word;
    int         m_area = -1;
    std::map<std::string, std::shared_ptr<const core::Picture>> m_pictures;   // recent page pictures
    std::vector<std::string> m_picture_order;
    std::set<std::string> m_pictures_pending, m_thumbs_pending;

    std::string m_batch_label;
    int m_batch_total = 0, m_batch_done = 0;

    // -- widgets
    widgets::HeaderBar m_header{"header"};
    widgets::Label     m_title{"header.title"};
    widgets::Box       m_root{"root", Gtk::Orientation::VERTICAL};
    widgets::Paned     m_outer{"panes.outer", Gtk::Orientation::HORIZONTAL};
    widgets::Paned     m_inner{"panes.inner", Gtk::Orientation::HORIZONTAL};
    PagesPanel         m_pages;
    PageView           m_view;
    TextPanel          m_text;
    widgets::Box       m_status{"status", Gtk::Orientation::HORIZONTAL, 8};
    widgets::Label     m_status_where{"status.where"};
    widgets::Label     m_status_msg{"status.message"};
    widgets::ProgressBar m_progress{"status.progress"};
    widgets::Button    m_cancel{"status.cancel"};
    widgets::MenuButton m_settings_btn{"header.settings"};
    Gtk::Popover       m_settings_pop;
    std::function<void()> m_settings_sync;
    sigc::connection   m_msg_timer;

    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> m_actions;
};

}  // namespace optik
