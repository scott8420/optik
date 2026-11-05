// Shell_read.cpp -- reading: the pool, pictures, read page / all / area.
//
// Every worker job carries only plain values out (a ReadRequest, a path and a
// rect) and brings plain values back; its landing runs on the UI thread and is
// where the book changes -- one journal step per page read.
#include "Shell.hpp"

#include "Log.hpp"
#include "core/Picture.hpp"
#include "core/Reading.hpp"

namespace optik {

namespace {
constexpr int kViewSide = 2400;        // page pictures: plenty for a screen, not a 600 dpi master
constexpr std::size_t kKeepPictures = 8;
}  // namespace

void Shell::batch_begin(const std::string& label, int total) {
    if (m_batch_total > 0 && m_batch_done < m_batch_total) {
        m_batch_total += total;       // a second batch joins the first
    } else {
        m_batch_label = label;
        m_batch_total = total;
        m_batch_done = 0;
    }
    refresh_status();
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
    const int done = m_batch_done, total = m_batch_total;
    m_batch_total = m_batch_done = 0;
    m_pictures_pending.clear();
    m_thumbs_pending.clear();
    refresh_status();
    if (total) say("Cancelled after " + std::to_string(done) + " of " + std::to_string(total) + ".");
}

void Shell::read_pages(const std::vector<std::string>& ids, const std::string& label) {
    if (ids.empty()) return;
    batch_begin(label, int(ids.size()));
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
            step(new_area ? "Draw area" : "Read area again", [&](core::Edit& e) {
                core::Page& pg = e.page(reading->page_id);
                if (new_area) {
                    ocr::Area a;
                    a.kind = ocr::Kind::Text;
                    a.box = reading->area_box;
                    a.user = true;
                    pg.areas.push_back(a);
                    pg.read = true;
                }
                core::land(e, *reading);
            });
            if (reading->paras.empty()) say("Nothing readable in that area.");
        };
    });
}

void Shell::request_picture(const std::string& id) {
    const core::Page* p = m_book.page(id);
    if (!p || m_pictures_pending.count(id)) return;
    m_pictures_pending.insert(id);
    const std::string path = m_book.scan_path(p->scan_id);
    const core::Rect crop = p->crop;
    m_jobs.submit([this, id, path, crop](ocr::Engine&) -> core::Jobs::Landing {
        auto pic = std::make_shared<core::Picture>();
        core::load_picture(path, crop, kViewSide, *pic);
        return [this, id, pic] {
            m_pictures_pending.erase(id);
            if (!pic->valid()) { say("Could not show the scan for this page."); return; }
            m_pictures[id] = pic;
            m_picture_order.push_back(id);
            while (m_picture_order.size() > kKeepPictures) {
                const std::string old = m_picture_order.front();
                m_picture_order.erase(m_picture_order.begin());
                if (old != m_page_id) m_pictures.erase(old);
            }
            if (id == m_page_id) m_view.set_picture(id, pic);
        };
    });
}

void Shell::request_thumbs() {
    for (const auto& p : m_book.pages()) {
        if (m_pages.has_thumb(p.id) || m_thumbs_pending.count(p.id)) continue;
        m_thumbs_pending.insert(p.id);
        const std::string id = p.id, path = m_book.scan_path(p.scan_id);
        const core::Rect crop = p.crop;
        m_jobs.submit([this, id, path, crop](ocr::Engine&) -> core::Jobs::Landing {
            auto pic = std::make_shared<core::Picture>();
            core::load_picture(path, crop, PagesPanel::kThumbSide * 2, *pic);
            return [this, id, pic] {
                m_thumbs_pending.erase(id);
                m_pages.set_thumb(id, *pic);
            };
        });
    }
}

}  // namespace optik
