// Shell_files.cpp -- files: open, new from scans, save, export, book settings.
#include "Shell.hpp"

#include "Log.hpp"
#include "core/Spell.hpp"
#include "core/Export.hpp"
#include "core/Languages.hpp"
#include "model/Intake.hpp"

#include <giomm/file.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace optik {

void Shell::open_path(const std::string& path) {
    std::error_code ec;
    if (fs::exists(fs::path(path) / "book.json", ec)) open_book(path);
    else if (fs::is_directory(path, ec))             new_from_scans(path);
    else say("Not a book or a folder: " + path);
}

void Shell::on_open() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("Open a book (its .optik folder)");
    const auto known = m_recent.existing();   // start where the last book lives
    if (!known.empty())
        dlg->set_initial_folder(Gio::File::create_for_path(fs::path(known[0]).parent_path().string()));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            if (auto f = dlg->select_folder_finish(r)) open_path(f->get_path());
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void Shell::on_new_from_scans() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("Choose the folder of scans");
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            if (auto f = dlg->select_folder_finish(r)) new_from_scans(f->get_path());
        } catch (const Glib::Error&) {}
    });
}

void Shell::open_book(const std::string& folder) {
    if (m_has_book && m_journal.dirty()) { m_text.commit_now(); save(); }
    m_jobs.cancel();
    m_batch_total = m_batch_done = 0;
    core::Book b;
    const auto st = core::Book::open(folder, b);
    if (st != core::Status::Ok) {
        say(std::string("Could not open: ") + core::status_text(st));
        if (st == core::Status::NotABook) {   // gone from where it was: off the list
            m_recent.forget(folder);
            m_recent.save();
            refresh_recent_menu();
        }
        return;
    }
    m_book = std::move(b);
    m_has_book = true;
    m_journal.clear();
    m_journal.mark();
    m_pictures.clear();
    m_picture_order.clear();
    m_pictures_pending.clear();
    m_thumbs_pending.clear();
    m_page_id.clear();
    m_recent.touch(m_book.folder());
    m_recent.save();
    refresh_recent_menu();
    refresh_all();
    say("Opened " + m_book.meta().title + ": " + std::to_string(m_book.pages().size()) + " pages.");
}

// The book goes BESIDE the scans: <folder>.optik. The scans are copied in, so
// the book is whole on its own and the originals are never touched.
void Shell::new_from_scans(const std::string& folder) {
    const fs::path src = fs::path(folder).lexically_normal();
    const std::string name = (src.has_filename() ? src : src.parent_path()).filename().string();
    const std::string book_dir = ((src.has_filename() ? src : src.parent_path()).string()) + ".optik";

    std::error_code ec;
    if (fs::exists(fs::path(book_dir) / "book.json", ec)) {
        open_book(book_dir);
        say("This folder already has a book; opened it.");
        return;
    }
    const auto found = model::intake::discover(folder);
    if (!found.ok() || found.scans.empty()) { say(found.explain()); return; }

    core::Book b;
    const auto st = core::Book::create(book_dir, name, b);
    if (st != core::Status::Ok) { say(std::string("Could not make the book: ") + core::status_text(st)); return; }
    open_book(book_dir);

    std::vector<std::string> paths;
    for (const auto& f : found.scans) paths.push_back(f.path);
    const bool split = m_book.meta().settings.split_spreads;
    const std::string dest = m_book.folder();
    batch_begin("Importing the scans", 1, true);
    m_jobs.submit([this, paths, split, dest](ocr::Engine&) -> core::Jobs::Landing {
        auto plan = std::make_shared<core::ImportPlan>(core::plan_import(paths, split));
        auto copied = std::make_shared<std::vector<core::Scan>>(core::copy_scans(dest, plan->paths));
        return [this, plan, copied, dest] {
            batch_tick();   // first: the read that follows starts its own batch
            if (!m_has_book || m_book.folder() != dest) return;
            const std::size_t n = copied->size();
            step("Import " + std::to_string(n) + " scans", [&](core::Edit& e) {
                std::vector<core::Scan> scans;
                for (const auto& s : *copied) scans.push_back(e.add_scan(s));
                for (const auto& p : core::pages_for(*plan, scans))
                    e.insert_page(e.book().pages().size(), p);
            });
            save();
            std::string msg = std::to_string(n) + " scans, " + std::to_string(m_book.pages().size()) + " pages.";
            if (!plan->unreadable.empty())
                msg += "  " + std::to_string(plan->unreadable.size()) + " could not be read.";
            say(msg);
            go_to(0);
            activate_action("win.read-all");
        };
    });
}

