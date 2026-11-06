#include "PageOps.hpp"

#include <algorithm>

namespace optik::core {

namespace {
void clear_reading(Page& p) {
    p.areas.clear();
    p.read = false;
}
}  // namespace

bool move_page_by(Edit& e, const std::string& id, int delta) {
    const int from = e.book().index_of(id);
    if (from < 0 || delta == 0) return false;
    const int n = int(e.book().pages().size());
    const int to = std::clamp(from + delta, 0, n - 1);
    if (to == from) return false;
    e.move_page(id, std::size_t(to));
    return true;
}

bool delete_page(Edit& e, const std::string& id) {
    if (!e.book().page(id)) return false;
    e.remove_page(id);
    return true;
}

bool rotate_page(Edit& e, const std::string& id, int quarter_turns) {
    if (!e.book().page(id) || (quarter_turns & 3) == 0) return false;
    Page& p = e.page(id);
    p.turns = (p.turns + quarter_turns) & 3;
    clear_reading(p);
    return true;
}

std::vector<std::string> pages_of_scan(const Book& b, const std::string& scan_id) {
    std::vector<std::string> v;
    for (const auto& p : b.pages()) if (p.scan_id == scan_id) v.push_back(p.id);
    return v;
}

std::vector<std::string> recut_scan(Edit& e, const std::string& scan_id, const Rect& full, int cut) {
    using model::split::Half;
    const Book& b = e.book();
    if (!b.scan(scan_id) || full.empty()) return {};
    if (cut != 0 && (cut <= full.x0 || cut >= full.x1)) return {};
    std::vector<std::string> have = pages_of_scan(b, scan_id);

    // What the scan should be.
    struct Want { Half half; Rect crop; };
    std::vector<Want> want;
    if (cut == 0) {
        want.push_back({Half::Whole, full});
    } else {
        want.push_back({Half::Left,  Rect{full.x0, full.y0, cut, full.y1}});
        want.push_back({Half::Right, Rect{cut, full.y0, full.x1, full.y1}});
    }

    // Where its pages go: where the first of them is. A scan with no pages left
    // goes back after the last page of a scan named before it -- the natural
    // order import used (the book's scan LIST is not in page order).
    std::size_t at = 0;
    if (!have.empty()) {
        at = std::size_t(b.index_of(have[0]));
    } else {
        const std::string& name = b.scan(scan_id)->source_name;
        for (std::size_t i = 0; i < b.pages().size(); ++i) {
            const Scan* s = b.scan(b.pages()[i].scan_id);
            if (s && model::split::natural_less(s->source_name, name)) at = i + 1;
        }
    }

    // Nothing to do: the pages are already these.
    if (have.size() == want.size()) {
        bool same = true;
        for (std::size_t i = 0; i < want.size(); ++i) {
            const Page* p = b.page(have[i]);
            same = same && p->half == want[i].half && p->crop == want[i].crop;
        }
        if (same) return {};
    }

    std::vector<std::string> changed;
    int turns = have.empty() ? 0 : b.page(have[0])->turns;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (i < have.size()) {
            Page& p = e.page(have[i]);
            const bool moved = !(p.crop == want[i].crop);
            p.half = want[i].half;
            p.crop = want[i].crop;
            if (moved) { clear_reading(p); changed.push_back(p.id); }
            // Keep them together, in order, from where the first was.
            e.move_page(p.id, std::min(at + i, e.book().pages().size() - 1));
            turns = p.turns;
        } else {
            Page p;
            p.scan_id = scan_id;
            p.half = want[i].half;
            p.crop = want[i].crop;
            p.turns = turns;
            changed.push_back(e.insert_page(std::min(at + i, e.book().pages().size()), p).id);
        }
    }
    for (std::size_t i = want.size(); i < have.size(); ++i) e.remove_page(have[i]);
    return changed;
}

std::string look_key(const Page& p) {
    return p.id + "|" + std::to_string(p.crop.x0) + "," + std::to_string(p.crop.y0) + "," +
           std::to_string(p.crop.x1) + "," + std::to_string(p.crop.y1) + "|" + std::to_string(p.turns);
}

}  // namespace optik::core
