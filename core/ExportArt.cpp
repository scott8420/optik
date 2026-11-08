// core/ExportArt.cpp -- what the ODT carries beyond the text (s31): the book's
// page measured from its scans, and its pictures cut from them. The plan is
// taken on the UI thread (cheap, by value); realise() decodes on a worker.
#include "Export.hpp"
#include "PageOps.hpp"

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace optik::core {

ExportPlan plan_of(const Book& book, const std::vector<Block>& blocks) {
    ExportPlan plan;
    plan.title = book.meta().title;

    // The pictures, one source for each Picture block (an unknown page: an
    // empty source, which realise() leaves out -- the count still lines up).
    for (const auto& b : blocks) {
        if (b.kind != Block::Picture) continue;
        ArtSource src;
        if (const Page* p = book.page(b.page_id)) {
            src.path = book.scan_path(p->scan_id);
            src.look = look_of(book, *p);
            // The picture as printed: levelling evens paper to white and ink to
            // black, which is right for type and posterises a photograph.
            src.look.level = false;
            src.rect = b.rect;
        }
        plan.art.push_back(std::move(src));
    }

    // The pages the text comes from (in book order), a few of them spread
    // across the book: enough for a median, not a decode of every scan.
    std::vector<const Page*> body;
    std::set<std::string> seen;
    for (const auto& b : blocks) {
        if (b.kind != Block::Paragraph || !seen.insert(b.page_id).second) continue;
        if (const Page* p = book.page(b.page_id)) body.push_back(p);
    }
    constexpr std::size_t kSample = 9;
    for (std::size_t i = 0; i < std::min(body.size(), kSample); ++i) {
        const Page* p = body[body.size() <= kSample ? i : i * (body.size() - 1) / (kSample - 1)];
        PageSource s;
        s.path = book.scan_path(p->scan_id);
        s.crop = p->crop;
        s.turns = p->turns;
        s.verso = p->half == model::split::Half::Left;
        for (const auto& a : p->areas)
            if (ocr::kind_is_text(a.kind) || a.kind == ocr::Kind::Picture) s.text = ocr::unite(s.text, a.box);
        plan.pages.push_back(std::move(s));
    }
    return plan;
}

namespace {

struct ScanInfo { int w = 0, h = 0; double ppi = 0; bool ok = false; };

ScanInfo scan_info(const std::string& path) {
    ScanInfo si;
    Pix* pix = pixRead(path.c_str());
    if (!pix) return si;
    si.w = int(pixGetWidth(pix));
    si.h = int(pixGetHeight(pix));
    si.ppi = double(pixGetXRes(pix));
    si.ok = true;
    pixDestroy(&pix);
    return si;
}

}  // namespace

OdtBook realise(const ExportPlan& plan, const std::function<bool()>& cancelled) {
    OdtBook out;
    out.title = plan.title;

    // The page: each sampled page's size in pixels (its crop, turned) and its
    // text's bounds; the scans' resolution, their median.
    std::map<std::string, ScanInfo> scans;
    auto info = [&](const std::string& path) -> const ScanInfo& {
        auto it = scans.find(path);
        if (it == scans.end()) it = scans.emplace(path, scan_info(path)).first;
        return it->second;
    };
    std::vector<Measured> measured;
    std::vector<double> ppis;
    for (const auto& s : plan.pages) {
        if (cancelled && cancelled()) { out.art.resize(plan.art.size()); return out; }
        const ScanInfo& si = info(s.path);
        if (!si.ok) continue;
        int w = s.crop.empty() ? si.w : s.crop.width();
        int h = s.crop.empty() ? si.h : s.crop.height();
        if (s.turns % 2) std::swap(w, h);
        measured.push_back({w, h, s.text, s.verso});
        if (si.ppi > 0) ppis.push_back(si.ppi);
    }
    double ppi = 0;
    if (!ppis.empty()) {
        std::nth_element(ppis.begin(), ppis.begin() + std::ptrdiff_t(ppis.size() / 2), ppis.end());
        ppi = ppis[ppis.size() / 2];
    }
    out.page = page_size(measured, ppi);

    // The pictures: the page as read (cut, turned, straightened -- where its
    // boxes are), the box cut from it, as PNG. One page decoded once for all
    // the pictures on it.
    std::string last_key;
    Pix* page = nullptr;
    for (const auto& a : plan.art) {
        out.art.emplace_back();
        if (cancelled && cancelled()) break;
        if (a.path.empty() || a.rect.empty()) continue;
        const std::string key = a.path + '|' + std::to_string(a.look.crop.x0) + ',' + std::to_string(a.look.crop.y0) + ',' +
                                std::to_string(a.look.crop.x1) + ',' + std::to_string(a.look.crop.y1) + '|' +
                                std::to_string(a.look.turns) + '|' + std::to_string(a.look.skew) + '|' +
                                (a.look.decolour ? "g" : "c");
        if (key != last_key) {
            if (page) pixDestroy(&page);
            page = ocr::load_page(a.path, a.look);
            last_key = key;
        }
        if (!page) continue;
        const int pw = int(pixGetWidth(page)), ph = int(pixGetHeight(page));
        const int x0 = std::clamp(a.rect.x0, 0, pw), y0 = std::clamp(a.rect.y0, 0, ph);
        const int x1 = std::clamp(a.rect.x1, 0, pw), y1 = std::clamp(a.rect.y1, 0, ph);
        if (x1 <= x0 || y1 <= y0) continue;
        Box* box = boxCreate(x0, y0, x1 - x0, y1 - y0);
        Pix* cut = box ? pixClipRectangle(page, box, nullptr) : nullptr;
        if (box) boxDestroy(&box);
        if (!cut) continue;
        l_uint8* data = nullptr;
        size_t size = 0;
        if (pixWriteMem(&data, &size, cut, IFF_PNG) == 0 && data && size > 0) {
            Art& art = out.art.back();
            art.png.assign(reinterpret_cast<const char*>(data), size);
            // Its printed size: the scan's resolution, the book's when the scan
            // doesn't say; 300 when neither does (a book scan's commonest).
            double r = info(a.path).ppi;
            if (r < 100 || r > 2400) r = ppi >= 100 && ppi <= 2400 ? ppi : 300.0;
            art.w = (x1 - x0) * 72.0 / r;
            art.h = (y1 - y0) * 72.0 / r;
        }
        if (data) lept_free(data);
        pixDestroy(&cut);
    }
    if (page) pixDestroy(&page);
    out.art.resize(plan.art.size());
    return out;
}

}  // namespace optik::core