bool Shell::save() {
    if (!m_has_book) return false;
    m_text.commit_now();
    const auto st = m_book.save();
    if (st != core::Status::Ok) { say(std::string("Could not save: ") + core::status_text(st)); return false; }
    m_journal.mark();
    refresh_status();
    return true;
}

// -- s25: the app remembers its books ------------------------------------------------

void Shell::reopen_last() {
    if (m_has_book) return;
    for (const auto& b : m_recent.existing()) {
        open_book(b);
        if (m_has_book) return;
    }
}

void Shell::refresh_recent_menu() {
    if (!m_recent_menu) return;
    m_recent_menu->remove_all();
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    for (const auto& b : m_recent.existing()) {
        if (m_has_book && b == fs::absolute(m_book.folder()).lexically_normal().string()) continue;
        std::string where = fs::path(b).parent_path().string();
        if (!h.empty() && where.rfind(h, 0) == 0) where = "~" + where.substr(h.size());
        const std::string label = fs::path(b).stem().string() + "    " + where;
        auto item = Gio::MenuItem::create(label, "");
        item->set_action_and_target("win.open-recent", Glib::Variant<Glib::ustring>::create(b));
        m_recent_menu->append_item(item);
    }
    if (m_recent_menu->get_n_items() == 0) {
        auto none = Gio::MenuItem::create("(none yet)", "win.no-such-action");
        m_recent_menu->append_item(none);
    }
}

// Save As: save, copy the whole book folder on a worker (scans and all -- the
// book is whole on its own), then open the copy and give it the new name. The
// original stays as it was saved.
void Shell::on_save_as() {
    if (!m_has_book) return;
    if (!m_jobs.idle()) { say("Wait for the work under way to finish (or Cancel), then Save As."); return; }
    if (!save()) return;
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("Save the book as (a new .optik folder)");
    dlg->set_initial_folder(Gio::File::create_for_path(fs::path(m_book.folder()).parent_path().string()));
    dlg->set_initial_name(m_book.meta().title + " copy.optik");
    dlg->save(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
        Glib::RefPtr<Gio::File> f;
        try { f = dlg->save_finish(r); } catch (const Glib::Error&) { return; }
        if (!f) return;
        std::string to = f->get_path();
        if (fs::path(to).extension() != ".optik") to += ".optik";
        const std::string from = m_book.folder();
        batch_begin("Saving a copy of the book", 1, true);
        m_jobs.submit([this, from, to](ocr::Engine&) -> core::Jobs::Landing {
            auto why = std::make_shared<std::string>();
            const auto st = core::copy_book(from, to, why.get());
            return [this, st, why, to] {
                batch_tick();
                if (st != core::Status::Ok) { say("Save As failed: " + *why); return; }
                open_book(to);
                if (!m_has_book || m_book.folder() != to) return;
                const std::string name = fs::path(to).stem().string();
                if (name != m_book.meta().title)
                    step("Title " + name, [&](core::Edit& e) { e.meta().title = name; });
                save();
                say("Saved as " + to);
            };
        });
    });
}

