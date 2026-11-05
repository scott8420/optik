// Shell.cpp -- zones: construction, actions, keys, refresh, status.
#include "Shell.hpp"

#include "App.hpp"
#include "Log.hpp"

#include <giomm/menu.h>
#include <glibmm/main.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/togglebutton.h>

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace optik {

namespace {
std::vector<std::string> order_of(const core::Book& b) {
    std::vector<std::string> v;
    for (const auto& p : b.pages()) v.push_back(p.id);
    return v;
}
}  // namespace

Shell::Shell(App& app) {
    set_name("shell");
    set_title("Optik");
    set_icon_name("optik-logo-symbolic");
    set_default_size(1500, 950);
    m_recent.load();

    build_header();
    build_body();
    build_actions(app);

    m_jobs.set_notify([this] { m_dispatch.emit(); });
    m_dispatch.connect([this] {
        m_jobs.pump();
        refresh_status();
    });

    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(sigc::mem_fun(*this, &Shell::on_key), false);
    add_controller(keys);
    signal_close_request().connect(sigc::mem_fun(*this, &Shell::on_close), false);

    refresh_recent_menu();
    refresh_all();
}

Shell::~Shell() { m_jobs.cancel(); }

// -- zone: header bar ----------------------------------------------------------

void Shell::build_header() {
    auto button = [](const char* name, const char* label, const char* action, const char* tip) {
        auto* b = Gtk::make_managed<widgets::Button>(name, label);
        b->set_action_name(action);
        b->set_tooltip_text(tip);
        return b;
    };
    m_header.pack_start(*button("header.open", "Open", "win.open", "Open a book (Ctrl+O)"));
    m_header.pack_start(*button("header.new", "New from Scans", "win.new", "Make a book from a folder of scans (Ctrl+N)"));
    auto* undo = button("header.undo", "", "win.undo", "Undo (Ctrl+Z)");
    undo->set_icon_name("edit-undo-symbolic");
    auto* redo = button("header.redo", "", "win.redo", "Redo (Ctrl+Shift+Z)");
    redo->set_icon_name("edit-redo-symbolic");
    m_header.pack_start(*undo);
    m_header.pack_start(*redo);

    m_title.set_text("Optik");
    m_title.add_css_class("title");
    m_header.set_title_widget(m_title);

    // Right side, packed end-first.
    auto* menu = Gtk::make_managed<widgets::MenuButton>("header.menu");
    menu->set_icon_name("open-menu-symbolic");
    auto model = Gio::Menu::create();
    model->append("Save", "win.save");
    model->append("Save As…", "win.save-as");
    model->append("Export…", "win.export");
    m_recent_menu = Gio::Menu::create();
    model->append_submenu("Recent Books", m_recent_menu);
    auto keys = Gio::Menu::create();
    keys->append("Next word to check   F4", "win.next");
    keys->append("Accept word   Ctrl+Enter", "win.accept");
    keys->append("Next page   Ctrl+PgDn", "win.page-next");
    keys->append("Previous page   Ctrl+PgUp", "win.page-prev");
    model->append_section(keys);
    auto view = Gio::Menu::create();
    view->append("Fit page", "win.fit-page");
    view->append("Fit width", "win.fit-width");
    view->append("Zoom in  (or Ctrl+wheel)", "win.zoom-in");
    view->append("Zoom out", "win.zoom-out");
    model->append_section(view);
    auto about = Gio::Menu::create();
    about->append("About Optik", "win.about");
    model->append_section(about);
    menu->set_menu_model(model);
    m_header.pack_end(*menu);

    m_header.pack_end(*button("header.export", "Export", "win.export", "Export the book as ODT or text (Ctrl+E)"));
    m_settings_btn.set_label("Book");
    m_settings_btn.set_tooltip_text("This book's settings: language, what to check, export rules");
    build_settings_popover();
    m_header.pack_end(m_settings_btn);
    m_header.pack_end(*button("header.next", "Next to Check", "win.next", "Next uncertain word (F4)"));

    auto* area = Gtk::make_managed<widgets::MenuButton>("header.area");
    area->set_label("Area");
    area->set_tooltip_text("The selected area: what it is, read it again, delete it");
    auto am = Gio::Menu::create();
    auto kinds = Gio::Menu::create();
    kinds->append("Text", "win.area-kind::text");
    kinds->append("Heading", "win.area-kind::heading");
    kinds->append("Picture", "win.area-kind::picture");
    kinds->append("Running head", "win.area-kind::header");
    kinds->append("Folio / footer", "win.area-kind::footer");
    kinds->append("Ignore", "win.area-kind::ignore");
    am->append_section(kinds);
    auto acts = Gio::Menu::create();
    acts->append("Read area again", "win.area-read");
    acts->append("Delete area   Del", "win.area-delete");
    am->append_section(acts);
    area->set_menu_model(am);
    m_header.pack_end(*area);

    auto* draw = Gtk::make_managed<widgets::ToggleButton>("header.draw", "Draw Area");
    draw->set_tooltip_text("Drag on the page to add a text area; it is read at once (Esc leaves)");
    draw->signal_toggled().connect([this, draw] {
        m_view.set_tool(draw->get_active() ? PageView::Tool::Draw : PageView::Tool::Select);
    });
    m_header.pack_end(*draw);
    m_header.pack_end(*button("header.readall", "Read All", "win.read-all", "Read every page not read yet (Ctrl+Shift+R)"));
    m_header.pack_end(*button("header.read", "Read Page", "win.read-page", "Read this page again (Ctrl+R)"));
    set_titlebar(m_header);
}

// -- zone: panes and status --------------------------------------------------------

void Shell::build_body() {
    m_inner.set_start_child(m_view);
    m_inner.set_end_child(m_text);
    m_inner.set_resize_start_child(true);
    m_inner.set_shrink_start_child(false);
    m_inner.set_position(760);
    m_outer.set_start_child(m_pages);
    m_outer.set_end_child(m_inner);
    m_outer.set_resize_start_child(false);
    m_outer.set_shrink_start_child(false);
    m_outer.set_position(230);
    m_outer.set_vexpand(true);

    m_status.add_css_class("optik-status");
    m_status_where.set_xalign(0);
    m_status_msg.set_xalign(0);
    m_status_msg.set_hexpand(true);
    m_status_msg.set_ellipsize(Pango::EllipsizeMode::END);
    m_progress.set_size_request(220, -1);
    m_progress.set_valign(Gtk::Align::CENTER);
    m_progress.set_show_text(true);
    m_cancel.set_label("Cancel");
    m_cancel.set_action_name("win.cancel");
    m_status.append(m_status_where);
    m_status.append(m_status_msg);
    m_status.append(m_progress);
    m_status.append(m_cancel);

    m_root.append(m_outer);
    m_root.append(m_status);
    set_child(m_root);

    m_pages.signal_page_selected.connect([this](int i) { go_to(i); });
    m_view.signal_word_clicked.connect([this](WordRef w) { on_word(w, false); });
    m_view.signal_area_selected.connect([this](int a) { m_area = a; refresh_actions(); });
    m_view.signal_area_drawn.connect(sigc::mem_fun(*this, &Shell::on_area_drawn));
    m_view.signal_area_resized.connect(sigc::mem_fun(*this, &Shell::on_area_resized));
    m_view.signal_zoom_changed.connect([this] { refresh_status(); });
    m_text.signal_word_focused.connect([this](WordRef w) { on_word(w, true); });
    m_text.signal_edited.connect(sigc::mem_fun(*this, &Shell::on_text_edited));
}

// -- zone: actions --------------------------------------------------------------------

void Shell::build_actions(App& app) {
    auto add = [this](const char* name, std::function<void()> fn) {
        m_actions[name] = add_action(name, fn);
    };
    add("open", [this] { on_open(); });
    add("new", [this] { on_new_from_scans(); });
    add("save", [this] { if (save()) say("Saved."); });
    add("save-as", [this] { on_save_as(); });
    add("about", [this] { on_about(); });
    add("export", [this] { on_export(); });
    add("undo", [this] { undo(); });
    add("redo", [this] { redo(); });
    add("read-page", [this] { if (current()) read_pages({m_page_id}, "Reading the page"); });
    add("read-all", [this] {
        std::vector<std::string> ids;
        for (const auto& p : m_book.pages()) if (!p.read) ids.push_back(p.id);
        if (ids.empty()) { say("Every page has been read. Read Page reads one again."); return; }
        read_pages(ids, "Reading");
    });
    add("next", [this] { next_to_check(); });
    add("accept", [this] { accept_word(); });
    add("page-next", [this] { go_to(m_book.index_of(m_page_id) + 1); });
    add("page-prev", [this] { go_to(m_book.index_of(m_page_id) - 1); });
    add("cancel", [this] { on_cancel(); });
    add("area-read", [this] {
        const core::Page* p = current();
        if (p && m_area >= 0 && m_area < int(p->areas.size()))
            read_area(m_area, p->areas[std::size_t(m_area)].box, false);
    });
    add("area-delete", [this] { delete_area(); });
    add("fit-page",  [this] { m_view.set_fit(PageView::Fit::Page); });
    add("fit-width", [this] { m_view.set_fit(PageView::Fit::Width); });
    add("zoom-in",   [this] { m_view.zoom_by(1.25); });
    add("zoom-out",  [this] { m_view.zoom_by(0.8); });
    m_actions["open-recent"] = add_action_with_parameter(
        "open-recent", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& v) {
            open_path(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get());
        });
    m_actions["area-kind"] = add_action_with_parameter(
        "area-kind", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& v) {
            ocr::Kind k;
            if (ocr::kind_from(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get(), k))
                set_area_kind(k);
        });

    app.set_accels_for_action("win.open", {"<Control>o"});
    app.set_accels_for_action("win.new", {"<Control>n"});
    app.set_accels_for_action("win.save", {"<Control>s"});
    app.set_accels_for_action("win.save-as", {"<Control><Shift>s"});
    app.set_accels_for_action("win.export", {"<Control>e"});
    app.set_accels_for_action("win.read-page", {"<Control>r"});
    app.set_accels_for_action("win.read-all", {"<Control><Shift>r"});
    app.set_accels_for_action("win.fit-page", {"<Control>0"});
    app.set_accels_for_action("win.fit-width", {"<Control>1"});
    app.set_accels_for_action("win.zoom-in", {"<Control>plus", "<Control>equal", "<Control>KP_Add"});
    app.set_accels_for_action("win.zoom-out", {"<Control>minus", "<Control>KP_Subtract"});
    // Undo / redo / accept / F4 / paging are caught in on_key (capture), so they
    // reach the journal even from inside the text pane.
}

