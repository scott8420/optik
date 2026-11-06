#include "Format.hpp"

namespace optik::core {

const ocr::Word* word_of(const Book& book, const Hit& h) {
    const Page* p = book.page(h.page_id);
    if (!p || h.area < 0 || h.area >= int(p->areas.size())) return nullptr;
    const auto& a = p->areas[std::size_t(h.area)];
    if (h.para < 0 || h.para >= int(a.paras.size())) return nullptr;
    const auto& q = a.paras[std::size_t(h.para)];
    if (h.line < 0 || h.line >= int(q.lines.size())) return nullptr;
    const auto& ws = q.lines[std::size_t(h.line)].words;
    if (h.word < 0 || h.word >= int(ws.size())) return nullptr;
    return &ws[std::size_t(h.word)];
}

bool all_have(const Book& book, const std::vector<Hit>& words, unsigned bit) {
    int seen = 0;
    for (const Hit& h : words) {
        const ocr::Word* w = word_of(book, h);
        if (!w) continue;
        ++seen;
        if (!(ocr::fmt_of(*w) & bit)) return false;
    }
    return seen > 0;
}

int set_format(Edit& e, const std::vector<Hit>& words, unsigned bit, bool on) {
    int n = 0;
    for (const Hit& h : words) {
        const ocr::Word* look = word_of(e.book(), h);
        if (!look || bool(ocr::fmt_of(*look) & bit) == on) continue;
        ocr::Word& w = e.page(h.page_id).areas[std::size_t(h.area)].paras[std::size_t(h.para)]
                           .lines[std::size_t(h.line)].words[std::size_t(h.word)];
        ocr::set_fmt(w, bit, on);
        ++n;
    }
    return n;
}

}  // namespace optik::core