void Shell::on_about() {
    if (!m_about) {
        auto dlg = std::make_unique<Gtk::AboutDialog>();
        dlg->set_transient_for(*this);
        dlg->set_modal(true);
        dlg->set_hide_on_close(true);
        dlg->set_program_name("Optik");
        dlg->set_version(std::string(OPTIK_BUILD) + " -- Tesseract " + ocr::Engine::version());
        dlg->set_comments("An OCR workbench for scanned books, on ABBYY FineReader's model.\n"
                          "The engine proposes; the author proofs; the book exports to ODT for Writer.");
        dlg->set_logo_icon_name("optik-logo-symbolic");
        dlg->set_authors({"Scott"});
        m_about = std::move(dlg);
    }
    m_about->present();
}

// s29 (milestone 18): Export asks first -- which pages, running heads kept or
// not, page breaks -- then where. The answers are the window's for the session
// (not the book's: no journal step); the page range starts again for another book.
void Shell::on_export() {
    if (!m_has_book) return;
    m_text.commit_now();
    if (m_export_for != m_book.folder()) {
        m_export_opts.first = 1;
        m_export_opts.last  = 0;
        m_export_for = m_book.folder();
    }
    if (!m_export_win) {
        auto win = std::make_unique<Gtk::Window>();
        win->set_title("Export");
        win->set_transient_for(*this);
        win->set_modal(true);
        win->set_hide_on_close(true);
        win->set_resizable(false);

        auto* box = Gtk::make_managed<widgets::Box>("export.box", Gtk::Orientation::VERTICAL, 8);
        box->set_margin(14);

        auto* range = Gtk::make_managed<widgets::Box>("export.range", Gtk::Orientation::HORIZONTAL, 6);
        auto* from_lbl = Gtk::make_managed<widgets::Label>("export.from.label", "Pages");
        auto* from = Gtk::make_managed<widgets::SpinButton>("export.from");
        auto* to_lbl = Gtk::make_managed<widgets::Label>("export.to.label", "to");
        auto* to = Gtk::make_managed<widgets::SpinButton>("export.to");
        auto* all = Gtk::make_managed<widgets::Button>("export.all", "All");
        auto* here = Gtk::make_managed<widgets::Button>("export.here", "This Page");
        for (auto* sb : {from, to}) { sb->set_increments(1, 10); sb->set_numeric(true); sb->set_width_chars(5); }
        all->set_tooltip_text("Every page of the book");
        here->set_tooltip_text("Only the page in view");
        range->append(*from_lbl); range->append(*from); range->append(*to_lbl); range->append(*to);
        range->append(*all); range->append(*here);

        auto* heads = Gtk::make_managed<widgets::CheckButton>("export.heads", "Keep running heads and folios");
        heads->set_tooltip_text("Each a Running Head paragraph where it stands; a paragraph that runs on "
                                "over the page turn is still joined, and the heads follow it");
        auto* br_lbl = Gtk::make_managed<widgets::Label>("export.breaks.label", "Page breaks");
        br_lbl->set_halign(Gtk::Align::START);
        auto* br_none = Gtk::make_managed<widgets::CheckButton>("export.breaks.none", "None -- the book runs on");
        auto* br_head = Gtk::make_managed<widgets::CheckButton>("export.breaks.headings", "Before each heading (chapters start a page)");
        auto* br_page = Gtk::make_managed<widgets::CheckButton>("export.breaks.pages", "At each scanned page (no joins over the page turn)");
        br_head->set_group(*br_none);
        br_page->set_group(*br_none);

        auto* buttons = Gtk::make_managed<widgets::Box>("export.buttons", Gtk::Orientation::HORIZONTAL, 6);
        buttons->set_halign(Gtk::Align::END);
        auto* cancel = Gtk::make_managed<widgets::Button>("export.cancel", "Cancel");
        auto* go = Gtk::make_managed<widgets::Button>("export.go", "Export…");
        go->add_css_class("suggested-action");
        buttons->append(*cancel);
        buttons->append(*go);

        for (Gtk::Widget* w : std::initializer_list<Gtk::Widget*>{range, heads, br_lbl, br_none, br_head, br_page, buttons})
            box->append(*w);
        win->set_child(*box);

        // The window shows m_export_opts; Export… takes them back.
        m_export_sync = [this, from, to, heads, br_none, br_head, br_page] {
            const int n = std::max<int>(1, int(m_book.pages().size()));
            from->set_range(1, n);
            to->set_range(1, n);
            const core::PageSpan sp = core::span_of(m_book, m_export_opts);
            from->set_value(sp.from + 1);
            to->set_value(sp.to + 1);
            heads->set_active(m_export_opts.keep_heads);
            br_none->set_active(m_export_opts.breaks == core::ExportOptions::NoBreaks);
            br_head->set_active(m_export_opts.breaks == core::ExportOptions::AtHeadings);
            br_page->set_active(m_export_opts.breaks == core::ExportOptions::AtPages);
        };
        // The range never turns inside out: moving one end past the other moves both.
        from->signal_value_changed().connect([from, to] {
            if (to->get_value_as_int() < from->get_value_as_int()) to->set_value(from->get_value());
        });
        to->signal_value_changed().connect([from, to] {
            if (from->get_value_as_int() > to->get_value_as_int()) from->set_value(to->get_value());
        });
        all->signal_clicked().connect([this, from, to] {
            from->set_value(1);
            to->set_value(std::max<int>(1, int(m_book.pages().size())));
        });
        here->signal_clicked().connect([this, from, to] {
            const int i = m_book.index_of(m_page_id);
            if (i < 0) return;
            to->set_value(std::max<int>(1, int(m_book.pages().size())));
            from->set_value(i + 1);
            to->set_value(i + 1);
        });
        cancel->signal_clicked().connect([this] { m_export_win->set_visible(false); });
        m_export_take = [this, from, to, heads, br_head, br_page] {
            const int n = int(m_book.pages().size());
            m_export_opts.first = from->get_value_as_int();
            m_export_opts.last  = to->get_value_as_int() >= n ? 0 : to->get_value_as_int();
            m_export_opts.keep_heads = heads->get_active();
            m_export_opts.breaks = br_page->get_active() ? core::ExportOptions::AtPages
                                 : br_head->get_active() ? core::ExportOptions::AtHeadings
                                                         : core::ExportOptions::NoBreaks;
            m_export_win->set_visible(false);
        };
        go->signal_clicked().connect([this] {
            m_export_take();

            auto dlg = Gtk::FileDialog::create();
            dlg->set_title("Export the book (.odt for Writer, or .txt)");
            dlg->set_initial_folder(Gio::File::create_for_path(fs::path(m_book.folder()).parent_path().string()));
            dlg->set_initial_name(m_book.meta().title + ".odt");
            dlg->save(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
                Glib::RefPtr<Gio::File> f;
                try { f = dlg->save_finish(r); } catch (const Glib::Error&) { return; }
                if (f) export_to(f->get_path());
            });
        });
        m_export_win = std::move(win);
    }
    m_export_sync();
    m_export_win->present();
}

