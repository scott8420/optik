#include "model/Split.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace optik::model::split {
namespace {

bool is_digit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

bool natural_less(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            // Compare the whole digit runs as numbers. Leading zeros are
            // skipped for the comparison but the run lengths break a tie, so
            // "007" and "7" order stably instead of comparing equal.
            std::size_t ai = i, bj = j;
            while (ai < a.size() && a[ai] == '0') ++ai;
            while (bj < b.size() && b[bj] == '0') ++bj;

            std::size_t ae = ai, be = bj;
            while (ae < a.size() && is_digit(a[ae])) ++ae;
            while (be < b.size() && is_digit(b[be])) ++be;

            const std::size_t alen = ae - ai, blen = be - bj;
            if (alen != blen) return alen < blen;
            for (std::size_t n = 0; n < alen; ++n)
                if (a[ai + n] != b[bj + n]) return a[ai + n] < b[bj + n];

            i = ae; j = be;
            continue;
        }
        if (a[i] != b[j]) return a[i] < b[j];
        ++i; ++j;
    }
    return (a.size() - i) < (b.size() - j);
}

Plan plan(std::vector<Scan> scans, const Constants& k) {
    Plan out;

    // Sort here rather than trusting the caller. Input filenames carry no
    // meaning past sort order, which makes sort order load-bearing -- and a
    // load-bearing property enforced by convention is enforced by nobody.
    std::sort(scans.begin(), scans.end(),
              [](const Scan& a, const Scan& b) { return natural_less(a.name, b.name); });

    for (const auto& s : scans)
        (s.result.verdict == spread::Verdict::Spread ? out.spreads : out.singles)++;

    // The consensus is computed over the whole folder BEFORE any page is
    // emitted, because an image whose own gutter is unmeasurable is cut at the
    // book's offset -- and the book's offset is not knowable until every image
    // has been asked. This is the second half of the two-level pass, and it is
    // why this cannot run per-file.
    std::vector<spread::Result> results;
    results.reserve(scans.size());
    for (const auto& s : scans) results.push_back(s.result);
    out.consensus_offset = spread::consensus_offset(results);

    const std::size_t total = scans.size();
    const bool spreads_are_majority = out.spreads >= out.singles;
    const std::size_t minority_count =
        static_cast<std::size_t>(std::floor(k.verdict_minority_frac * double(total)));

    std::size_t number = 1;
    for (const auto& s : scans) {
        const spread::Result& r = s.result;

        unsigned shared = ConcernNone;
        // BlankPage is deliberately NOT shared any more. It is the one concern
        // that is about a PAGE rather than about the cut that made it: two
        // halves of one sheet can disagree about whether they hold ink, and
        // sharing it meant they never could. See Spread.hpp.
        // The verdict guard is provably redundant -- a sheet with no ink
        // anywhere has none in either half either, so a spread would get the
        // flag from the halves regardless, and a sweep confirmed the two are
        // indistinguishable. It stays because it says which path this line is
        // for, and the next person to add a concern here should be able to see
        // that "shared" means shared BY THE CUT, not by the sheet.
        if (r.verdict != spread::Verdict::Spread && r.blank) shared |= BlankPage;

        // A verdict is a minority report when the OTHER verdict dominates the
        // folder. A folder of genuinely mixed input flags nothing, which is
        // right: mixed is normal, and a flag that fires on normal input is a
        // flag the author learns to ignore.
        const bool is_spread = (r.verdict == spread::Verdict::Spread);
        const std::size_t own = is_spread ? out.spreads : out.singles;
        if (total > 0 && own <= minority_count && is_spread != spreads_are_majority)
            shared |= VerdictOutlier;

        if (!is_spread) {
            PageSource p;
            p.number      = number++;
            p.source_name = s.name;
            p.half        = Half::Whole;
            p.x0 = 0; p.y0 = 0; p.x1 = r.width; p.y1 = r.height;
            p.concerns    = shared;
            out.pages.push_back(p);
            continue;
        }

        const int cut = spread::resolve_cut(r, out.consensus_offset);

        unsigned sc = shared;
        if (r.gutter != spread::Gutter::Measured) {
            sc |= GutterFromConsensus;
        } else if (std::fabs(r.offset_frac - out.consensus_offset) > k.offset_outlier_frac) {
            sc |= OffsetOutlier;
        }

        // Handedness: the left half is the earlier page. Free sanity check, and
        // it held on the corpus -- one page ended "...The people there probably"
        // and its right half opened "wouldn't have known...".
        PageSource l;
        l.number = number++;
        l.source_name = s.name;
        l.half = Half::Left;
        l.x0 = 0; l.y0 = 0; l.x1 = cut; l.y1 = r.height;
        l.concerns = sc | (r.left_blank ? unsigned(BlankPage) : ConcernNone);

        PageSource rp;
        rp.number = number++;
        rp.source_name = s.name;
        rp.half = Half::Right;
        rp.x0 = cut; rp.y0 = 0; rp.x1 = r.width; rp.y1 = r.height;
        rp.concerns = sc | (r.right_blank ? unsigned(BlankPage) : ConcernNone);

        out.pages.push_back(l);
        out.pages.push_back(rp);
    }

    for (const auto& p : out.pages)
        if (p.flagged()) ++out.flagged;

    return out;
}

}  // namespace optik::model::split
