// selftest_scan.cpp -- spread::, split::, intake:: (carried whole from the
// s23 tree with their tests; the project / review / band sections stayed
// behind with the code they tested). Original header follows.
//
// optik_selftest -- the headless engine harness (DESIGN "headless selftest").
//
// Exercises optik::model with NO display, which is what makes engine
// verification work under "Scott compiles, Claude doesn't": Scott runs this,
// reports the trace, no windowing system needed.
//
// s22 opens the fresh tree with ONE pass in it -- spread::. The harness starts
// small on purpose: every file that arrives here from now on arrives because
// someone named it, not because it was already in the directory.
//
// The fixtures are SYNTHETIC with stated ground truth, and the expected values
// are written down before the run (CANON: prediction before observation). They
// are the starting point, not the proof -- the real net is the 100-image
// Hellboy corpus, which reproduces the numbers in HANDOFF "What was proven".
// The synthetic cases exist to pin the FAILURE MODES that corpus taught us:
// the wrap channel that a free-roaming finder locks onto, and the clipped run
// on a blank half that reports a confident wrong position.
//
//   cmake --build build --target optik_selftest && ./build/optik_selftest

#include "model/Spread.hpp"
#include "model/Split.hpp"
#include "model/Intake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Check.hpp"

namespace spread = optik::model::spread;
namespace split  = optik::model::split;
using optik::test::check;

// -- Fixture builder ----------------------------------------------------------
// An owning white canvas with helpers that lay down text-shaped ink. Text-SHAPED
// matters: a solid rectangle would make every column inked and hide the very
// bug the word-gap case exists to catch, so words are drawn with gaps whose
// positions vary line to line. Deterministic, so a failure reproduces.
struct Canvas {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;

    Canvas(int width, int height) : w(width), h(height), px(std::size_t(width) * height, 255) {}

    spread::Grey view() const { return spread::Grey{px.data(), w, h, w}; }

    void bar(int x0, int y0, int x1, int y1, std::uint8_t v = 20) {
        x0 = std::max(0, x0); y0 = std::max(0, y0);
        x1 = std::min(w, x1); y1 = std::min(h, y1);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                px[std::size_t(y) * w + x] = v;
    }

    // Lines of words between [x0,x1) and [y0,y1). `seed` shifts the word-gap
    // pattern so gaps do not stack into a false vertical channel.
    void text_block(int x0, int y0, int x1, int y1, unsigned seed = 1) {
        const int line_h = 10, leading = 18;
        unsigned s = seed * 2654435761u + 1;
        for (int y = y0; y + line_h <= y1; y += leading) {
            int x = x0;
            while (x < x1) {
                s = s * 1664525u + 1013904223u;
                const int word = 24 + int((s >> 16) % 46);
                bar(x, y, std::min(x + word, x1), y + line_h);
                x += word + 7;
            }
        }
    }

    // ONE line of type spanning [x0,x1). Ink is guaranteed at x0 and at x1-1,
    // so a fixture's STATED edges are the edges the detector must find -- the
    // band tests below assert exact left_edge / right_edge values, and a
    // fixture whose real edge drifted a few px from its stated one would turn
    // every such assertion into a tolerance argument.
    void line_at(int x0, int y, int x1, unsigned seed, int line_h = 10) {
        unsigned s = seed * 2654435761u + 1;
        int x = x0;
        while (x < x1) {
            s = s * 1664525u + 1013904223u;
            const int word = 24 + int((s >> 16) % 46);
            bar(x, y, std::min(x + word, x1), y + line_h);
            x += word + 7;
        }
        bar(std::max(x0, x1 - 6), y, x1, y + line_h);
    }

    // `n` lines of one measure. Returns the y just past the block, so fixtures
    // stack without arithmetic in the test. leading > line_h, or the rows never
    // clear and the projection reads the block as one line.
    int block(int x0, int y, int x1, int n, unsigned seed, int leading = 20) {
        for (int i = 0; i < n; ++i) line_at(x0, y + i * leading, x1, seed + unsigned(i));
        return y + n * leading;
    }
};

// Both halves of a spread, with the gutter centred at `cut`.
static Canvas make_spread(int w, int h, int cut, int gutter_w) {
    Canvas c(w, h);
    const int margin = int(w * 0.045);
    const int top = int(h * 0.09), bot = h - int(h * 0.09);
    c.text_block(margin, top, cut - gutter_w / 2, bot, 11);
    c.text_block(cut + gutter_w / 2, top, w - margin, bot, 29);
    return c;
}

