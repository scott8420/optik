#include "Reading.hpp"

namespace optik::core {

ReadRequest request_for(const Book& book, const Page& page) {
    ReadRequest r;
    r.page_id   = page.id;
    r.scan_path = book.scan_path(page.scan_id);
    r.crop      = page.crop;
    r.settings  = book.meta().settings;
    return r;
}

ReadRequest request_for_area(const Book& book, const Page& page, int area, const Rect& box) {
    ReadRequest r = request_for(book, page);
    r.area     = area;
    r.area_box = box;
    return r;
}

Reading read(ocr::Engine& engine, const ReadRequest& r) {
    Reading out;
    out.page_id = r.page_id;
    out.area    = r.area;
    out.area_box = r.area_box;
    if (!engine.ready() || engine.language() != r.settings.language) {
        if (!engine.init(r.settings.language, &out.why)) return out;
    }
    if (r.area >= 0) {
        out.ok = engine.read_area(r.scan_path, r.crop, r.area_box, out.paras, &out.why);
        return out;
    }
    out.ok = engine.read_page(r.scan_path, r.crop, out.areas, &out.why);
    if (out.ok && r.settings.mark_running_heads)
        mark_running_heads(out.areas, r.crop.height());
    return out;
}

bool land(Edit& e, const Reading& r) {
    if (!r.ok || !e.book().page(r.page_id)) return false;
    Page& p = e.page(r.page_id);
    if (r.area >= 0) {
        if (r.area >= int(p.areas.size())) return false;
        ocr::Area& a = p.areas[std::size_t(r.area)];
        a.box   = r.area_box;
        a.paras = r.paras;
        a.user  = true;
        if (!ocr::kind_is_text(a.kind)) a.kind = ocr::Kind::Text;
        return true;
    }
    p.areas = r.areas;
    p.read  = true;
    return true;
}

}  // namespace optik::core
