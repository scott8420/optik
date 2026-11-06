#include "Passes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace optik::core {

using ocr::Area;
using ocr::Kind;
using ocr::Line;
using ocr::Para;
using ocr::Rect;
using ocr::Word;

namespace {

bool untouchable(const Word& w) { return w.checked || w.edited; }

// -- UTF-8, just enough for quote marks ------------------------------------------

std::vector<char32_t> decode(const std::string& s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + std::size_t(n) > s.size()) n = 1;
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += std::size_t(n);
    }
    return out;
}

void encode(char32_t cp, std::string& out) {
    if (cp < 0x80) { out += char(cp); return; }
    if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); return; }
    if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F)); return;
    }
    out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
}

constexpr char32_t kLsquo = 0x2018, kRsquo = 0x2019, kLdquo = 0x201C, kRdquo = 0x201D;

// The weight of a quote mark: 1 for a single, 2 for a double, 0 for not a quote.
int quote_weight(char32_t c) {
    switch (c) {
        case U'\'': case U'`': case 0x00B4: case kLsquo: case kRsquo: case 0x2032: return 1;
        case U'"': case kLdquo: case kRdquo: case 0x201E: case 0x2033:        return 2;
        default: return 0;
    }
}

// A letter or digit: ASCII alnum, or anything past ASCII that is not a quote or
// a dash/space-like mark (letters with accents are what is up there in a book).
bool wordish(char32_t c) {
    if (c < 0x80) return std::isalnum(int(c)) != 0;
    if (quote_weight(c)) return false;
    return !(c >= 0x2000 && c <= 0x206F) && c != 0x00A0;
}

bool has_wordish(const std::string& s) {
    for (char32_t c : decode(s)) if (wordish(c)) return true;
    return false;
}

bool all_quotes(const std::string& s) {
    const auto cps = decode(s);
    if (cps.empty()) return false;
    for (char32_t c : cps) if (!quote_weight(c)) return false;
    return true;
}

// Elisions open with an apostrophe, not a quote: 'tis, 'em.
bool elision(const std::vector<char32_t>& core) {
    std::string w;
    for (char32_t c : core) { if (c >= 0x80) return false; w += char(std::tolower(int(c))); }
    for (const char* e : {"tis", "twas", "em", "til", "cause", "n", "neath", "round", "ere"})
        if (w == e) return true;
    return false;
}

// One run of marks -> the quote it stands for. `singles` / `doubles` count the
// marks of each weight; `units` is their total weight.
void emit_run(int units, int singles, int doubles, bool opening, std::string& out, bool& doubt) {
    const char32_t s = opening ? kLsquo : kRsquo;
    const char32_t d = opening ? kLdquo : kRdquo;
    if (units == 0) return;
    if (units == 1) { encode(s, out); return; }
    if (units == 2 || units == 4) { encode(d, out); return; }   // `` or a double read twice
    if (units == 3 && singles == 1 && doubles == 1) {           // nested: “‘ ... ’”
        if (opening) { encode(d, out); encode(s, out); }
        else         { encode(s, out); encode(d, out); }
        return;
    }
    encode(d, out);          // 3 of one kind, or heavier than any one quote
    doubt = true;
}

}  // namespace

// -- quotes ------------------------------------------------------------------------

