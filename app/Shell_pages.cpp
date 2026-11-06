// Shell_pages.cpp -- the page list (s26, milestone 12): move, delete, turn, re-cut.
//
// Each verb is one journal step through core/PageOps; turning and re-cutting
// clear the pages' readings in that step and then read them again (a reading
// asked before lands only on a page that still looks as it did -- core::land).
#include "Shell.hpp"

#include "RecutWindow.hpp"
#include "core/PageOps.hpp"
#include "core/Picture.hpp"

namespace optik {

namespace {
constexpr int kRecutSide = 1600;   // the whole scan, for choosing a cut
}

void Shell::page_move(int delta) {
    const core::Page* p = current();
    if (!p) return;
    const std::string id = p->id;
    if (step(delta < 0 ? "Move page up" : "Move page down",
             [&](core::Edit& e) { core::move_page_by(e, id, delta); }))
        go_to(m_book.index_of(id));
}

void Shell::page_delete() {
    const core::Page* p = current();
    if (!p) return;
    m_text.commit_now();
    const std::string id = p->id;
    const int i = m_book.index_of(id);
    if (!step("Delete page " + std::to_string(i + 1), [&](core::Edit& e) { core::delete_page(e, id); })) return;
    go_to(std::min(i, int(m_book.pages().size()) - 1));
    say("Deleted page " + std::to_string(i + 1) + "; its scan stays in the book. Ctrl+Z brings it back.");
}

void Shell::page_rotate(int quarter_turns) {
    const core::Page* p = current();
    if (!p) return;
    m_text.commit_now();
    const std::string id = p->id;
    if (!step(quarter_turns > 0 ? "Turn page clockwise" : "Turn page anticlockwise",
              [&](core::Edit& e) { core::rotate_page(e, id, quarter_turns); }))
        return;
    read_pages({id}, "Reading the turned page");
}

void Shell::on_recut() {
    const core::Page* p = current();
    if (!p) return;
    m_text.commit_now();
    const std::string scan_id = p->scan_id;
    const auto ids = core::pages_of_scan(m_book, scan_id);
    bool two = false;
    int cut = 0;
    for (const auto& id : ids) {
        const core::Page* q = m_book.page(id);
        if (q->half == model::split::Half::Left) { two = true; cut = q->crop.x1; }
        if (q->half == model::split::Half::Right) { two = true; if (!cut) cut = q->crop.x0; }
    }
    if (!m_recut) {
        m_recut = std::make_unique<RecutWindow>();
        m_recut->set_transient_for(*this);
        m_recut->signal_recut.connect(sigc::mem_fun(*this, &Shell::recut_scan));
    }
    const core::Scan* s = m_book.scan(scan_id);
    m_recut->open_for(scan_id, s ? s->source_name : scan_id, two, cut);
    const std::string path = m_book.scan_path(scan_id);
    m_jobs.submit([this, scan_id, path](ocr::Engine&) -> core::Jobs::Landing {
        auto pic = std::make_shared<core::Picture>();
        core::load_picture(path, {}, 0, kRecutSide, *pic);
        return [this, scan_id, pic] {
            if (!pic->valid()) { say("Could not show the scan."); return; }
            if (m_recut) m_recut->set_picture(scan_id, pic);
        };
    });
}

void Shell::recut_scan(std::string scan_id, core::Rect full, int cut) {
    std::vector<std::string> changed;
    if (!step(cut ? "Re-cut the scan" : "Make the scan one page",
              [&](core::Edit& e) { changed = core::recut_scan(e, scan_id, full, cut); })) {
        say("The scan is already cut that way.");
        return;
    }
    const auto ids = core::pages_of_scan(m_book, scan_id);
    if (!ids.empty()) go_to(m_book.index_of(ids[0]));
    if (!changed.empty()) read_pages(changed, "Reading the re-cut pages");
}

}  // namespace optik