bool Shell::on_key(guint key, guint, Gdk::ModifierType mods) {
    const bool ctrl  = (mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool shift = (mods & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    if (ctrl && (key == GDK_KEY_z || key == GDK_KEY_Z)) { shift ? redo() : undo(); return true; }
    if (ctrl && (key == GDK_KEY_y || key == GDK_KEY_Y)) { redo(); return true; }
    if (ctrl && (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter)) { accept_word(); return true; }
    if (key == GDK_KEY_F4) { next_to_check(); return true; }
    if (ctrl && key == GDK_KEY_Page_Down) { go_to(m_book.index_of(m_page_id) + 1); return true; }
    if (ctrl && key == GDK_KEY_Page_Up)   { go_to(m_book.index_of(m_page_id) - 1); return true; }
    if (key == GDK_KEY_Escape) {
        if (m_view.tool() == PageView::Tool::Draw) {
            if (auto* t = dynamic_cast<Gtk::ToggleButton*>(registry::find("header.draw"))) t->set_active(false);
            return true;
        }
        if (m_batch_total) { on_cancel(); return true; }
    }
    // Delete removes the selected AREA only when the keyboard is not in the text.
    if (key == GDK_KEY_Delete && m_area >= 0) {
        auto* f = get_focus();
        const bool in_text = f && (f == &m_text || f->is_ancestor(m_text));
        if (!in_text) { delete_area(); return true; }
    }
    return false;
}

bool Shell::on_close() {
    m_text.commit_now();
    m_jobs.cancel();
    if (m_has_book && m_journal.dirty()) save();
    return false;
}

// -- the door -------------------------------------------------------------------------

bool Shell::step(const std::string& label, const std::function<void(core::Edit&)>& fn) {
    if (!m_has_book) return false;
    const auto before = order_of(m_book);
    if (!m_journal.run(m_book, label, fn)) return false;
    if (order_of(m_book) != before) refresh_all();
    else for (const auto& id : m_journal.last_touched()) refresh_page(id);
    refresh_status();
    return true;
}

void Shell::undo() {
    m_text.commit_now();
    const std::string label = m_journal.undo_label();
    const auto before = order_of(m_book);
    if (!m_journal.undo(m_book)) return;
    // A step that touched no page (settings, title) can change every page's count.
    if (order_of(m_book) != before || m_journal.last_touched().empty()) refresh_all();
    else for (const auto& id : m_journal.last_touched()) refresh_page(id);
    say("Undid " + label + ".");
}

void Shell::redo() {
    m_text.commit_now();
    const std::string label = m_journal.redo_label();
    const auto before = order_of(m_book);
    if (!m_journal.redo(m_book)) return;
    // A step that touched no page (settings, title) can change every page's count.
    if (order_of(m_book) != before || m_journal.last_touched().empty()) refresh_all();
    else for (const auto& id : m_journal.last_touched()) refresh_page(id);
    say("Redid " + label + ".");
}

// -- refresh --------------------------------------------------------------------------

const core::Page* Shell::current() const { return m_has_book ? m_book.page(m_page_id) : nullptr; }

void Shell::refresh_all() {
    m_pages.rebuild(m_book);
    int i = m_book.index_of(m_page_id);
    if (i < 0 && !m_book.pages().empty()) i = 0;
    m_page_id.clear();
    go_to(i);
    request_thumbs();
    if (m_settings_sync) m_settings_sync();
    refresh_status();
}

void Shell::refresh_page(const std::string& id) {
    m_pages.refresh(m_book, id);
    if (id != m_page_id) return;
    const core::Page* p = current();
    m_view.show(&m_book, p);
    m_text.show(&m_book, p);
    if (p && m_word.valid() && word_at(*p, m_word)) {
        m_view.set_word(m_word);
        m_text.set_word(m_word, false);
    }
    if (p && m_area >= int(p->areas.size())) m_area = -1;
    m_view.set_area(m_area);
    refresh_actions();
}

void Shell::go_to(int index) {
    if (index < 0 || index >= int(m_book.pages().size())) {
        if (m_book.pages().empty()) {
            m_page_id.clear();
            m_view.show(nullptr, nullptr);
            m_text.show(nullptr, nullptr);
            refresh_status();
        }
        return;
    }
    m_text.commit_now();
    const std::string id = m_book.pages()[std::size_t(index)].id;
    const bool other = id != m_page_id;
    m_page_id = id;
    if (other) { m_word = {}; m_area = -1; }
    m_pages.select(index);
    m_view.show(&m_book, current());
    m_text.show(&m_book, current());
    auto it = m_pictures.find(id);
    if (it != m_pictures.end()) m_view.set_picture(id, it->second);
    else request_picture(id);
    refresh_actions();
    refresh_status();
}

void Shell::refresh_status() {
    std::string where;
    int book_left = 0;
    for (const auto& p : m_book.pages()) book_left += m_book.uncertain_count(p);
    if (!m_has_book) {
        where = "Open a book, or make one from a folder of scans.";
    } else if (const core::Page* p = current()) {
        const int i = m_book.index_of(p->id);
        where = "Page " + std::to_string(i + 1) + " of " + std::to_string(m_book.pages().size());
        if (p->read) where += "  ·  " + std::to_string(m_book.uncertain_count(*p)) + " to check here";
        where += "  ·  " + std::to_string(book_left) + " in the book";
        if (const double z = m_view.zoom(); z > 0)
            where += "  ·  " + std::to_string(int(std::lround(z * 100))) + "%";
    } else {
        where = "No pages.";
    }
    m_status_where.set_text(where);

    const bool busy = m_batch_total > 0;
    m_progress.set_visible(busy);
    m_cancel.set_visible(busy);
    if (busy) {
        m_progress.set_fraction(double(m_batch_done) / m_batch_total);
        m_progress.set_text(m_batch_label + "  " + std::to_string(m_batch_done) + " / " +
                            std::to_string(m_batch_total));
    }

    std::string t = m_has_book ? m_book.meta().title : std::string("Optik");
    if (m_has_book && m_journal.dirty()) t = "• " + t;
    m_title.set_text(t);
    set_title(m_has_book ? m_book.meta().title + " — Optik" : "Optik");
    refresh_actions();
}

void Shell::refresh_actions() {
    auto en = [this](const char* name, bool on) {
        auto it = m_actions.find(name);
        if (it != m_actions.end()) it->second->set_enabled(on);
    };
    const core::Page* p = current();
    const bool area = p && m_area >= 0 && m_area < int(p->areas.size());
    en("save", m_has_book);
    en("export", m_has_book && !m_book.pages().empty());
    en("undo", m_journal.can_undo());
    en("redo", m_journal.can_redo());
    en("read-page", p != nullptr);
    en("read-all", m_has_book && !m_book.pages().empty());
    en("next", m_has_book);
    en("accept", m_word.valid());
    en("area-kind", area);
    en("area-read", area);
    en("area-delete", area);
    en("cancel", m_batch_total > 0);
    m_settings_btn.set_sensitive(m_has_book);
}

void Shell::say(const std::string& msg) {
    m_status_msg.set_text(msg);
    if (auto lg = log::get(log::Area::App)) lg->info("say: {}", msg);
    m_msg_timer.disconnect();
    m_msg_timer = Glib::signal_timeout().connect_seconds([this] { m_status_msg.set_text(""); return false; }, 8);
}

}  // namespace optik