std::string mend_quote_word(const std::string& text, bool* doubt_out) {
    const auto cps = decode(text);
    bool doubt = false;
    std::size_t a = 0, b = cps.size();
    while (a < cps.size() && !wordish(cps[a])) ++a;
    if (a == cps.size()) {                       // no letters: a lone mark or punctuation
        if (doubt_out) *doubt_out = false;
        return text;
    }
    while (b > a && !wordish(cps[b - 1])) --b;

    std::string out;
    // Lead: other punctuation keeps its place before the opening quote ("(``" ->
    // "(“"); the quote goes where its first mark was.
    {
        int units = 0, singles = 0, doubles = 0;
        std::size_t first_q = a, last_q = 0;
        bool any = false;
        for (std::size_t i = 0; i < a; ++i)
            if (int q = quote_weight(cps[i])) {
                units += q; (q == 1 ? singles : doubles)++;
                if (!any) first_q = i;
                last_q = i; any = true;
            }
        bool split = false;
        for (std::size_t i = first_q; any && i < last_q; ++i)
            if (!quote_weight(cps[i])) split = true;
        const std::vector<char32_t> core(cps.begin() + long(a), cps.begin() + long(b));
        for (std::size_t i = 0; i < a; ++i) {
            if (quote_weight(cps[i])) {
                if (i == first_q) {
                    if (units == 1 && elision(core)) encode(kRsquo, out);
                    else emit_run(units, singles, doubles, true, out, doubt);
                }
                continue;
            }
            encode(cps[i], out);
        }
        if (split) doubt = true;
    }
    // Core: a single mark between letters is an apostrophe.
    for (std::size_t i = a; i < b; ++i) {
        if (quote_weight(cps[i]) == 1) encode(kRsquo, out);
        else encode(cps[i], out);
    }
    // Tail: the closing quote goes where its last mark was ("car.''" -> "car.”",
    // "car''." -> "car”."); marks split by other punctuation are doubted.
    {
        int units = 0, singles = 0, doubles = 0;
        std::size_t first_q = 0, last_q = b;
        bool any = false;
        for (std::size_t i = b; i < cps.size(); ++i)
            if (int q = quote_weight(cps[i])) {
                units += q; (q == 1 ? singles : doubles)++;
                if (!any) first_q = i;
                last_q = i; any = true;
            }
        bool split = false;
        for (std::size_t i = first_q; any && i < last_q; ++i)
            if (!quote_weight(cps[i])) split = true;
        for (std::size_t i = b; i < cps.size(); ++i) {
            if (quote_weight(cps[i])) {
                if (i == last_q) emit_run(units, singles, doubles, false, out, doubt);
                continue;
            }
            encode(cps[i], out);
        }
        if (split) doubt = true;
    }
    if (doubt_out) *doubt_out = doubt;
    return out;
}

void mend_quotes(std::vector<Area>& areas) {
    for (auto& area : areas) {
        if (!ocr::kind_is_text(area.kind)) continue;
        for (auto& para : area.paras)
            for (auto& line : para.lines) {
                auto& ws = line.words;
                // A word that is nothing but marks joins its neighbour: the next
                // word if there is one (it opens), else the one before (it closes).
                for (std::size_t i = 0; i < ws.size();) {
                    if (untouchable(ws[i]) || !all_quotes(ws[i].text)) { ++i; continue; }
                    const bool next = i + 1 < ws.size() && !untouchable(ws[i + 1]);
                    const bool prev = i > 0 && !untouchable(ws[i - 1]);
                    if (!next && !prev) { ++i; continue; }
                    Word& to = next ? ws[i + 1] : ws[i - 1];
                    to.text = next ? ws[i].text + to.text : to.text + ws[i].text;
                    to.box  = ocr::unite(to.box, ws[i].box);
                    to.conf = std::min(to.conf, ws[i].conf);
                    to.doubt = to.doubt || ws[i].doubt;
                    ws.erase(ws.begin() + long(i));
                }
                for (auto& w : ws) {
                    if (untouchable(w) || !has_wordish(w.text)) continue;
                    bool doubt = false;
                    std::string t = mend_quote_word(w.text, &doubt);
                    w.text = std::move(t);
                    w.doubt = w.doubt || doubt;
                }
            }
    }
}

// -- headings ----------------------------------------------------------------------