void Shell::export_to(const std::string& where) {
    std::string path = where;
    const auto ext = fs::path(path).extension().string();
    const auto blocks = core::flow(m_book, m_export_opts);
    const core::PageSpan sp = core::span_of(m_book, m_export_opts);
    std::string why;
    bool ok;
    if (ext == ".txt") {
        ok = core::write_text(blocks, path, &why);
    } else {
        if (ext != ".odt") path += ".odt";
        ok = core::write_odt(blocks, m_book.meta().title, path, &why);
    }
    const bool whole = sp.from == 0 && sp.to + 1 == int(m_book.pages().size());
    const std::string pages = whole ? "" : sp.from == sp.to
        ? " (page " + std::to_string(sp.from + 1) + ")"
        : " (pages " + std::to_string(sp.from + 1) + "-" + std::to_string(sp.to + 1) + ")";
    say(ok ? "Exported " + std::to_string(blocks.size()) + " paragraphs" + pages + " to " + path
           : "Export failed: " + why);
}

// -- the Book popover: FineReader's options, per book, each one a step ------------------

void Shell::build_settings_popover() {
    auto* box = Gtk::make_managed<widgets::Box>("settings.box", Gtk::Orientation::VERTICAL, 6);
    box->set_margin(10);

    auto* lang_row = Gtk::make_managed<widgets::Box>("settings.lang.row", Gtk::Orientation::HORIZONTAL, 6);
    auto* lang_lbl = Gtk::make_managed<widgets::Label>("settings.lang.label", "Reads in");
    auto* lang = Gtk::make_managed<widgets::Entry>("settings.lang");
    lang->set_tooltip_text("Tesseract's codes, joined with + (eng+fra); the first is the book's own, "
                           "and its dictionary checks the spelling. Ticking below does the same");
    lang->set_width_chars(14);
    lang_row->append(*lang_lbl);
    lang_row->append(*lang);

    // s30 (milestone 19): the installed languages, ticked to read in. Rebuilt
    // when what is installed or what the book reads in changes.
    auto* langs = Gtk::make_managed<widgets::Box>("settings.langs", Gtk::Orientation::VERTICAL, 2);
    auto* langs_scroll = Gtk::make_managed<widgets::ScrolledWindow>("settings.langs.scroll");
    langs_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    langs_scroll->set_max_content_height(170);
    langs_scroll->set_propagate_natural_height(true);
    langs_scroll->set_child(*langs);
    auto* langs_hint = Gtk::make_managed<widgets::Label>("settings.langs.hint",
        "More languages: tesseract-langpack-<code>; spelling: hunspell-<xx> (Fedora)");
    langs_hint->add_css_class("dim-label");
    langs_hint->set_halign(Gtk::Align::START);
    auto langs_key = std::make_shared<std::string>();
    auto langs_rebuild = std::make_shared<std::function<void()>>();

    auto* thr_row = Gtk::make_managed<widgets::Box>("settings.thr.row", Gtk::Orientation::HORIZONTAL, 6);
    auto* thr_lbl = Gtk::make_managed<widgets::Label>("settings.thr.label", "Check words under");
    auto* thr = Gtk::make_managed<widgets::SpinButton>("settings.thr");
    thr->set_range(0, 100);
    thr->set_increments(5, 10);
    thr->set_tooltip_text("A word read with less confidence than this is marked to check");
    auto* thr_pct = Gtk::make_managed<widgets::Label>("settings.thr.pct", "% confidence");
    thr_row->append(*thr_lbl);
    thr_row->append(*thr);
    thr_row->append(*thr_pct);

    auto* split = Gtk::make_managed<widgets::CheckButton>("settings.split", "Split two-page scans on import");
    auto* heads = Gtk::make_managed<widgets::CheckButton>("settings.heads", "Mark running heads and folios when reading");
    auto* join  = Gtk::make_managed<widgets::CheckButton>("settings.join", "Join paragraphs across the page turn on export");
    auto* hyph  = Gtk::make_managed<widgets::CheckButton>("settings.hyph", "Mend words hyphenated at line ends on export");
    auto* heads2 = Gtk::make_managed<widgets::CheckButton>("settings.headings", "Mark headings (short, centred, space below) when reading");
    auto* quotes = Gtk::make_managed<widgets::CheckButton>("settings.quotes", "Mend quotes read as two marks (``Stop -> \u201CStop) when reading");
    auto* caps   = Gtk::make_managed<widgets::CheckButton>("settings.smallcaps", "Small caps: capital headings, check chapter openers when reading");
    auto* ital  = Gtk::make_managed<widgets::CheckButton>("settings.italic", "Italic: measure the lean, check italic words when reading");
    auto* spell = Gtk::make_managed<widgets::CheckButton>("settings.spell", "Spelling: check words the dictionary does not know when reading");
    // Condition, phase 0 (s29): the page before the engine, and on the screen.
    auto* deskew = Gtk::make_managed<widgets::CheckButton>("settings.deskew", "Deskew: measure the page's tilt and read it straightened");
    auto* decol  = Gtk::make_managed<widgets::CheckButton>("settings.decolour", "Decolour: show and read the page in grey");
    auto* level  = Gtk::make_managed<widgets::CheckButton>("settings.level", "Level: even the paper to white and the ink to black");
    deskew->set_tooltip_text("Takes effect on the next read of a page (the page turns with its words)");
    decol->set_tooltip_text("The page picture changes now; the engine reads it so on the next read");
    level->set_tooltip_text("The page picture changes now; the engine reads it so on the next read");
    ital->set_tooltip_text("Takes effect on the next read of a page. Ctrl+I sets or clears italic on a word");
    heads2->set_tooltip_text("Takes effect on the next read of a page");
    quotes->set_tooltip_text("Takes effect on the next read of a page");
    caps->set_tooltip_text("Takes effect on the next read of a page");

    for (Gtk::Widget* w : std::initializer_list<Gtk::Widget*>{lang_row, langs_scroll, langs_hint, thr_row, split, deskew, decol, level, heads, heads2, quotes, caps, ital, spell, join, hyph})
        box->append(*w);
    m_settings_pop.set_child(*box);
    m_settings_pop.set_parent(m_title);   // ☰ -> Book Settings… points it at the title

    bool syncing = false;
    auto quiet = std::make_shared<bool>(false);
    m_settings_sync = [=, this] {
        *quiet = true;
        const auto& s = m_book.meta().settings;
        lang->set_text(s.language);
        if (*langs_rebuild) (*langs_rebuild)();
        thr->set_value(s.uncertain_below);
        split->set_active(s.split_spreads);
        heads->set_active(s.mark_running_heads);
        join->set_active(s.join_pages);
        hyph->set_active(s.dehyphenate);
        heads2->set_active(s.mark_headings);
        quotes->set_active(s.mend_quotes);
        caps->set_active(s.small_caps);
        ital->set_active(s.mark_italic);
        spell->set_active(s.check_spelling);
        deskew->set_active(s.deskew);
        decol->set_active(s.decolour);
        level->set_active(s.level);
        // No dictionary for the book's language: say which package, keep the box.
        const std::string dic = core::dictionary_for(s.language);
        spell->set_tooltip_text(dic.empty()
            ? "No spelling dictionary is installed for '" + s.language +
              "' -- install hunspell-en-US (or your language's), then read again"
            : "Takes effect on the next read of a page. Dictionary: " + dic +
              ". Ctrl+Shift+Enter adds a word to the book's own words");
        *quiet = false;
    };
    (void)syncing;
    auto change = [this, quiet](const std::string& label, std::function<void(core::Settings&)> fn) {
        if (*quiet || !m_has_book) return;
        step(label, [&](core::Edit& e) { fn(e.meta().settings); });
        // The threshold changes every page's count.
        if (label == "Check threshold") {
            for (const auto& p : m_book.pages()) m_pages.refresh(m_book, p.id);
            refresh_page(m_page_id);
        }
        // The tone changes how every page looks (s29): new pictures and thumbs.
        if (label == "Decolour" || label == "Level") refresh_page(m_page_id);
        // s30: the list, the codes and the dictionary say the new languages --
        // on idle: a tick's own box is rebuilt, and must not go in its handler.
        if (label.rfind("Language ", 0) == 0)
            Glib::signal_idle().connect_once([this] { if (m_settings_sync) m_settings_sync(); });
    };
    *langs_rebuild = [=, this] {
        const std::string setting = m_book.meta().settings.language;
        const std::vector<std::string> installed = core::installed_codes();
        std::string key = setting + "|";
        for (const auto& c : installed) key += c + ",";
        if (key == *langs_key) return;
        *langs_key = key;
        while (Gtk::Widget* c = langs->get_first_child()) langs->remove(*c);
        const std::vector<std::string> reading = core::split_languages(setting);
        for (const core::Language& l : core::languages_for(setting, installed)) {
            std::string text = l.name + "  (" + l.code + ")";
            if (!l.installed) text += "  -- not installed here";
            else if (l.dictionary.empty()) text += "  -- no spelling dictionary";
            auto* c = Gtk::make_managed<widgets::CheckButton>("settings.lang." + l.code, text);
            const bool on = std::find(reading.begin(), reading.end(), l.code) != reading.end();
            c->set_active(on);
            if (on && !reading.empty() && reading[0] == l.code)
                c->set_tooltip_text("The book's own language: its dictionary checks the spelling");
            else if (!l.dictionary.empty())
                c->set_tooltip_text("Spelling dictionary: " + l.dictionary);
            const std::string code = l.code;
            c->signal_toggled().connect([=, this] {
                if (*quiet || !m_has_book) return;
                const std::string was = m_book.meta().settings.language;
                const std::string now = core::with_language(was, code, c->get_active());
                if (now == was) {          // the last one: a book reads in something
                    *quiet = true;
                    c->set_active(true);
                    *quiet = false;
                    say("A book reads in at least one language.");
                    return;
                }
                change("Language " + now, [now](core::Settings& s) { s.language = now; });
            });
            langs->append(*c);
        }
    };
    lang->signal_activate().connect([=] {
        const std::string v = lang->get_text();
        if (!v.empty()) change("Language " + v, [v](core::Settings& s) { s.language = v; });
    });
    thr->signal_value_changed().connect([=] {
        const int v = thr->get_value_as_int();
        change("Check threshold", [v](core::Settings& s) { s.uncertain_below = v; });
    });
    split->signal_toggled().connect([=] {
        const bool v = split->get_active();
        change("Split on import", [v](core::Settings& s) { s.split_spreads = v; });
    });
    heads->signal_toggled().connect([=] {
        const bool v = heads->get_active();
        change("Running heads", [v](core::Settings& s) { s.mark_running_heads = v; });
    });
    join->signal_toggled().connect([=] {
        const bool v = join->get_active();
        change("Join across pages", [v](core::Settings& s) { s.join_pages = v; });
    });
    hyph->signal_toggled().connect([=] {
        const bool v = hyph->get_active();
        change("Mend hyphens", [v](core::Settings& s) { s.dehyphenate = v; });
    });
    heads2->signal_toggled().connect([=] {
        const bool v = heads2->get_active();
        change("Headings", [v](core::Settings& s) { s.mark_headings = v; });
    });
    quotes->signal_toggled().connect([=] {
        const bool v = quotes->get_active();
        change("Mend quotes", [v](core::Settings& s) { s.mend_quotes = v; });
    });
    caps->signal_toggled().connect([=] {
        const bool v = caps->get_active();
        change("Small caps", [v](core::Settings& s) { s.small_caps = v; });
    });
    ital->signal_toggled().connect([=] {
        const bool v = ital->get_active();
        change("Italic", [v](core::Settings& s) { s.mark_italic = v; });
    });
    deskew->signal_toggled().connect([=] {
        const bool v = deskew->get_active();
        change("Deskew", [v](core::Settings& s) { s.deskew = v; });
    });
    decol->signal_toggled().connect([=] {
        const bool v = decol->get_active();
        change("Decolour", [v](core::Settings& s) { s.decolour = v; });
    });
    level->signal_toggled().connect([=] {
        const bool v = level->get_active();
        change("Level", [v](core::Settings& s) { s.level = v; });
    });
    spell->signal_toggled().connect([=] {
        const bool v = spell->get_active();
        change("Spelling", [v](core::Settings& s) { s.check_spelling = v; });
    });
}

void Shell::sync_settings_popover() { if (m_settings_sync) m_settings_sync(); }

}  // namespace optik
