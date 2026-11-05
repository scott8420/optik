// Shell_files.cpp -- files: open, new from scans, save, export, book settings.
#include "Shell.hpp"

#include "Log.hpp"
#include "core/Export.hpp"
#include "model/Intake.hpp"

#include <giomm/file.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

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
    if (st != core::Status::Ok) { say(std::string("Could not open: ") + core::status_text(st)); return; }
    m_book = std::move(b);
    m_has_book = true;
    m_journal.clear();
    m_journal.mark();
    m_pictures.clear();
    m_picture_order.clear();
    m_pictures_pending.clear();
    m_thumbs_pending.clear();
    m_page_id.clear();
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
    batch_begin("Importing", 1);
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

void Shell::on_export() {
    if (!m_has_book) return;
    m_text.commit_now();
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("Export the book (.odt for Writer, or .txt)");
    dlg->set_initial_folder(Gio::File::create_for_path(fs::path(m_book.folder()).parent_path().string()));
    dlg->set_initial_name(m_book.meta().title + ".odt");
    dlg->save(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
        Glib::RefPtr<Gio::File> f;
        try { f = dlg->save_finish(r); } catch (const Glib::Error&) { return; }
        if (!f) return;
        std::string path = f->get_path();
        const auto ext = fs::path(path).extension().string();
        const auto blocks = core::flow(m_book);
        std::string why;
        bool ok;
        if (ext == ".txt") {
            ok = core::write_text(blocks, path, &why);
        } else {
            if (ext != ".odt") path += ".odt";
            ok = core::write_odt(blocks, m_book.meta().title, path, &why);
        }
        say(ok ? "Exported " + std::to_string(blocks.size()) + " paragraphs to " + path
               : "Export failed: " + why);
    });
}

// -- the Book popover: FineReader's options, per book, each one a step ------------------

void Shell::build_settings_popover() {
    auto* box = Gtk::make_managed<widgets::Box>("settings.box", Gtk::Orientation::VERTICAL, 6);
    box->set_margin(10);

    auto* lang_row = Gtk::make_managed<widgets::Box>("settings.lang.row", Gtk::Orientation::HORIZONTAL, 6);
    auto* lang_lbl = Gtk::make_managed<widgets::Label>("settings.lang.label", "Language");
    auto* lang = Gtk::make_managed<widgets::Entry>("settings.lang");
    lang->set_tooltip_text("Tesseract language codes: eng, fra, deu, eng+fra ...");
    lang->set_width_chars(10);
    lang_row->append(*lang_lbl);
    lang_row->append(*lang);

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

    for (Gtk::Widget* w : std::initializer_list<Gtk::Widget*>{lang_row, thr_row, split, heads, join, hyph})
        box->append(*w);
    m_settings_pop.set_child(*box);
    m_settings_btn.set_popover(m_settings_pop);

    bool syncing = false;
    auto quiet = std::make_shared<bool>(false);
    m_settings_sync = [=, this] {
        *quiet = true;
        const auto& s = m_book.meta().settings;
        lang->set_text(s.language);
        thr->set_value(s.uncertain_below);
        split->set_active(s.split_spreads);
        heads->set_active(s.mark_running_heads);
        join->set_active(s.join_pages);
        hyph->set_active(s.dehyphenate);
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
}

void Shell::sync_settings_popover() { if (m_settings_sync) m_settings_sync(); }

}  // namespace optik