namespace {

struct LineRef {
    std::size_t area, para, line;
    Rect        box;
    bool        heading = false;
};

std::string line_text(const Line& l) {
    std::string s;
    for (const auto& w : l.words) { if (!s.empty()) s += ' '; s += w.text; }
    return s;
}

int median(std::vector<int> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

Rect lines_box(const std::vector<Line>& ls) {
    Rect r;
    for (const auto& l : ls) r = ocr::unite(r, l.box);
    return r;
}

}  // namespace

void mark_headings(std::vector<Area>& areas) {
    // The body column and its line pitch, from the multi-line text areas.
    int cx0 = 1 << 30, cx1 = -1;
    std::vector<int> pitches, heights;
    for (const auto& a : areas) {
        if (a.kind != Kind::Text) continue;
        int n = 0;
        for (const auto& p : a.paras) n += int(p.lines.size());
        if (n < 2) continue;
        for (const auto& p : a.paras)
            for (std::size_t l = 0; l < p.lines.size(); ++l) {
                const Rect& b = p.lines[l].box;
                if (b.empty()) continue;
                cx0 = std::min(cx0, b.x0);
                cx1 = std::max(cx1, b.x1);
                heights.push_back(b.height());
                if (l > 0 && !p.lines[l - 1].box.empty())
                    pitches.push_back(b.y0 - p.lines[l - 1].box.y0);
            }
    }
    if (cx1 <= cx0) return;            // no body on the page to measure against
    const int cw = cx1 - cx0;
    const int cmid2 = cx0 + cx1;       // twice the centre, to stay in integers
    int pitch = median(pitches);
    const int lh = median(heights);
    if (pitch <= 0) pitch = lh * 3 / 2;
    const int need_gap = std::max(pitch, lh * 3 / 2);

    // Every line of the text areas the passes may mark, top to bottom.
    std::vector<LineRef> lines;
    for (std::size_t a = 0; a < areas.size(); ++a) {
        if (!ocr::kind_is_text(areas[a].kind)) continue;
        for (std::size_t p = 0; p < areas[a].paras.size(); ++p)
            for (std::size_t l = 0; l < areas[a].paras[p].lines.size(); ++l) {
                const auto& ln = areas[a].paras[p].lines[l];
                if (ln.box.empty()) continue;
                LineRef r{a, p, l, ln.box};
                const Area& ar = areas[a];
                const std::string text = line_text(ln);
                const int w = ln.box.width();
                const int off2 = std::abs(ln.box.x0 + ln.box.x1 - cmid2);   // 2 x off-centre
                r.heading = !ar.user && ar.kind == Kind::Text &&
                            has_wordish(text) && text.size() <= 60 &&
                            w * 4 <= cw * 3 &&                   // at most 3/4 of the column
                            ln.box.x0 - cx0 >= cw / 10 &&        // indented from the left...
                            off2 * 100 <= cw * 12;               // ...and centred (within 6%)
                lines.push_back(r);
            }
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LineRef& x, const LineRef& y) { return x.box.y0 < y.box.y0; });

    // A run of heading-like lines is a heading only with space below it and only
    // while short (a centred poem is not a heading).
    for (std::size_t i = 0; i < lines.size();) {
        if (!lines[i].heading) { ++i; continue; }
        std::size_t j = i;
        while (j < lines.size() && lines[j].heading) ++j;
        const bool space = j == lines.size() || lines[j].box.y0 - lines[j - 1].box.y1 >= need_gap;
        if (!(space && j - i <= 4))
            for (std::size_t k = i; k < j; ++k) lines[k].heading = false;
        i = j;
    }

    // Which lines of which area are heading.
    std::vector<std::vector<std::vector<char>>> is_head(areas.size());
    for (std::size_t a = 0; a < areas.size(); ++a) {
        is_head[a].resize(areas[a].paras.size());
        for (std::size_t p = 0; p < areas[a].paras.size(); ++p)
            is_head[a][p].assign(areas[a].paras[p].lines.size(), 0);
    }
    bool any = false;
    for (const auto& r : lines)
        if (r.heading) { is_head[r.area][r.para][r.line] = 1; any = true; }
    if (!any) return;

    // Rebuild: each area cut into runs of heading / not-heading lines. A para cut
    // by a run boundary becomes two paras. A heading run is one para, its lines
    // kept (export breaks them, "CHAPTER II" / "AN IVORY CHARM").
    std::vector<Area> out;
    for (std::size_t a = 0; a < areas.size(); ++a) {
        Area& src = areas[a];
        bool touched = false;
        for (const auto& pv : is_head[a]) for (char c : pv) touched = touched || c;
        if (!touched) { out.push_back(std::move(src)); continue; }

        Area cur;
        bool cur_head = false, started = false;
        auto flush = [&] {
            if (!started) return;
            cur.box = Rect{};
            for (auto& p : cur.paras) { p.box = lines_box(p.lines); cur.box = ocr::unite(cur.box, p.box); }
            if (!cur.paras.empty()) out.push_back(std::move(cur));
            cur = Area{};
            started = false;
        };
        for (std::size_t p = 0; p < src.paras.size(); ++p) {
            bool new_para = true;
            for (std::size_t l = 0; l < src.paras[p].lines.size(); ++l) {
                const bool h = is_head[a][p][l];
                if (!started || h != cur_head) {
                    flush();
                    cur.kind = h ? Kind::Heading : src.kind;
                    cur.user = src.user;
                    cur_head = h;
                    started  = true;
                    new_para = true;
                }
                if (new_para && !(h && !cur.paras.empty())) cur.paras.push_back(Para{});
                new_para = false;
                cur.paras.back().lines.push_back(std::move(src.paras[p].lines[l]));
            }
        }
        flush();
    }
    areas = std::move(out);
}

