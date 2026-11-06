#include "Find.hpp"

namespace optik::core {

namespace {

char lo(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

// Is the character that starts at / ends just before byte i part of a word, for
// "whole words"? ASCII letters and digits, and letters past ASCII (an accent) --
// but not the typographic quotes, dashes and guillemets books are full of.
bool wordish_cp(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (c == 0xAB || c == 0xBB || (c >= 0x2000 && c <= 0x206F)) return false;
    return c >= 0xC0;
}
char32_t decode_at(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + std::size_t(n) > s.size()) n = 1;
    char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3F);
    return cp;
}
bool wordish_after(const std::string& s, std::size_t i) { return i < s.size() && wordish_cp(decode_at(s, i)); }
bool wordish_before(const std::string& s, std::size_t i) {
    if (i == 0) return false;
    std::size_t k = i - 1;
    while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;   // back to the lead byte
    return wordish_cp(decode_at(s, k));
}

}  // namespace

std::vector<std::size_t> find_in(const std::string& text, const std::string& q, const FindOptions& o) {
    std::vector<std::size_t> at;
    if (q.empty() || q.size() > text.size()) return at;
    for (std::size_t i = 0; i + q.size() <= text.size();) {
        bool same = true;
        for (std::size_t k = 0; same && k < q.size(); ++k)
            same = o.match_case ? text[i + k] == q[k] : lo(text[i + k]) == lo(q[k]);
        if (same && o.whole_words) {
            const bool left_ok  = !wordish_before(text, i) || !wordish_after(q, 0);
            const bool right_ok = !wordish_after(text, i + q.size()) || !wordish_before(q, q.size());
            same = left_ok && right_ok;
        }
        if (same) { at.push_back(i); i += q.size(); }
        else ++i;
    }
    return at;
}

std::vector<Hit> find_all(const Book& book, const std::string& q, const FindOptions& o) {
    std::vector<Hit> out;
    if (q.empty()) return out;
    for (const Page& p : book.pages())
        for (std::size_t a = 0; a < p.areas.size(); ++a)
            for (std::size_t r = 0; r < p.areas[a].paras.size(); ++r)
                for (std::size_t l = 0; l < p.areas[a].paras[r].lines.size(); ++l) {
                    const auto& ws = p.areas[a].paras[r].lines[l].words;
                    for (std::size_t w = 0; w < ws.size(); ++w)
                        if (!find_in(ws[w].text, q, o).empty())
                            out.push_back(Hit{p.id, int(a), int(r), int(l), int(w)});
                }
    return out;
}

std::string replace_in(const std::string& text, const std::string& q, const std::string& with,
                       const FindOptions& o, int* n) {
    const auto at = find_in(text, q, o);
    if (n) *n = int(at.size());
    if (at.empty()) return text;
    std::string out;
    std::size_t from = 0;
    for (std::size_t i : at) {
        out.append(text, from, i - from);
        out += with;
        from = i + q.size();
    }
    out.append(text, from, std::string::npos);
    return out;
}

int replace_hits(Edit& e, const std::vector<Hit>& hits, const std::string& q,
                 const std::string& with, const FindOptions& o) {
    int total = 0;
    for (const Hit& h : hits) {
        const Page* look = e.book().page(h.page_id);
        if (!look || h.area < 0 || h.area >= int(look->areas.size())) continue;
        const auto& a = look->areas[std::size_t(h.area)];
        if (h.para < 0 || h.para >= int(a.paras.size())) continue;
        const auto& p = a.paras[std::size_t(h.para)];
        if (h.line < 0 || h.line >= int(p.lines.size())) continue;
        const auto& ws = p.lines[std::size_t(h.line)].words;
        if (h.word < 0 || h.word >= int(ws.size())) continue;
        int n = 0;
        const std::string now = replace_in(ws[std::size_t(h.word)].text, q, with, o, &n);
        if (n == 0) continue;
        ocr::Word& w = e.page(h.page_id).areas[std::size_t(h.area)].paras[std::size_t(h.para)]
                           .lines[std::size_t(h.line)].words[std::size_t(h.word)];
        if (!w.edited) w.original = w.text;
        w.text = now;
        w.edited = w.text != w.original;
        if (!w.edited) w.original.clear();
        total += n;
    }
    return total;
}

}  // namespace optik::core
