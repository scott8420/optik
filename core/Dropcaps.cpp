// core/Dropcaps.cpp -- the dropcap pass (s32, milestone 24). See Passes.hpp.
//
// A chapter's large initial is two to four lines deep at the left edge. The
// engine does one of three things with it, and each is a shape here:
//
//   A  its own area: a one-letter text area, or a picture, standing left of
//      the lines it is beside
//   B  a tall first word in its paragraph's first line
//   C  nothing at all -- the lines beside it come back indented (often an
//      area of their own, the paragraph's rest in the next area below)
//
// In every shape the letter(s) go to the head of the paragraph's first word
// ("T" + "HE" -> "THE"), the word is doubted (a pass's guess wants a look),
// and the paragraph records the cap: how many letters, how many lines deep.
// Export gives it the drop-cap look or not (Settings::rebuild_dropcaps).
//
// Shapes A and C read the cap's pixels through `read` (the engine's read_word
// on a worker; a stand-in in the selftest). Nothing that does not read as one
// to three letters is a cap: the read is the witness, the geometry the reason
// to ask.

#include "Passes.hpp"

#include <algorithm>
#include <cctype>

namespace optik::core {

using ocr::Area;
using ocr::Kind;
using ocr::Line;
using ocr::Para;
using ocr::Rect;
using ocr::Word;

namespace {

int median(std::vector<int> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// One to three letters, nothing else: a cap. Non-ASCII code points count as
// letters (an accented capital); ASCII must be alphabetic.
int cap_letters_of(const std::string& s) {
    int n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (!std::isalpha(c)) return 0;
            ++n;
        } else if ((c & 0xC0) != 0x80) {
            ++n;   // a lead byte: one code point
        }
    }
    return n >= 1 && n <= 3 ? n : 0;
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

int centre_y(const Rect& r) { return (r.y0 + r.y1) / 2; }

// The lines of `p` whose middle lies within the cap's height: how deep it is.
int depth(const Para& p, const Rect& cap) {
    int n = 0;
    for (const auto& l : p.lines)
        if (!l.words.empty() && centre_y(l.box) >= cap.y0 && centre_y(l.box) <= cap.y1) ++n;
    return std::max(n, 1);
}

// The splice: the letters at the head of the paragraph's first word.
bool splice(Para& p, const std::string& letters, const Rect& cap, float conf) {
    if (p.lines.empty() || p.lines[0].words.empty() || p.cap_letters > 0) return false;
    Word& w = p.lines[0].words[0];
    if (w.checked || w.edited) return false;
    w.text  = letters + w.text;
    w.box.x0 = std::min(w.box.x0, cap.x0);
    w.conf  = std::min(w.conf, conf);
    w.doubt = true;
    p.lines[0].box.x0 = std::min(p.lines[0].box.x0, cap.x0);
    p.box = ocr::unite(p.box, cap);
    p.cap_letters = cap_letters_of(letters);
    p.cap_lines   = depth(p, cap);
    return true;
}

struct Metrics { int lh = 0; int pitch = 0; };

// The body's line height and pitch: medians over the page's text lines.
Metrics metrics_of(const std::vector<Area>& areas) {
    std::vector<int> h, step;
    for (const auto& a : areas) {
        if (a.kind != Kind::Text) continue;
        for (const auto& p : a.paras)
            for (std::size_t i = 0; i < p.lines.size(); ++i) {
                h.push_back(p.lines[i].box.height());
                if (i) step.push_back(p.lines[i].box.y0 - p.lines[i - 1].box.y0);
            }
    }
    Metrics m;
    m.lh = median(h);
    m.pitch = step.empty() ? m.lh * 3 / 2 : median(step);
    if (m.pitch < m.lh) m.pitch = m.lh * 3 / 2;
    return m;
}

// A cap area's letters: its own words if they are one to three letters, else
// read from the pixels.
bool letters_of(const Area& a, const DropcapReader& read, std::string& text, float& conf, Rect& box) {
    if (ocr::kind_is_text(a.kind)) {
        std::string t;
        float c = 100.0f;
        int words = 0;
        for (const auto& p : a.paras)
            for (const auto& l : p.lines)
                for (const auto& w : l.words) { t += w.text; c = std::min(c, w.conf); ++words; }
        if (words == 1 && cap_letters_of(t)) { text = t; conf = c; box = a.box; return true; }
    }
    if (!read) return false;
    Word w;
    if (!read(a.box, w) || !cap_letters_of(w.text)) return false;
    text = w.text; conf = w.conf; box = w.box.empty() ? a.box : w.box;
    return true;
}

// Shape A: a small area left of a text area's first line, as deep as 2-4 lines.
int shape_a(std::vector<Area>& areas, const Metrics& m, const DropcapReader& read) {
    int found = 0;
    for (std::size_t c = 0; c < areas.size(); ++c) {
        const Area& cap = areas[c];
        if (cap.user || cap.kind == Kind::Header || cap.kind == Kind::Footer || cap.kind == Kind::Ignore)
            continue;
        const int h = cap.box.height(), w = cap.box.width();
        if (h < m.lh * 8 / 5 || h > m.pitch * 5 || w > m.pitch * 4) continue;
        // ...a one-line text area at most (a picture has no lines).
        int lines = 0;
        for (const auto& p : cap.paras) lines += int(p.lines.size());
        if (lines > 1) continue;
        // The text it stands beside: an area whose first line starts just right
        // of it, its top near the cap's top.
        int beside = -1;
        for (std::size_t t = 0; t < areas.size() && beside < 0; ++t) {
            const Area& a = areas[t];
            if (t == c || a.user || a.kind != Kind::Text || a.paras.empty() || a.paras[0].lines.empty()) continue;
            const Rect& l0 = a.paras[0].lines[0].box;
            const int gap = l0.x0 - cap.box.x1;
            if (gap < -m.lh / 2 || gap > m.lh * 2) continue;
            if (std::abs(l0.y0 - cap.box.y0) > m.lh) continue;
            beside = int(t);
        }
        if (beside < 0) continue;
        std::string text;
        float conf = 0;
        Rect box;
        if (!letters_of(cap, read, text, conf, box)) continue;
        Area& t = areas[std::size_t(beside)];
        if (!splice(t.paras[0], text, box, conf)) continue;
        t.box = ocr::unite(t.box, box);
        areas.erase(areas.begin() + std::ptrdiff_t(c));
        --c;
        ++found;
    }
    return found;
}

// Shape B: the first word of a paragraph, far taller than the body's line.
int shape_b(std::vector<Area>& areas, const Metrics& m) {
    int found = 0;
    for (auto& a : areas) {
        if (a.user || a.kind != Kind::Text) continue;
        for (auto& p : a.paras) {
            if (p.cap_letters || p.lines.empty() || p.lines[0].words.empty()) continue;
            Line& l0 = p.lines[0];
            const Word w0 = l0.words[0];
            if (w0.checked || w0.edited || !cap_letters_of(w0.text)) continue;
            if (w0.box.height() < m.lh * 8 / 5) continue;
            // The word it opens: the next in the line, else the next line's first.
            Para trial = p;
            Line& t0 = trial.lines[0];
            t0.words.erase(t0.words.begin());
            if (t0.words.empty()) trial.lines.erase(trial.lines.begin());
            if (trial.lines.empty() || trial.lines[0].words.empty()) continue;
            if (!splice(trial, w0.text, w0.box, w0.conf)) continue;
            p = std::move(trial);
            ++found;
        }
    }
    return found;
}

// Shape C: the cap missed. A paragraph's first 2-4 lines stand indented by more
// than a line's height from where its next line starts -- in the paragraph, or
// in the area just below (the engine cut the paragraph there) -- and the gap
// they leave reads as letters.
int shape_c(std::vector<Area>& areas, const Metrics& m, const DropcapReader& read) {
    if (!read) return 0;
    int found = 0;
    for (std::size_t ai = 0; ai < areas.size(); ++ai) {
        if (areas[ai].user || areas[ai].kind != Kind::Text) continue;
        for (std::size_t pi = 0; pi < areas[ai].paras.size(); ++pi) {
            Para& p = areas[ai].paras[pi];
            if (p.cap_letters || p.lines.size() < 2 || p.lines[0].words.empty()) continue;
            const int x = p.lines[0].box.x0;
            // The indented run: lines starting where the first does.
            std::size_t k = 1;
            while (k < p.lines.size() && std::abs(p.lines[k].box.x0 - x) <= m.lh / 2) ++k;
            if (k < 2 || k > 4) continue;
            // Where the column starts: the paragraph's next line, or the area below.
            int left = 0;
            int below = -1;
            if (k < p.lines.size()) {
                left = p.lines[k].box.x0;
            } else if (pi + 1 == areas[ai].paras.size()) {
                const int foot = p.lines[k - 1].box.y1;
                for (std::size_t b = 0; b < areas.size() && below < 0; ++b) {
                    const Area& a = areas[b];
                    if (b == ai || a.user || a.kind != Kind::Text || a.paras.empty() ||
                        a.paras[0].lines.empty())
                        continue;
                    const Rect& l0 = a.paras[0].lines[0].box;
                    if (l0.y0 < foot - m.lh / 2 || l0.y0 > foot + m.pitch) continue;
                    if (l0.x1 <= x) continue;   // not under these lines
                    below = int(b);
                    left = l0.x0;
                }
                if (below < 0) continue;
            } else {
                continue;
            }
            if (x - left < m.lh * 6 / 5) continue;
            // The gap beside the indented lines, clear of every other area.
            const Rect gap{left - m.lh / 2, p.lines[0].box.y0 - m.lh / 2, x - m.lh / 4,
                           p.lines[k - 1].box.y1 + m.lh / 2};
            bool clear = true;
            for (std::size_t o = 0; o < areas.size() && clear; ++o)
                if (o != ai && int(o) != below && overlaps(areas[o].box, gap)) clear = false;
            if (!clear) continue;
            Word w;
            if (!read(gap, w) || !cap_letters_of(w.text)) continue;
            const Rect cap = w.box.empty() ? gap : w.box;
            if (cap.height() < m.lh * 8 / 5) continue;   // a small letter: not a cap
            if (!splice(p, w.text, cap, w.conf)) continue;
            areas[ai].box = ocr::unite(areas[ai].box, cap);
            ++found;
            break;   // one cap an area
        }
    }
    return found;
}

// The paragraph's rest, cut off by the engine into the area just below, comes
// back to a cap's paragraph -- when that area's first line starts out at the
// column's edge, left of the lines beside the cap (an indent of its own there
// would be a new paragraph).
void rejoin(std::vector<Area>& areas, const Metrics& m) {
    for (std::size_t ai = 0; ai < areas.size(); ++ai) {
        Area& a = areas[ai];
        if (a.user || a.kind != Kind::Text || a.paras.empty()) continue;
        Para& p = a.paras.back();
        if (!p.cap_letters || p.lines.empty()) continue;
        const Rect& last = p.lines.back().box;
        int below = -1;
        for (std::size_t b = 0; b < areas.size() && below < 0; ++b) {
            const Area& o = areas[b];
            if (b == ai || o.user || o.kind != Kind::Text || o.paras.empty() || o.paras[0].lines.empty())
                continue;
            const Rect& l0 = o.paras[0].lines[0].box;
            if (l0.y0 < last.y1 - m.lh / 2 || l0.y0 > last.y1 + m.pitch) continue;
            if (l0.x1 <= last.x0 || l0.x0 > last.x0 - m.lh) continue;
            below = int(b);
        }
        if (below < 0) continue;
        Area& b = areas[std::size_t(below)];
        Para& rest = b.paras[0];
        if (rest.lines.size() > 1 && rest.lines[0].box.x0 - rest.lines[1].box.x0 > m.lh / 2) continue;
        for (auto& l : rest.lines) p.lines.push_back(std::move(l));
        p.box = ocr::unite(p.box, rest.box);
        a.box = ocr::unite(a.box, rest.box);
        b.paras.erase(b.paras.begin());
        if (b.paras.empty()) {
            areas.erase(areas.begin() + below);
            if (std::size_t(below) < ai) --ai;
        } else {
            Rect r = b.paras[0].box;
            for (const auto& bp : b.paras) r = ocr::unite(r, bp.box);
            b.box = r;
        }
    }
}

}  // namespace

int mark_dropcaps(std::vector<Area>& areas, const DropcapReader& read) {
    const Metrics m = metrics_of(areas);
    if (m.lh <= 0) return 0;
    int n = shape_a(areas, m, read);
    n += shape_b(areas, m);
    n += shape_c(areas, m, read);
    if (n) rejoin(areas, m);
    return n;
}

}  // namespace optik::core