void scan_tests() {
    std::cout << "-- scan: spread, split, intake\n";

    const spread::Constants k;
    const int W = 1528, H = 1112;          // corpus proportions, 0.4 scale
    const double mid = W / 2.0;

    // 1. Test 1 rejects a portrait page without measuring anything. An
    //    already-split archive must cost one comparison per image.
    {
        Canvas c(900, 1400);
        c.text_block(60, 100, 840, 1300, 3);
        const auto r = spread::classify(c.view(), k);
        check("aspect: a portrait page is Single",
              r.verdict == spread::Verdict::Single && !r.aspect_pass);
        check("aspect: a rejected page reports no gutter",
              r.gutter == spread::Gutter::None);
        check("aspect: a page with ink on it is not blank", !r.blank);

        // s23. The aspect gate returned before anything looked for ink, so a
        // blank leaf in an ALREADY-SPLIT archive reported blank == false -- on
        // the archive Scott actually has, which is hand-split, that meant no
        // page could ever be flagged blank at all. The gate stays cheap: the
        // scan stops at the first inked row, so only genuinely empty pages pay
        // for the whole image.
        const Canvas empty(900, 1400);
        const auto er = spread::classify(empty.view(), k);
        check("aspect: a blank portrait leaf is reported blank",
              er.verdict == spread::Verdict::Single && er.blank);

        split::Scan s; s.name = "leaf.png"; s.result = er;
        const auto p = split::plan({s});
        check("aspect: and a single blank page carries the concern",
              p.pages.size() == 1 && (p.pages[0].concerns & split::BlankPage) != 0);
    }

    // 2. A clean spread cut on the midline. Expect Measured, cut within a few
    //    px of centre, offset_frac ~ 0.
    {
        const Canvas c = make_spread(W, H, int(mid), 90);
        const auto r = spread::classify(c.view(), k);
        check("spread: two text blocks with a divide read as Spread",
              r.verdict == spread::Verdict::Spread, std::to_string(r.cut_x));
        check("spread: a run bounded by ink on both sides is Measured",
              r.gutter == spread::Gutter::Measured);
        check("spread: the cut lands on the midline (+/-6px)",
              std::abs(r.cut_x - int(mid)) <= 6, std::to_string(r.cut_x));
        check("spread: offset_frac is ~0 on a centred gutter",
              std::fabs(r.offset_frac) < 0.006, std::to_string(r.offset_frac));
    }

    // 3. The corpus's real case -- no image cuts at exactly 50%. A gutter
    //    +1.5% right of centre must be FOUND there, not snapped to the midpoint.
    {
        const int cut = int(mid + 0.015 * W);
        const Canvas c = make_spread(W, H, cut, 90);
        const auto r = spread::classify(c.view(), k);
        check("offset: an off-centre gutter is found where it is",
              r.gutter == spread::Gutter::Measured && std::abs(r.cut_x - cut) <= 6,
              std::to_string(r.cut_x) + " want " + std::to_string(cut));
        check("offset: offset_frac reports the book's real offset",
              std::fabs(r.offset_frac - 0.015) < 0.006, std::to_string(r.offset_frac));
    }

    // 4. THE WRAP-CHANNEL REGRESSION. The right page has a clear channel at its
    //    inner edge where an illustration was removed -- wider than the gutter,
    //    inside the search window, and clear for most of its height. It is
    //    interrupted by five full-measure lines in the middle band, and those
    //    five lines are the entire difference between a real gutter and a fake
    //    one. A "mostly white" test picks the channel and cuts in the wrong
    //    place; the full-height rule rejects it.
    {
        const int cut = int(mid);
        const int gw  = 80;
        Canvas c(W, H);
        const int margin = int(W * 0.045);
        const int top = int(H * 0.09), bot = H - int(H * 0.09);
        const int r0 = cut + gw / 2;                 // right page left edge
        const int chan_w = 200;                      // wider than the gutter
        // Left page: ordinary.
        c.text_block(margin, top, cut - gw / 2, bot, 11);
        // Right page: text wraps to the right of the channel...
        c.text_block(r0 + chan_w, top, W - margin, bot, 29);
        // ...except for five full-measure lines in the middle band.
        const int band_y = (top + bot) / 2;
        c.text_block(r0, band_y, W - margin, band_y + 5 * 18, 47);

        const auto r = spread::classify(c.view(), k);
        check("wrap: the image still reads as Spread",
              r.verdict == spread::Verdict::Spread);
        check("wrap: the cut is the gutter, NOT the wider illustration channel",
              std::abs(r.cut_x - cut) <= 8,
              "cut=" + std::to_string(r.cut_x) + " gutter=" + std::to_string(cut) +
              " channel=" + std::to_string(r0 + chan_w / 2));
        check("wrap: the channel never forms a run, so the gutter is Measured",
              r.gutter == spread::Gutter::Measured);
    }

    // 5. A blank half. The run has no ink to bound it, runs off into the empty
    //    half, and is clipped by the window. Verdict stands; position is not
    //    trusted. This is the case that would otherwise report a confident cut
    //    ~150px off true centre.
    {
        Canvas c(W, H);
        const int margin = int(W * 0.045);
        c.text_block(int(mid) + 60, int(H * 0.09), W - margin, H - int(H * 0.09), 29);
        const auto r = spread::classify(c.view(), k);
        check("blank half: the verdict is still Spread",
              r.verdict == spread::Verdict::Spread);
        check("blank half: a run touching the window edge is Unmeasurable",
              r.gutter == spread::Gutter::Unmeasurable);

        // s23, THE DEFECT THE FIRST REAL CORPUS RUN FOUND. `blank` is a fact
        // about the SHEET; this is the question about the PAGE, and until now
        // nothing asked it. Nine sheets in the Hellboy corpus have a blank half
        // -- page 164 among them, the page DESIGN's transparency rule is built
        // on -- and not one was ever flagged, because the flag it needed could
        // only fire when the WHOLE sheet was empty.
        check("blank half: the empty half is the one reported empty",
              r.left_blank && !r.right_blank);
        check("blank half: a sheet with ink on it is not a blank sheet",
              !r.blank);

        // And the flag has to survive the trip through the planner onto the
        // page it is about -- which is where it was being lost.
        split::Scan s; s.name = "one.png"; s.result = r;
        const auto p = split::plan({s});
        check("blank half: the concern lands on that half and not its neighbour",
              p.pages.size() == 2 &&
                  (p.pages[0].concerns & split::BlankPage) != 0 &&
                  (p.pages[1].concerns & split::BlankPage) == 0);

        // Speckle is not ink. The floor is row_min_ink, reused rather than
        // reinvented, so a dust hair on an empty leaf does not make it a page.
        Canvas d(W, H);
        const int dm = int(W * 0.045);
        d.text_block(int(mid) + 60, int(H * 0.09), W - dm, H - int(H * 0.09), 29);
        d.bar(120, 300, 121, 301);
        d.bar(400, 700, 401, 701);
        check("blank half: two specks do not make an empty half inked",
              spread::classify(d.view(), k).left_blank);

        // The MIRROR, and it is not symmetry for its own sake: on the corpus
        // the blank halves land on both sides (five odd pages, three even), so
        // a rule that only ever sees an empty left half is a rule half the
        // book's holes walk straight past. A mutation swapping the right half's
        // question for the left's was invisible until this existed.
        Canvas rb(W, H);
        rb.text_block(int(W * 0.045), int(H * 0.09), int(mid) - 60, H - int(H * 0.09), 11);
        const auto rr = spread::classify(rb.view(), k);
        check("blank half: an empty RIGHT half is reported on the right",
              !rr.left_blank && rr.right_blank);
        split::Scan rs; rs.name = "two.png"; rs.result = rr;
        const auto rp2 = split::plan({rs});
        check("blank half: and the concern lands on the second page, not the first",
              rp2.pages.size() == 2 &&
                  (rp2.pages[0].concerns & split::BlankPage) == 0 &&
                  (rp2.pages[1].concerns & split::BlankPage) != 0);

        // A SPARSE half is not a blank one, and the difference is the whole
        // value of the flag. A__0000 is a title page: little ink, unmeasurable
        // gutter, and absolutely not empty.
        Canvas e(W, H);
        e.text_block(int(mid) + 60, int(H * 0.09), W - dm, H - int(H * 0.09), 29);
        e.text_block(int(W * 0.30), int(H * 0.40), int(W * 0.42), int(H * 0.46), 7);
        const auto er = spread::classify(e.view(), k);
        check("blank half: a sparse half is not an empty one",
              !er.left_blank && !er.right_blank);
    }

    // 6. A wholly blank sheet. Not an error and not a Single: two blank pages
    //    still have to exist, or the joiner cannot see across them (page 164).
    {
        const Canvas c(W, H);
        const auto r = spread::classify(c.view(), k);
        check("blank sheet: reads as Spread with the blank flag set",
              r.verdict == spread::Verdict::Spread && r.blank);
        check("blank sheet: both halves report empty, so both pages are flagged",
              r.left_blank && r.right_blank);
        check("blank sheet: position is Unmeasurable, not a confident midline",
              r.gutter == spread::Gutter::Unmeasurable);
    }

    // 7. A wide single page -- a landscape plate, or a two-up sheet already
    //    split by hand. Aspect passes, the midline does not. Honest: one page.
    {
        Canvas c(W, H);
        c.text_block(int(W * 0.045), int(H * 0.09), W - int(W * 0.045), H - int(H * 0.09), 5);
        const auto r = spread::classify(c.view(), k);
        check("wide single: aspect passes but no divide -> Single",
              r.aspect_pass && r.verdict == spread::Verdict::Single,
              "cut=" + std::to_string(r.cut_x));
    }

    // 8. Consensus. The gutter is a BOOK constant -- one scanner setup holding
    //    steady -- so the folder's median is what an unmeasurable image gets.
    //    A median, not a mean, so one near-miss cannot drag the book.
    {
        std::vector<spread::Result> rs;
        auto measured = [&](double off) {
            spread::Result r;
            r.verdict = spread::Verdict::Spread;
            r.gutter  = spread::Gutter::Measured;
            r.width   = W;
            r.offset_frac = off;
            r.cut_x = int(std::lround(mid + off * W));
            rs.push_back(r);
        };
        measured(0.009); measured(0.010); measured(0.011);
        measured(0.0095); measured(0.120);          // one wild near-miss

        const double cons = spread::consensus_offset(rs);
        check("consensus: the median ignores a wild outlier",
              std::fabs(cons - 0.010) < 0.0005, std::to_string(cons));

        spread::Result unk;
        unk.verdict = spread::Verdict::Spread;
        unk.gutter  = spread::Gutter::Unmeasurable;
        unk.width   = W;
        unk.cut_x   = 40;                            // the clipped, wrong value
        const int cut = spread::resolve_cut(unk, cons);
        check("consensus: an Unmeasurable image is cut at the book's offset",
              std::abs(cut - int(std::lround(mid + cons * W))) <= 1, std::to_string(cut));
        check("consensus: resolve_cut ignores the clipped run's own midpoint",
              cut != unk.cut_x, std::to_string(cut));

        spread::Result meas = rs[0];
        check("consensus: a Measured image keeps its OWN cut, not the book's",
              spread::resolve_cut(meas, cons) == meas.cut_x);

        check("consensus: no measurable gutter anywhere falls back to the midline",
              std::fabs(spread::consensus_offset({})) < 1e-12);
    }

    // -- Below: fixtures that exist because a MUTATION SURVIVED without them.
    // Each one pins a constant or a tie-break that the cases above passed
    // through indifferently. A constant nothing can break is a constant nobody
    // chose (RULES: a surviving mutation is a test failure).

    // 9. The full-height rule at its strongest. A channel interrupted by ONE
    //    line is ~1% inked, so every "mostly white" tolerance worth the name
    //    passes it -- and picks a 200px channel over an 80px gutter. One line
    //    of ink through a column means it is not a binding.
    {
        const int cut = int(mid), gw = 80;
        Canvas c(W, H);
        const int margin = int(W * 0.045);
        const int top = int(H * 0.09), bot = H - int(H * 0.09);
        const int r0 = cut + gw / 2, chan_w = 200;
        c.text_block(margin, top, cut - gw / 2, bot, 11);
        c.text_block(r0 + chan_w, top, W - margin, bot, 29);
        c.text_block(r0, (top + bot) / 2, W - margin, (top + bot) / 2 + 10, 47);  // ONE line
        const auto r = spread::classify(c.view(), k);
        check("full-height: a single interrupting line disqualifies a channel",
              r.gutter == spread::Gutter::Measured && std::abs(r.cut_x - cut) <= 8,
              "cut=" + std::to_string(r.cut_x) + " gutter=" + std::to_string(cut));
    }

    // 10. The run floor. A 12px gap between two blocks of text on a wide single
    //     sheet is a wide word space, not a binding. Without a floor it reads
    //     as a spread and silently halves a page.
    {
        Canvas c(W, H);
        const int margin = int(W * 0.045);
        const int top = int(H * 0.09), bot = H - int(H * 0.09);
        c.text_block(margin, top, int(mid) - 6, bot, 5);
        c.text_block(int(mid) + 6, top, W - margin, bot, 7);
        const auto r = spread::classify(c.view(), k);
        check("run floor: a hairline gap is not a gutter",
              r.verdict == spread::Verdict::Single,
              "cut=" + std::to_string(r.cut_x) +
              " run=" + std::to_string(r.run_x1 - r.run_x0));
    }

    // 11. col_min_ink. One speckle in the gutter must not bridge it. At a
    //     threshold of 1 the run splits in two and the cut moves to the middle
    //     of one half.
    {
        Canvas c = make_spread(W, H, int(mid), 90);
        c.bar(int(mid), H / 2, int(mid) + 1, H / 2 + 1);      // exactly one pixel
        const auto r = spread::classify(c.view(), k);
        check("col_min_ink: one speckle does not split the gutter",
              r.gutter == spread::Gutter::Measured && std::abs(r.cut_x - int(mid)) <= 6,
              "cut=" + std::to_string(r.cut_x));
    }

    // 12. row_min_ink. Scanner dirt in the top margin, inside the gutter's
    //     x-range and isolated enough that no row reaches 3 ink pixels. At a
    //     threshold of 1 the type band swallows the margin, the dirt lands in a
    //     gutter column, and the run splits.
    {
        Canvas c = make_spread(W, H, int(mid), 90);
        const int dirt_y = int(H * 0.03);                     // above the type block
        for (int i = 0; i < 4; ++i) c.bar(int(mid), dirt_y + i * 2, int(mid) + 1, dirt_y + i * 2 + 1);
        const auto r = spread::classify(c.view(), k);
        check("row_min_ink: margin dirt stays outside the type band",
              r.gutter == spread::Gutter::Measured && std::abs(r.cut_x - int(mid)) <= 6,
              "cut=" + std::to_string(r.cut_x) +
              " band=" + std::to_string(r.type_top) + ".." + std::to_string(r.type_bottom));
    }

    // 13. Widest, not nearest. A removed vertical illustration on the left
    //     page's inner edge leaves a narrower full-height channel that sits
    //     CLOSER to the midline than the real gutter. Both are legitimate
    //     runs, so the full-height rule cannot arbitrate -- the tie-break does.
    //     The binding is the widest thing in the neighbourhood; the corpus's
    //     narrowest measured gutter (6.4% of width) still dwarfs an incidental
    //     channel.
    {
        const int cut = int(mid) + 120, gw = 90;
        Canvas c(W, H);
        const int margin = int(W * 0.045);
        const int top = int(H * 0.09), bot = H - int(H * 0.09);
        c.text_block(margin, top, int(mid) - 40, bot, 11);         // left of channel
        c.text_block(int(mid) + 20, top, cut - gw / 2, bot, 13);   // between channel and gutter
        c.text_block(cut + gw / 2, top, W - margin, bot, 29);      // right page
        const auto r = spread::classify(c.view(), k);
        check("tie-break: the WIDEST run wins, not the nearest to centre",
              std::abs(r.cut_x - cut) <= 8,
              "cut=" + std::to_string(r.cut_x) + " gutter=" + std::to_string(cut) +
              " channel=" + std::to_string(int(mid) - 10));
        check("tie-break: that run is still bounded on both sides",
              r.gutter == spread::Gutter::Measured);
    }

    // 14. Consensus must draw only on Measured results. Feeding it the
    //     unmeasurable ones re-imports exactly the clipped positions the
    //     Unmeasurable flag exists to quarantine.
    {
        std::vector<spread::Result> rs;
        auto add = [&](double off, spread::Gutter g) {
            spread::Result r;
            r.verdict = spread::Verdict::Spread;
            r.gutter  = g;
            r.width   = W;
            r.offset_frac = off;
            rs.push_back(r);
        };
        add(0.009, spread::Gutter::Measured);
        add(0.010, spread::Gutter::Measured);
        add(0.011, spread::Gutter::Measured);
        add(-0.140, spread::Gutter::Unmeasurable);
        add(-0.130, spread::Gutter::Unmeasurable);
        add(-0.120, spread::Gutter::Unmeasurable);
        const double cons = spread::consensus_offset(rs);
        check("consensus: unmeasurable positions are quarantined, not averaged in",
              std::fabs(cons - 0.010) < 0.0005, std::to_string(cons));
    }

    // ==== split:: -- phase 2, the page sequence ==============================

    {
        using namespace optik::model;
        const split::Constants sk;

        auto sp = [&](const std::string& name, double off, spread::Gutter g,
                      int w = 1528, int h = 1112) {
            split::Scan s;
            s.name = name;
            s.result.verdict     = spread::Verdict::Spread;
            s.result.gutter      = g;
            s.result.width       = w;
            s.result.height      = h;
            s.result.offset_frac = off;
            s.result.cut_x       = int(std::lround(w / 2.0 + off * w));
            return s;
        };
        auto sg = [&](const std::string& name, int w = 900, int h = 1400) {
            split::Scan s;
            s.name = name;
            s.result.verdict = spread::Verdict::Single;
            s.result.width   = w;
            s.result.height  = h;
            return s;
        };

        // 15. Numbering. Image i yields 2i+1 and 2i+2; the sequence is
        //     contiguous 1..N with no gaps.
        {
            const auto p = split::plan({sp("a1", 0.01, spread::Gutter::Measured),
                                        sp("a2", 0.01, spread::Gutter::Measured),
                                        sp("a3", 0.01, spread::Gutter::Measured)}, sk);
            bool contiguous = p.pages.size() == 6;
            for (std::size_t i = 0; contiguous && i < p.pages.size(); ++i)
                contiguous = (p.pages[i].number == i + 1);
            check("split: 3 spreads -> 6 contiguous pages", contiguous,
                  std::to_string(p.pages.size()));
            check("split: the left half is the earlier page",
                  p.pages[0].half == split::Half::Left &&
                  p.pages[1].half == split::Half::Right);
        }

        // 16. Mixed input is the loop, not a special case. Singles contribute
        //     one, spreads two, the running count carries.
        {
            const auto p = split::plan({sp("b1", 0.01, spread::Gutter::Measured),
                                        sg("b2"),
                                        sp("b3", 0.01, spread::Gutter::Measured)}, sk);
            check("split: mixed input numbers by construction (2+1+2)",
                  p.pages.size() == 5 && p.pages[2].half == split::Half::Whole &&
                  p.pages[2].number == 3 && p.pages[4].number == 5,
                  std::to_string(p.pages.size()));
            check("split: a single page crops to the whole image",
                  p.pages[2].x0 == 0 && p.pages[2].x1 == 900 &&
                  p.pages[2].y0 == 0 && p.pages[2].y1 == 1400);
        }

        // 17. THE TILING PROPERTY. The two halves must cover the source
        //     exactly -- no dropped column, no overlap. A one-pixel gap here is
        //     a sliver of a glyph that vanishes from the book, and nothing
        //     downstream would ever report it.
        {
            const auto p = split::plan({sp("c1", 0.031, spread::Gutter::Measured)}, sk);
            const auto& l = p.pages[0];
            const auto& r = p.pages[1];
            check("split: halves tile the source exactly, no gap or overlap",
                  l.x0 == 0 && l.x1 == r.x0 && r.x1 == 1528 &&
                  l.width() + r.width() == 1528,
                  std::to_string(l.x0) + ".." + std::to_string(l.x1) + " | " +
                  std::to_string(r.x0) + ".." + std::to_string(r.x1));
            check("split: the gutter is not trimmed -- both halves keep full height",
                  l.y0 == 0 && l.y1 == 1112 && r.y0 == 0 && r.y1 == 1112);
            check("split: an off-centre gutter cuts off centre",
                  std::abs(l.x1 - int(1528 / 2.0 + 0.031 * 1528)) <= 1,
                  std::to_string(l.x1));
        }

        // 18. Natural order. Unpadded filenames must not interleave the book.
        //     Lexicographic order puts scan_10 before scan_2 and the failure is
        //     silent -- it reads as bad OCR, not as a sorting bug.
        {
            check("natural order: scan_2 precedes scan_10",
                  split::natural_less("scan_2.png", "scan_10.png"));
            check("natural order: it is not merely lexicographic",
                  !split::natural_less("scan_10.png", "scan_2.png"));
            check("natural order: zero padding does not change the sequence",
                  split::natural_less("p007.png", "p0010.png"));
            const auto p = split::plan({sg("scan_10.png"), sg("scan_2.png"),
                                        sg("scan_1.png")}, sk);
            check("split: the folder is sorted before it is numbered",
                  p.pages[0].source_name == "scan_1.png" &&
                  p.pages[1].source_name == "scan_2.png" &&
                  p.pages[2].source_name == "scan_10.png",
                  p.pages[0].source_name + "," + p.pages[1].source_name + "," +
                  p.pages[2].source_name);
        }

        // 19. Provenance. "Which scan did page 47 come from, and which half?"
        //     is asked the moment a split looks wrong. s7's removal of
        //     source_name assumed one image = one page.
        {
            const auto p = split::plan({sp("A__0007.png", 0.01, spread::Gutter::Measured)}, sk);
            check("split: a page records its source scan and which half it is",
                  p.pages[1].source_name == "A__0007.png" &&
                  p.pages[1].half == split::Half::Right &&
                  p.pages[1].number == 2);
        }

        // 20. A verdict change renumbers the TAIL. This is what makes 2b an
        //     intake gate rather than an edit: cheap now, corrupting once
        //     corrections are keyed to page numbers.
        {
            std::vector<split::Scan> in = {sp("d1", 0.01, spread::Gutter::Measured),
                                           sp("d2", 0.01, spread::Gutter::Measured),
                                           sp("d3", 0.01, spread::Gutter::Measured)};
            const auto before = split::plan(in, sk);
            in[0].result.verdict = spread::Verdict::Single;      // the author's override
            const auto after = split::plan(in, sk);
            check("split: overriding one verdict renumbers everything after it",
                  before.pages.size() == 6 && after.pages.size() == 5 &&
                  after.pages.back().source_name == "d3" &&
                  after.pages.back().number == 5,
                  std::to_string(after.pages.size()));
        }

        // 21. The consensus fallback, end to end through the plan.
        {
            const auto p = split::plan({sp("e1", 0.010, spread::Gutter::Measured),
                                        sp("e2", 0.010, spread::Gutter::Measured),
                                        sp("e3", 0.010, spread::Gutter::Measured),
                                        sp("e4", -0.090, spread::Gutter::Unmeasurable)}, sk);
            check("split: consensus is computed over the folder before any cut",
                  std::fabs(p.consensus_offset - 0.010) < 0.0005,
                  std::to_string(p.consensus_offset));
            check("split: an unmeasurable image is cut at the book's offset",
                  std::abs(p.pages[6].x1 - int(std::lround(1528 / 2.0 + 0.010 * 1528))) <= 1,
                  std::to_string(p.pages[6].x1));
            check("split: the clipped run's own midpoint is never used",
                  p.pages[6].x1 != int(std::lround(1528 / 2.0 - 0.090 * 1528)));
            check("split: it is flagged for the 2b gate",
                  (p.pages[6].concerns & split::GutterFromConsensus) != 0 &&
                  (p.pages[0].concerns & split::GutterFromConsensus) == 0);
        }

        // 22. A measured gutter that disagrees with the book. Cut on its own
        //     measurement -- it measured something -- but shown to the author.
        {
            const auto p = split::plan({sp("f1", 0.010, spread::Gutter::Measured),
                                        sp("f2", 0.010, spread::Gutter::Measured),
                                        sp("f3", 0.010, spread::Gutter::Measured),
                                        sp("f4", 0.055, spread::Gutter::Measured)}, sk);
            check("split: a measured outlier is flagged, not overridden",
                  (p.pages[6].concerns & split::OffsetOutlier) != 0 &&
                  std::abs(p.pages[6].x1 - int(std::lround(1528 / 2.0 + 0.055 * 1528))) <= 1,
                  std::to_string(p.pages[6].x1));
            check("split: images agreeing with the book are not flagged",
                  p.pages[0].concerns == split::ConcernNone);
            // A concern belongs to the CUT, so it belongs to both pages the cut
            // produced. Flagging only the left half sends the author to review
            // one page of a two-page problem.
            check("split: a cut's concern reaches BOTH halves it produced",
                  p.pages[6].concerns == p.pages[7].concerns &&
                  (p.pages[7].concerns & split::OffsetOutlier) != 0);
        }

        // 22b. Ordinary scanner variation must not trip the outlier flag. The
        //      corpus held a 0.46% standard deviation across 100 sheets; a
        //      threshold tight enough to fire on that is a threshold that
        //      flags the whole book, which is the same as flagging none of it.
        {
            const auto p = split::plan({sp("j1", 0.006, spread::Gutter::Measured),
                                        sp("j2", 0.010, spread::Gutter::Measured),
                                        sp("j3", 0.014, spread::Gutter::Measured)}, sk);
            check("split: normal scanner drift raises no outlier flags",
                  p.flagged == 0, std::to_string(p.flagged));
        }

        // 23. Minority verdicts. One single in a folder of spreads is either a
        //     cover or a missed gutter, and the two are identical from here.
        {
            std::vector<split::Scan> in;
            for (int i = 0; i < 9; ++i)
                in.push_back(sp("g" + std::to_string(i), 0.01, spread::Gutter::Measured));
            in.push_back(sg("g9"));
            const auto p = split::plan(in, sk);
            check("split: a minority verdict is flagged for review",
                  (p.pages.back().concerns & split::VerdictOutlier) != 0 &&
                  p.pages.back().half == split::Half::Whole);
            check("split: the majority is not flagged",
                  (p.pages[0].concerns & split::VerdictOutlier) == 0);
        }

        // 24. Genuinely mixed input flags nothing. A flag that fires on normal
        //     input is a flag the author learns to ignore.
        {
            std::vector<split::Scan> in;
            for (int i = 0; i < 5; ++i)
                in.push_back(sp("h" + std::to_string(i), 0.01, spread::Gutter::Measured));
            for (int i = 5; i < 10; ++i)
                in.push_back(sg("h" + std::to_string(i)));
            const auto p = split::plan(in, sk);
            check("split: a half-and-half folder raises no verdict flags",
                  p.flagged == 0, std::to_string(p.flagged));
        }

        // 25. Blanks are carried, not dropped. DESIGN's page-164 rule needs the
        //     page to exist so the joiner can see across it.
        {
            auto b = sp("i1", 0.01, spread::Gutter::Unmeasurable);
            b.result.blank = true;
            b.result.left_blank = b.result.right_blank = true;
            const auto p = split::plan({b}, sk);
            check("split: a blank sheet still produces two numbered pages",
                  p.pages.size() == 2 &&
                  (p.pages[0].concerns & split::BlankPage) != 0);
        }

        // 26. Degenerate input.
        {
            const auto p = split::plan({}, sk);
            check("split: an empty folder plans an empty book",
                  p.pages.empty() && p.spreads == 0 && p.singles == 0 &&
                  std::fabs(p.consensus_offset) < 1e-12);
        }
    }

    // ==== intake:: -- discovery, and the end of silent zeroes ================

    {
        using namespace optik::model;
        namespace fs = std::filesystem;

        const fs::path root = fs::temp_directory_path() / "optik_selftest_intake";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "session_2", ec);
        fs::create_directories(root / ".thumbnails", ec);

        auto touch = [](const fs::path& p) { std::ofstream f(p.string()); f << "x"; };

        touch(root / "scan_10.png");
        touch(root / "scan_2.png");
        touch(root / "scan_1.PNG");          // case-insensitive extension
        touch(root / "notes.txt");
        touch(root / "raw_a.arw");
        touch(root / "raw_b.arw");
        touch(root / ".DS_Store");
        touch(root / "session_2" / "scan_3.jpg");   // nested, and a JPEG
        touch(root / ".thumbnails" / "scan_1.png"); // sidecar clutter

        // 27. Recursive, and JPEG accepted. Both were bugs in s21: a folder of
        //     JPEGs or scans one level down intook ZERO pages, silently.
        {
            const auto r = intake::discover(root.string());
            bool has_nested = false, has_upper = false;
            for (const auto& f : r.scans) {
                if (f.name == "scan_3.jpg") has_nested = true;
                if (f.name == "scan_1.PNG") has_upper = true;
            }
            check("intake: subfolders are walked", has_nested);
            check("intake: JPEG is in the accepted set", has_nested);
            check("intake: extensions match case-insensitively", has_upper);
            check("intake: four scans found, the rest passed over",
                  r.scans.size() == 4, std::to_string(r.scans.size()));
        }

        // 28. Dotfiles and dot-directories are clutter, not scans. The
        //     .thumbnails copy of scan_1 would otherwise duplicate a page.
        {
            const auto r = intake::discover(root.string());
            bool from_dotdir = false;
            for (const auto& f : r.scans)
                if (f.path.find(".thumbnails") != std::string::npos) from_dotdir = true;
            check("intake: a dot-directory subtree is skipped whole", !from_dotdir);
        }

        // 29. Natural order across the whole walk.
        {
            const auto r = intake::discover(root.string());
            check("intake: scans come back in natural order",
                  r.scans.size() == 4 && r.scans[0].name == "scan_1.PNG" &&
                  r.scans[1].name == "scan_2.png" && r.scans[2].name == "scan_10.png",
                  r.scans.empty() ? "" : r.scans[0].name + "," + r.scans[1].name);
        }

        // 30. Non-recursive on request.
        {
            intake::Options o; o.recursive = false;
            const auto r = intake::discover(root.string(), o);
            check("intake: recursion is optional, not assumed",
                  r.scans.size() == 3, std::to_string(r.scans.size()));
        }

        // 31. THE DISTINCTION THAT MATTERS. "Zero pages" and "zero pages
        //     because you pointed me at the wrong folder" are the same value
        //     and completely different facts. ok() answers whether the question
        //     was answerable, never whether the answer was zero.
        {
            const auto missing = intake::discover((root / "no_such_folder").string());
            check("intake: a missing folder is not ok()", !missing.ok());
            check("intake: and it says so", missing.explain().find("does not exist") != std::string::npos,
                  missing.explain());

            const fs::path raws = root / "raws_only";
            fs::create_directories(raws, ec);
            touch(raws / "a.arw"); touch(raws / "b.arw"); touch(raws / "c.cr2");
            const auto r = intake::discover(raws.string());
            check("intake: a readable folder with no scans is still ok()",
                  r.ok() && r.scans.empty());
            check("intake: it reports what it looked at and why each lost",
                  r.files_seen == 3 && r.skipped.size() == 3 &&
                  r.explain().find("3 file") != std::string::npos &&
                  r.explain().find(".arw") != std::string::npos,
                  r.explain());

            const fs::path empty = root / "empty";
            fs::create_directories(empty, ec);
            const auto e = intake::discover(empty.string());
            check("intake: an empty folder says empty, not 'no scans'",
                  e.ok() && e.explain().find("empty") != std::string::npos, e.explain());
        }

        fs::remove_all(root, ec);
    }

}
