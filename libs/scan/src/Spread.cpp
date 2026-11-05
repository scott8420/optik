#include "model/Spread.hpp"

#include <algorithm>
#include <cmath>

namespace optik::model::spread {
namespace {

// Vertical extent of the type block: first and last row holding at least
// k.row_min_ink ink pixels. Everything downstream measures "full height"
// against THIS, not against the image -- a page's top and bottom margins are
// clear columns too, and a gutter test that counts them is measuring paper.
struct TypeBand {
    int top    = 0;
    int bottom = -1;          // inclusive; bottom < top means "no ink found"
    bool empty() const { return bottom < top; }
};

TypeBand find_type_band(const Grey& g, const Constants& k) {
    TypeBand band;
    band.top    = 0;
    band.bottom = -1;

    int first = -1, last = -1;
    for (int y = 0; y < g.height; ++y) {
        int ink = 0;
        const std::uint8_t* row = g.data + static_cast<long>(y) * g.row_stride();
        for (int x = 0; x < g.width; ++x) {
            if (row[x] <= k.ink_max) {
                if (++ink >= k.row_min_ink) break;   // enough; stop counting
            }
        }
        if (ink >= k.row_min_ink) {
            if (first < 0) first = y;
            last = y;
        }
    }
    if (first >= 0) {
        band.top    = first;
        band.bottom = last;
    }
    return band;
}

// Is this column clear THROUGH the type band? Clear means fewer than
// k.col_min_ink ink pixels between band.top and band.bottom inclusive.
//
// This is the "full height of the type block" rule, and it is the entire
// difference between a real gutter and a fake one. On the corpus's wrap-channel
// page the right page's inner white column is nearly full height, interrupted
// only by five full-measure lines in the middle band -- and a "mostly white"
// test passes there and cuts in the wrong place.
bool column_clear(const Grey& g, const TypeBand& band, int x, const Constants& k) {
    int ink = 0;
    for (int y = band.top; y <= band.bottom; ++y) {
        if (g.at(x, y) <= k.ink_max) {
            if (++ink >= k.col_min_ink) return false;
        }
    }
    return true;
}

// Does [x0,x1) carry any row with enough ink to be type? Stops at the first
// one, so on an inked page this reads a handful of rows -- which is what keeps
// the aspect gate cheap even now that every page has to answer this.
bool any_ink(const Grey& g, int x0, int x1, const Constants& k) {
    x0 = std::max(0, x0);
    x1 = std::min(g.width, x1);
    // Unreachable on any valid input -- the search window is +/-15% of a
    // midline, so neither certain region can be empty -- and a mutation
    // inverting it is equivalent. Kept because "the caller passed a degenerate
    // range" is not a thing this function should discover by reading a row of
    // pixels that is not there.
    if (x1 <= x0) return false;
    for (int y = 0; y < g.height; ++y) {
        int ink = 0;
        const std::uint8_t* row = g.data + static_cast<long>(y) * g.row_stride();
        for (int x = x0; x < x1; ++x)
            if (row[x] <= k.ink_max && ++ink >= k.row_min_ink) return true;
    }
    return false;
}

}  // namespace

Result classify(const Grey& g, const Constants& k) {
    Result r;
    if (!g.valid()) return r;

    r.width  = g.width;
    r.height = g.height;

    // -- Test 1: the cheap gate ----------------------------------------------
    // Books are portrait. width >= height is a shape no single page has, with
    // factor-of-two separation and no tolerance to tune. Everything expensive
    // stays behind this comparison, which also means an already-split archive
    // is rejected in one operation per image.
    r.aspect_pass = (g.width >= g.height);
    if (!r.aspect_pass) {
        r.verdict = Verdict::Single;
        r.gutter  = Gutter::None;
        // A blank leaf in an ALREADY-SPLIT archive never reached the type-band
        // scan, so it reported blank == false and nothing downstream could
        // flag it -- on the archive Scott actually has, which is hand-split,
        // that meant no page was ever flagged blank at all. any_ink stops at
        // the first inked row, so the cheap gate stays cheap: the full scan
        // happens only on the pages that are in fact empty.
        r.blank = !any_ink(g, 0, g.width, k);
        return r;
    }

    // -- Test 2: verify a divide at the midline -------------------------------
    const TypeBand band = find_type_band(g, k);
    r.blank = band.empty();
    if (!band.empty()) {
        r.type_top    = band.top;
        r.type_bottom = band.bottom;
    }

    const double mid       = g.width / 2.0;
    const int    half_span = static_cast<int>(std::lround(k.window_frac * g.width));
    const int    win_x0    = std::max(0, static_cast<int>(std::lround(mid)) - half_span);
    const int    win_x1    = std::min(g.width, static_cast<int>(std::lround(mid)) + half_span);

    // Per half, over the regions no cut inside the window can move. See
    // Spread.hpp for why this is asked of the certain region rather than of the
    // resolved halves.
    r.left_blank  = !any_ink(g, 0, win_x0, k);
    r.right_blank = !any_ink(g, win_x1, g.width, k);

    // A blank sheet has no type band to measure through, so every column in the
    // window is trivially clear. That is not a failure: a blank spread is still
    // two blank pages, and DESIGN's page-164 rule needs those pages to exist so
    // the joiner can see across them. Verdict stands, position is unknown, and
    // the folder consensus supplies the cut.
    if (band.empty()) {
        r.verdict     = Verdict::Spread;
        r.gutter      = Gutter::Unmeasurable;
        r.run_x0      = win_x0;
        r.run_x1      = win_x1;
        r.cut_x       = static_cast<int>(std::lround(mid));
        r.offset_frac = 0.0;
        return r;
    }

    // Widest maximal run of clear columns inside the window. Widest rather than
    // nearest, because the binding is the widest thing in the neighbourhood and
    // "nearest to the midline" would prefer an inter-word gap that happened to
    // straddle centre. Ties break toward the midline.
    int best_x0 = 0, best_x1 = 0, best_w = 0;
    double best_dist = 0.0;

    int run_start = -1;
    for (int x = win_x0; x <= win_x1; ++x) {
        const bool clear = (x < win_x1) && column_clear(g, band, x, k);
        if (clear && run_start < 0) {
            run_start = x;
        } else if (!clear && run_start >= 0) {
            const int  w    = x - run_start;
            const double c  = (run_start + x) / 2.0;
            const double d  = std::fabs(c - mid);
            if (w > best_w || (w == best_w && d < best_dist)) {
                best_x0 = run_start; best_x1 = x; best_w = w; best_dist = d;
            }
            run_start = -1;
        }
    }

    const int min_run = static_cast<int>(std::lround(k.min_run_frac * g.width));
    if (best_w < min_run || best_w <= 0) {
        // Aspect said spread, the midline says otherwise. A wide single page --
        // a landscape plate, a map, a two-up sheet already split by hand and
        // saved wide. Honest answer: one page.
        r.verdict = Verdict::Single;
        r.gutter  = Gutter::None;
        return r;
    }

    r.verdict = Verdict::Spread;
    r.run_x0  = best_x0;
    r.run_x1  = best_x1;

    const double centre = (best_x0 + best_x1) / 2.0;
    r.cut_x       = static_cast<int>(std::lround(centre));
    r.offset_frac = (centre - mid) / g.width;

    // The free discriminator. A run touching either window edge was clipped by
    // where we stopped looking, not bounded by ink, so its midpoint carries no
    // information about the binding.
    const bool clipped = (best_x0 <= win_x0) || (best_x1 >= win_x1);
    r.gutter = clipped ? Gutter::Unmeasurable : Gutter::Measured;

    return r;
}

double consensus_offset(const std::vector<Result>& results) {
    std::vector<double> offsets;
    offsets.reserve(results.size());
    for (const auto& r : results)
        if (r.verdict == Verdict::Spread && r.gutter == Gutter::Measured)
            offsets.push_back(r.offset_frac);

    // No measurable gutter anywhere in the folder. The midline is the honest
    // answer -- it is what a cut with no evidence behind it actually is, and
    // saying so plainly beats inventing a number from the unmeasurable ones.
    if (offsets.empty()) return 0.0;

    std::sort(offsets.begin(), offsets.end());
    const std::size_t n = offsets.size();
    if (n % 2 == 1) return offsets[n / 2];
    return (offsets[n / 2 - 1] + offsets[n / 2]) / 2.0;
}

int resolve_cut(const Result& r, double consensus_offset_frac) {
    if (r.width <= 0) return 0;
    if (r.gutter == Gutter::Measured) return r.cut_x;

    const double mid = r.width / 2.0;
    const int    cut = static_cast<int>(std::lround(mid + consensus_offset_frac * r.width));
    return std::clamp(cut, 0, r.width);
}

}  // namespace optik::model::spread