// -- small caps --------------------------------------------------------------------

namespace {

int count_if_ascii(const std::string& s, int (*pred)(int)) {
    int n = 0;
    for (unsigned char c : s) if (c < 0x80 && pred(c)) ++n;
    return n;
}

// "CHarM": a capital after a small letter, inside a word.
bool inner_capital(const std::string& s) {
    bool lower_seen = false;
    for (unsigned char c : s) {
        if (c >= 0x80) continue;
        if (std::islower(c)) lower_seen = true;
        else if (std::isupper(c) && lower_seen) return true;
    }
    return false;
}

bool all_caps(const std::string& s) {
    return count_if_ascii(s, std::isupper) > 0 && count_if_ascii(s, std::islower) == 0;
}

// "AN", "OF": a word of two or more capitals that is not a roman numeral ("II",
// "IV" sit in title-case headings too). One is enough to call a line caps when
// the engine read the rest of it in small letters ("AN Ivory Crrsrst").
bool caps_word(const std::string& s) {
    if (count_if_ascii(s, std::isupper) < 2 || !all_caps(s)) return false;
    for (unsigned char c : s)
        if (std::isalpha(c) && !std::strchr("IVXLCDM", c)) return true;
    return false;
}

}  // namespace

void mark_small_caps(std::vector<Area>& areas) {
    // Headings in caps: mend the case, doubt the letters.
    for (auto& a : areas) {
        if (a.kind != Kind::Heading || a.user) continue;
        for (auto& p : a.paras)
            for (auto& l : p.lines) {
                int up = 0, low = 0;
                bool witness = false;   // a capital inside a word, or a caps word
                for (const auto& w : l.words) {
                    up  += count_if_ascii(w.text, std::isupper);
                    low += count_if_ascii(w.text, std::islower);
                    witness = witness || inner_capital(w.text) || caps_word(w.text);
                }
                if (low == 0 || (up < low && !witness)) continue;
                for (auto& w : l.words) {
                    if (untouchable(w) || count_if_ascii(w.text, std::islower) == 0) continue;
                    for (char& c : w.text)
                        if (static_cast<unsigned char>(c) < 0x80) c = char(std::toupper(static_cast<unsigned char>(c)));
                    w.doubt = true;
                }
            }
    }

    // Openers: the first text line under each heading.
    for (const auto& h : areas) {
        if (h.kind != Kind::Heading) continue;
        Line* best = nullptr;
        int best_y = 1 << 30;
        bool heading_between = false;
        for (auto& a : areas) {
            if (!ocr::kind_is_text(a.kind)) continue;
            for (auto& p : a.paras)
                for (auto& l : p.lines) {
                    if (l.box.empty() || l.words.empty() || l.box.y0 < h.box.y1) continue;
                    if (l.box.y0 < best_y) {
                        best_y = l.box.y0;
                        best = &l;
                        heading_between = a.kind == Kind::Heading;
                    }
                }
        }
        if (!best || heading_between) continue;   // the next heading line, not the text
        auto& ws = best->words;
        for (std::size_t i = 0; i < ws.size(); ++i) {
            const bool caps = all_caps(ws[i].text) || inner_capital(ws[i].text);
            if (i > 0 && !caps) break;
            if (!untouchable(ws[i]) && has_wordish(ws[i].text)) ws[i].doubt = true;
        }
    }
}

// -- italic (s27) ---------------------------------------------------------------

void mark_italic(std::vector<ocr::Para>& paras, bool on) {
    for (auto& p : paras)
        for (auto& l : p.lines)
            for (auto& w : l.words) {
                if (w.checked || w.edited) continue;
                if (!on) { w.italic = false; continue; }
                if (w.italic) w.doubt = true;
            }
}

void mark_italic(std::vector<ocr::Area>& areas, bool on) {
    for (auto& a : areas)
        if (ocr::kind_is_text(a.kind)) mark_italic(a.paras, on);
        else if (!on) mark_italic(a.paras, false);
}

}  // namespace optik::core
