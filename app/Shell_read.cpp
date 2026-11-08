// Shell_read.cpp -- reading: the pool, pictures, read page / all / area.
//
// Every worker job carries only plain values out (a ReadRequest, a path and a
// rect) and brings plain values back; its landing runs on the UI thread and is
// where the book changes -- one journal step per page read.
#include "Shell.hpp"

#include "Log.hpp"
#include "core/PageOps.hpp"
#include "core/Picture.hpp"
#include "core/Reading.hpp"

#include <glibmm/main.h>

namespace optik {

namespace {
constexpr int kViewSide = 2400;        // page pictures: plenty for a screen, not a 600 dpi master
constexpr std::size_t kKeepPictures = 8;
}  // namespace

void Shell::batch_begin(const std::string& label, int total, bool dialog) {
    if (m_batch_total > 0 && m_batch_done < m_batch_total) {
        m_batch_total += total;       // a second batch joins the first
        m_batch_dialog = m_batch_dialog || dialog;
    } else {
        m_batch_label = label;
        m_batch_total = total;
        m_batch_done = 0;
        m_batch_dialog = dialog;
    }
    refresh_status();
}

// The progress window (Scott, s25: "it could take time ... so the user knows
// that something is happening"). For the long jobs only -- Read All, Rescan,
// Import, Save As; one page reads in the status line. NOT modal: the UI never
// waits (RULES), so the author can go on proofing while the book reads. Hide
// leaves the status line's bar; Cancel is the status line's Cancel.
void Shell::refresh_progress_dialog() {
    const bool show = m_batch_total > 0 && m_batch_dialog;
    if (!show) {
        if (m_pdlg) m_pdlg->set_visible(false);
        m_pdlg_pulse.disconnect();
        return;
    }
    if (!m_pdlg) {
        m_pdlg = std::make_unique<Gtk::Window>();
        m_pdlg->set_name("progress");
        m_pdlg->set_transient_for(*this);
        m_pdlg->set_modal(false);
        m_pdlg->set_resizable(false);
        m_pdlg->set_default_size(440, -1);
        m_pdlg->set_hide_on_close(true);
        m_pdlg->signal_close_request().connect([this] { m_batch_dialog = false; return false; }, false);
        auto* box = Gtk::make_managed<widgets::Box>("progress.box", Gtk::Orientation::VERTICAL, 10);
        box->set_margin(18);
        m_pdlg_what.set_xalign(0);
        m_pdlg_what.add_css_class("heading");
        m_pdlg_count.set_xalign(0);
        m_pdlg_count.add_css_class("dim-label");
        m_pdlg_bar.set_hexpand(true);
        auto* row = Gtk::make_managed<widgets::Box>("progress.buttons", Gtk::Orientation::HORIZONTAL, 8);
        row->set_halign(Gtk::Align::END);
        auto* hide = Gtk::make_managed<widgets::Button>("progress.hide", "Hide");
        hide->set_tooltip_text("Keep working; the status line still shows the progress");
        hide->signal_clicked().connect([this] { m_batch_dialog = false; refresh_progress_dialog(); });
        auto* cancel = Gtk::make_managed<widgets::Button>("progress.cancel", "Cancel");
        // Its own toplevel: "win." actions do not reach it, so call the verb.
        cancel->signal_clicked().connect([this] { on_cancel(); });
        row->append(*hide);
        row->append(*cancel);
        box->append(m_pdlg_what);
        box->append(m_pdlg_bar);
        box->append(m_pdlg_count);
        box->append(*row);
        m_pdlg->set_child(*box);
    }
    m_pdlg->set_title(m_batch_label);
    m_pdlg_what.set_text(m_batch_label + "…");
    if (m_zip_progress) {   // s33b: a zip coming in -- copy, unpack, import
        const int ph = m_zip_progress->phase.load();
        const auto done = m_zip_progress->done.load(), total = m_zip_progress->total.load();
        auto mb = [](std::uint64_t b) { return std::to_string((b + 524288) / 1048576); };
        if ((ph == 1 || ph == 2) && total > 0) {
            m_pdlg_pulse.disconnect();
            m_pdlg_bar.set_fraction(double(done) / double(total));
            m_pdlg_count.set_text(ph == 1 ? "Copying from the library: " + mb(done) + " of " + mb(total) + " MB"
                                          : "Unpacking: scan " + std::to_string(done) + " of " + std::to_string(total));
        } else {
            if (!m_pdlg_pulse.connected())
                m_pdlg_pulse = Glib::signal_timeout().connect([this] { m_pdlg_bar.pulse(); return true; }, 120);
            m_pdlg_count.set_text(ph <= 1 ? "Opening the zip on the library…" : "Putting the scans into the book…");
        }
        if (!m_pdlg->get_visible()) m_pdlg->present();
        return;
    }
    if (m_batch_total > 1) {
        m_pdlg_pulse.disconnect();
        m_pdlg_bar.set_fraction(double(m_batch_done) / m_batch_total);
        m_pdlg_count.set_text(std::to_string(m_batch_done) + " of " + std::to_string(m_batch_total) +
                              " pages  ·  you can go on working");
    } else {
        // One long step (import, a copy): no count to show, so the bar moves.
        if (!m_pdlg_pulse.connected())
            m_pdlg_pulse = Glib::signal_timeout().connect([this] { m_pdlg_bar.pulse(); return true; }, 120);
        m_pdlg_count.set_text("This can take a while for a big book.");
    }
    if (!m_pdlg->get_visible()) m_pdlg->present();
}

void Shell::batch_tick() {
    if (m_batch_total == 0) return;
    if (++m_batch_done >= m_batch_total) {
        m_batch_total = m_batch_done = 0;
        if (m_has_book && m_journal.dirty()) save();   // a long read is worth keeping at once
    }
    refresh_status();
}

void Shell::on_cancel() {
    m_jobs.cancel();
    if (m_export_stop) m_export_stop->store(true);   // s31: an export writes nothing
    if (m_import_stop) m_import_stop->store(true);   // s33: a zip stops unpacking
    if (m_import_cancel) m_import_cancel->cancel();  // s33b: and stops copying
    zip_done();
    const int done = m_batch_done, total = m_batch_total;
    m_batch_total = m_batch_done = 0;
    m_pictures_pending.clear();
    m_thumbs_pending.clear();
    refresh_status();
    if (total) say("Cancelled after " + std::to_string(done) + " of " + std::to_string(total) + ".");
}

void Shell::read_pages(const std::vector<std::string>& ids, const std::string& label, bool dialog) {
    if (ids.empty()) return;
    batch_begin(label, int(ids.size()), dialog);
    for (const auto& id : ids) {
        const core::Page* p = m_book.page(id);
        if (!p) { batch_tick(); continue; }
        const core::ReadRequest req = core::request_for(m_book, *p);
        m_jobs.submit([this, req](ocr::Engine& eng) -> core::Jobs::Landing {
            auto reading = std::make_shared<core::Reading>(core::read(eng, req));
            return [this, reading] {
                // The tick comes LAST: the batch's end saves the book, and the
                // page that ends it must be in it.
                if (!reading->ok) {
                    say("Could not read a page: " + reading->why);
                    if (auto lg = log::get(log::Area::Read)) lg->warn("read {}: {}", reading->page_id, reading->why);
                } else if (const int i = m_book.index_of(reading->page_id); i >= 0) {
                    step("Read page " + std::to_string(i + 1),
                         [&](core::Edit& e) { core::land(e, *reading); });
                }   // else: undone away while it was being read
                batch_tick();
            };
        });
    }
}

void Shell::read_area(int area, const core::Rect& box, bool new_area) {
    const core::Page* p = current();
    if (!p) return;
    const core::ReadRequest req = core::request_for_area(m_book, *p, area, box);
    batch_begin("Reading the area", 1);
    m_jobs.submit([this, req, new_area](ocr::Engine& eng) -> core::Jobs::Landing {
        auto reading = std::make_shared<core::Reading>(core::read(eng, req));
        return [this, reading, new_area] {
            struct Tick { Shell* s; ~Tick() { s->batch_tick(); } } tick{this};
            if (!reading->ok) { say("Could not read the area: " + reading->why); return; }
            if (!m_book.page(reading->page_id)) return;
            // A drawn area joins the step that reads it, so one undo takes both.
            bool landed = false;
            step(new_area ? "Draw area" : "Read area again", [&](core::Edit& e) {
                core::Page& pg = e.page(reading->page_id);
                core::Reading r = *reading;
                if (new_area) {
                    // Appended now: its index is where it lands, whatever was
                    // added or deleted while it was read.
                    ocr::Area a;
                    a.kind = ocr::Kind::Text;
                    a.box = r.area_box;
                    a.user = true;
                    pg.areas.push_back(a);
                    pg.read = true;
                    r.area = int(pg.areas.size()) - 1;
                    r.area_was = r.area_box;
                }
                landed = core::land(e, r);
            });
            if (!landed) say("The area changed while it was being read; its reading was dropped.");
            else if (reading->paras.empty()) say("Nothing readable in that area.");
        };
    });
}

// Pictures are cached by how the page LOOKS (core::look_key: id, crop, turn; s29: skew, tone),
// so a re-cut or turned page -- or the undo of one -- is drawn afresh (s26).
void Shell::request_picture(const std::string& id) {
    const core::Page* p = m_book.page(id);
    if (!p) return;
    const std::string key = core::look_key(m_book, *p);
    if (m_pictures.count(key) || m_pictures_pending.count(key)) return;
    m_pictures_pending.insert(key);
    const std::string path = m_book.scan_path(p->scan_id);
    const ocr::Look look = core::look_of(m_book, *p);
    m_jobs.submit([this, key, path, look](ocr::Engine&) -> core::Jobs::Landing {
        auto pic = std::make_shared<core::Picture>();
        core::load_picture(path, look, kViewSide, *pic);
        return [this, key, pic] {
            m_pictures_pending.erase(key);
            if (!pic->valid()) { say("Could not show the scan for this page."); return; }
            m_pictures[key] = pic;
            m_picture_order.push_back(key);
            const core::Page* cur = current();
            const std::string shown = cur ? core::look_key(m_book, *cur) : std::string();
            while (m_picture_order.size() > kKeepPictures) {
                const std::string old = m_picture_order.front();
                m_picture_order.erase(m_picture_order.begin());
                if (old != shown) m_pictures.erase(old);
            }
            if (key == shown) m_view.set_picture(key, pic);
        };
    });
}

// The page's picture into the view: from the cache, or asked for.
void Shell::show_picture() {
    const core::Page* p = current();
    if (!p) return;
    auto it = m_pictures.find(core::look_key(m_book, *p));
    if (it != m_pictures.end()) m_view.set_picture(it->first, it->second);
    else request_picture(p->id);
}

void Shell::request_thumbs() {
    for (const auto& p : m_book.pages()) {
        const std::string key = core::look_key(m_book, p);
        if (m_pages.has_thumb(p.id, key) || m_thumbs_pending.count(key)) continue;
        m_thumbs_pending.insert(key);
        const std::string id = p.id, path = m_book.scan_path(p.scan_id);
        const ocr::Look look = core::look_of(m_book, p);
        m_jobs.submit([this, id, key, path, look](ocr::Engine&) -> core::Jobs::Landing {
            auto pic = std::make_shared<core::Picture>();
            core::load_picture(path, look, PagesPanel::kThumbSide * 2, *pic);
            return [this, id, key, pic] {
                m_thumbs_pending.erase(key);
                m_pages.set_thumb(id, key, *pic);
            };
        });
    }
}

}  // namespace optik
