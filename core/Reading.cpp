#include "Reading.hpp"

#include "Passes.hpp"
#include "Spell.hpp"

#include <memory>

namespace optik::core {

namespace {

// One Hunspell per worker thread, made on first use (s27). Null when the book's
// language has no dictionary installed: then there is no spelling pass.
Speller* speller_for(const std::string& language) {
    thread_local std::unique_ptr<Speller> sp;
    if (sp && sp->language() == language) return sp->ready() ? sp.get() : nullptr;
    sp = std::make_unique<Speller>();
    sp->open(language);
    return sp->ready() ? sp.get() : nullptr;
}

template <class T>
void spell(T& what, const ReadRequest& r) {
    if (!r.settings.check_spelling) return;
    Speller* sp = speller_for(r.settings.language);
    if (!sp) return;
    mark_spelling(what, [sp](const std::string& w) { return sp->known(w); }, r.book_words);
}

}  // namespace

ReadRequest request_for(const Book& book, const Page& page) {
    ReadRequest r;
    r.page_id   = page.id;
    r.scan_path = book.scan_path(page.scan_id);
    r.crop      = page.crop;
    r.turns     = page.turns;
    r.skew      = page.skew;
    r.settings  = book.meta().settings;
    r.book_words = book.meta().words;
    return r;
}

ReadRequest request_for_area(const Book& book, const Page& page, int area, const Rect& box) {
    ReadRequest r = request_for(book, page);
    r.area     = area;
    r.area_box = box;
    r.area_was = area >= 0 && area < int(page.areas.size()) ? page.areas[std::size_t(area)].box : box;
    return r;
}

Reading read(ocr::Engine& engine, const ReadRequest& r) {
    Reading out;
    out.page_id = r.page_id;
    out.crop    = r.crop;
    out.turns   = r.turns;
    out.skew    = r.skew;
    out.area    = r.area;
    out.area_box = r.area_box;
    out.area_was = r.area_was;
    if (!engine.ready() || engine.language() != r.settings.language) {
        if (!engine.init(r.settings.language, &out.why)) return out;
    }
    if (r.area >= 0) {
        // An area is read in the page as it was read: its boxes are there.
        const ocr::Look look{r.crop, r.turns, r.skew, r.settings.decolour, r.settings.level};
        out.ok = engine.read_area(r.scan_path, look, r.area_box, out.paras, &out.why);
        // The author drew or moved this area: it keeps its kind, so only the
        // word-level pass applies (quotes); headings and openers are the page's.
        if (out.ok && r.settings.mend_quotes) {
            std::vector<ocr::Area> one(1);
            one[0].paras = std::move(out.paras);
            mend_quotes(one);
            out.paras = std::move(one[0].paras);
        }
        if (out.ok) {
            mark_italic(out.paras, r.settings.mark_italic);
            spell(out.paras, r);
        }
        return out;
    }
    // The whole page: conditioned (phase 0, s29), the skew measured afresh when
    // the book deskews -- else read as cut, and the page is straight again.
    const ocr::Look look{r.crop, r.turns, 0.0, r.settings.decolour, r.settings.level};
    out.skew = 0.0;
    out.ok = engine.read_page(r.scan_path, look, out.areas, &out.why,
                              r.settings.deskew ? &out.skew : nullptr);
    if (!out.ok) return out;
    // The passes, each the book's to decline (Passes.hpp has the order's why).
    const Settings& st = r.settings;
    if (st.mark_running_heads) mark_running_heads(out.areas, (r.turns & 1) ? r.crop.width() : r.crop.height());
    if (st.find_dropcaps) {
        // s32: the cap's gap read through the page as just read (straightened
        // by the skew it was measured at), so its boxes are the page's.
        const ocr::Look as_read{r.crop, r.turns, out.skew, st.decolour, st.level};
        mark_dropcaps(out.areas, [&](const Rect& where, ocr::Word& w) {
            return engine.read_word(r.scan_path, as_read, where, w);
        });
    }
    if (st.mark_headings)      mark_headings(out.areas);
    if (st.mend_quotes)        mend_quotes(out.areas);
    if (st.small_caps)         mark_small_caps(out.areas);
    mark_italic(out.areas, st.mark_italic);   // on: doubted; off: the measure dropped
    spell(out.areas, r);                      // Settings::check_spelling, if a dictionary
    return out;
}

bool land(Edit& e, const Reading& r) {
    if (!r.ok || !e.book().page(r.page_id)) return false;
    if (const Page* now = e.book().page(r.page_id); !(now->crop == r.crop) || now->turns != r.turns)
        return false;
    Page& p = e.page(r.page_id);
    if (r.area >= 0) {
        if (e.book().page(r.page_id)->skew != r.skew) return false;   // s29: straightened since
        int at = -1;
        if (r.area < int(p.areas.size()) && p.areas[std::size_t(r.area)].box == r.area_was) at = r.area;
        for (std::size_t i = 0; at < 0 && i < p.areas.size(); ++i)
            if (p.areas[i].box == r.area_was) at = int(i);
        if (at < 0) return false;
        ocr::Area& a = p.areas[std::size_t(at)];
        a.box   = r.area_box;
        a.paras = r.paras;
        a.user  = true;
        if (!ocr::kind_is_text(a.kind)) a.kind = ocr::Kind::Text;
        return true;
    }
    p.areas = r.areas;
    p.read  = true;
    p.skew  = r.skew;
    return true;
}

}  // namespace optik::core
