#include "Slant.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace optik::ocr {

namespace {

constexpr float kMaxShear  = 0.50f;   // ~27 degrees either way
constexpr float kStep      = 0.02f;
constexpr int   kMinInk    = 40;      // pixels of ink: less is a mark, not a word
constexpr int   kMinHeight = 8;
constexpr int   kMinContrast = 60;    // darkest to lightest inside the box
constexpr float kPageLeanCap = 0.08f;

float nan() { return std::numeric_limits<float>::quiet_NaN(); }

}  // namespace

float slant_of(const Grey& g, const Rect& box_in) {
    if (!g.px || g.w <= 0 || g.h <= 0) return nan();
    Rect b{std::max(0, box_in.x0), std::max(0, box_in.y0),
           std::min(g.w, box_in.x1), std::min(g.h, box_in.y1)};
    if (b.empty() || b.height() < kMinHeight) return nan();
    // Two letters at least: a box narrower than it is tall is one glyph ("I",
    // "f", a quote) and its lean says more about the letter than the face.
    if (b.width() < b.height() * 9 / 10) return nan();

    int lo = 255, hi = 0;
    for (int y = b.y0; y < b.y1; ++y) {
        const unsigned char* row = g.px + std::size_t(y) * std::size_t(g.stride);
        for (int x = b.x0; x < b.x1; ++x) { lo = std::min(lo, int(row[x])); hi = std::max(hi, int(row[x])); }
    }
    if (hi - lo < kMinContrast) return nan();
    const int thr = (lo + hi) / 2;

    struct P { int x, y; };
    std::vector<P> ink;
    ink.reserve(std::size_t(b.width()) * std::size_t(b.height()) / 4);
    for (int y = b.y0; y < b.y1; ++y) {
        const unsigned char* row = g.px + std::size_t(y) * std::size_t(g.stride);
        for (int x = b.x0; x < b.x1; ++x)
            if (row[x] < thr) ink.push_back({x - b.x0, y - b.y0});
    }
    if (int(ink.size()) < kMinInk) return nan();

    const float yc  = float(b.height()) / 2.0f;
    const int   pad = int(std::ceil(kMaxShear * yc)) + 2;
    const int   cols = b.width() + 2 * pad;
    std::vector<int> hist(static_cast<std::size_t>(cols));

    const int n = int(std::lround(2.0f * kMaxShear / kStep)) + 1;
    std::vector<double> score(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float s = -kMaxShear + float(i) * kStep;
        std::fill(hist.begin(), hist.end(), 0);
        for (const P& p : ink) {
            const int c = int(std::lround(float(p.x) + s * (float(p.y) - yc))) + pad;
            if (c >= 0 && c < cols) ++hist[std::size_t(c)];
        }
        double sum = 0;
        for (int v : hist) sum += double(v) * double(v);
        score[std::size_t(i)] = sum;
    }
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (score[std::size_t(i)] > score[std::size_t(best)]) best = i;

    // A parabola through the peak and its neighbours: between-step precision.
    float off = 0.0f;
    if (best > 0 && best < n - 1) {
        const double a = score[std::size_t(best - 1)], c0 = score[std::size_t(best)],
                     c = score[std::size_t(best + 1)];
        const double den = a - 2.0 * c0 + c;
        if (den < 0) off = float(0.5 * (a - c) / den);
    }
    return -kMaxShear + (float(best) + off) * kStep;
}

namespace {

void measure_words(const Grey& g, std::vector<std::vector<Word>*>& lines) {
    std::vector<std::vector<float>> slants(lines.size());
    std::vector<float> all;
    for (std::size_t l = 0; l < lines.size(); ++l)
        for (const Word& w : *lines[l]) {
            const float s = slant_of(g, w.box);
            slants[l].push_back(s);
            if (!std::isnan(s)) all.push_back(s);
        }
    float lean = 0.0f;
    if (!all.empty()) {
        std::nth_element(all.begin(), all.begin() + std::ptrdiff_t(all.size() / 2), all.end());
        lean = std::clamp(all[all.size() / 2], -kPageLeanCap, kPageLeanCap);
    }
    for (std::size_t l = 0; l < lines.size(); ++l) {
        std::vector<Word>& ws = *lines[l];
        const std::vector<float>& sl = slants[l];
        // 1 = italic, 0 = roman, -1 = cannot tell, 2 = leans a little (near)
        std::vector<int> it(ws.size());
        for (std::size_t i = 0; i < ws.size(); ++i) {
            if (std::isnan(sl[i])) { it[i] = -1; continue; }
            const float d = sl[i] - lean;
            it[i] = d > kItalicLean ? 1 : d > kItalicLeanNear ? 2 : 0;
        }
        std::vector<int> settled = it;
        for (std::size_t i = 0; i < ws.size(); ++i) {
            if (it[i] != -1 && it[i] != 2) continue;
            // The nearest clearly measured word either side, on this line.
            int left = -1, right = -1;
            for (std::size_t j = i; j-- > 0;) if (it[j] == 0 || it[j] == 1) { left = it[j]; break; }
            for (std::size_t j = i + 1; j < ws.size(); ++j) if (it[j] == 0 || it[j] == 1) { right = it[j]; break; }
            settled[i] = (left == 1 && right == 1) ? 1 : 0;
        }
        it = settled;
        for (std::size_t i = 0; i < ws.size(); ++i) {
            Word& w = ws[i];
            if (w.checked || w.edited) continue;
            w.italic = it[i] == 1;
        }
    }
}

}  // namespace

void measure_italic(const Grey& g, std::vector<Para>& paras) {
    std::vector<std::vector<Word>*> lines;
    for (Para& p : paras)
        for (Line& l : p.lines) lines.push_back(&l.words);
    measure_words(g, lines);
}

void measure_italic(const Grey& g, std::vector<Area>& areas) {
    std::vector<std::vector<Word>*> lines;
    for (Area& a : areas)
        if (kind_is_text(a.kind) || a.kind == Kind::Header || a.kind == Kind::Footer)
            for (Para& p : a.paras)
                for (Line& l : p.lines) lines.push_back(&l.words);
    measure_words(g, lines);
}

}  // namespace optik::ocr
