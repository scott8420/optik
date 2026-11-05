#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Spread (s22 -- pipeline phase 1: "one page or two?").
//
// DESIGN "Phases 1-2 -- input variety". The image is not the page. A scan may
// hold one page or two, and that is determined PER IMAGE, never assumed per
// folder. A wrong assumption does not fail loudly -- it interleaves two pages
// of prose into nonsense that reads like bad OCR.
//
// Scott's test, which is the architecture:
//   1) Is the width of the image as wide as the height or more?
//   2) If split in 2 halves, is there a vertical divide between the text blocks?
//
// Step 1 is the cheap gate -- books are portrait, so width >= height is a shape
// no single page has. Factor-of-two separation, nothing to tune. Step 2 is the
// load-bearing improvement over hunting a trough ANYWHERE in the image: a
// removed illustration at the inner edge opens a white channel adjacent to the
// gutter, and a free-roaming finder locks onto it. Anchored at the midline that
// channel is noise NEAR the answer instead of a competing answer.
//
//   A binding is at the center because that is what a spread is.
//   You do not search for it; you verify it.
//
// NO LEPTONICA HERE, and that is deliberate. The verdict is arithmetic over an
// 8-bit greyscale plane -- row ink counts, column ink counts, run detection.
// Decoding a PNG into that plane is image::'s job. Keeping the seam here means
// the load-bearing logic is provable with zero image libraries, which is what
// "numbers before widgets" asks for and what lets the selftest carry it.
//
// The pass is TWO-LEVEL and cannot be collapsed into one:
//   classify()  -- per image, independent, parallelisable. Verdict + position.
//   consensus() -- per FOLDER, over all classify results. The gutter is a book
//                  constant (one scanner setup), so images whose own position
//                  is unmeasurable are cut at the book's offset.
// "Position detects, consensus confirms" (CANON), applied one level up.
namespace optik::model::spread {

// -- The named constants block ------------------------------------------------
// HANDOFF s22: these worked across all 100 images of the Hellboy corpus and are
// the numbers most likely to want tuning on a different book. They live in ONE
// named struct rather than scattered through the code, so a book that argues
// with them is a settings change and not a code hunt.
struct Constants {
    // Ink threshold. A pixel <= this is ink. The engine always reads
    // black-on-white (DESIGN bedrock rule), so this needs no polarity branch.
    std::uint8_t ink_max = 200;

    // A row counts as inked if it holds at least this many ink pixels. Sets the
    // vertical extent of the type block, which is what "full height" is
    // measured against. 3 rejects scanner speckle without rejecting a rule or
    // an em dash.
    int row_min_ink = 3;

    // A column counts as inked if it holds at least this many ink pixels WITHIN
    // the type band. 2 rather than 1 so a single stray pixel cannot bridge an
    // otherwise clear gutter.
    int col_min_ink = 2;

    // The search window, as a fraction of width either side of the midline. No
    // image on the corpus cut exactly at 50% (measured -5.5% .. +5.9%), so the
    // divide is searched for in a window -- but a window, not the whole image.
    // 0.15 covered every case with room.
    double window_frac = 0.15;

    // Floor on run width, as a fraction of image width, before a clear run is
    // taken seriously. Observed minimum on the corpus was 6.4%; this sits well
    // below that and exists only to reject a hairline.
    double min_run_frac = 0.02;
};

// -- Results ------------------------------------------------------------------

// What the image holds. Spread requires BOTH of Scott's tests to pass.
enum class Verdict { Single, Spread };

// What is known about WHERE the binding is. Distinct from the verdict on
// purpose: nine corpus images are certainly spreads whose gutter position
// cannot be measured, and conflating the two loses nine pages.
enum class Gutter {
    None,          // not a spread; no gutter to speak of
    Measured,      // a clear run bounded by ink on both sides
    Unmeasurable   // a clear run that runs off the window edge -- see below
};

// Unmeasurable is the discriminator that comes free. Where one half is blank or
// sparse the white run has no ink to bound it, runs off into the empty half,
// and is CLIPPED BY THE SEARCH WINDOW -- so its midpoint is an artifact of
// where we stopped looking, not the binding. On the corpus this put a 851px run
// 149px left of true centre purely because the left page had no ink.
//
//   If the run touches either window edge, the gutter is UNMEASURABLE.
//   The verdict still stands; only the position is unknown.

struct Result {
    Verdict verdict = Verdict::Single;
    Gutter  gutter  = Gutter::None;

    // Geometry of the image measured, carried so a caller need not re-read it.
    int width  = 0;
    int height = 0;

    // The winning clear run, in absolute px, [run_x0, run_x1). Zero-width when
    // no run was found.
    int run_x0 = 0;
    int run_x1 = 0;

    // Proposed cut, absolute px -- the run's midpoint. Meaningful only when
    // gutter == Measured; when Unmeasurable a caller resolves it against the
    // book consensus (see resolve_cut).
    int cut_x = 0;

    // Signed offset of cut_x from the midline as a fraction of width. This is
    // the quantity that is a BOOK constant, so it is what consensus averages --
    // not the absolute pixel, which would drift with any width variation.
    double offset_frac = 0.0;

    // Vertical extent of the type block, [type_top, type_bottom]. "Full height"
    // means full height of THIS, not of the image -- margins are not gutters.
    int type_top    = 0;
    int type_bottom = -1;

    // Test 1, recorded separately so a caller can say WHICH test rejected an
    // image rather than only that it did.
    bool aspect_pass = false;

    // True when the image carries no ink at all. Not an error and not a
    // verdict: a blank sheet is still a spread of two blank pages, and DESIGN's
    // page-164 rule says an interior empty page is transparent, not a
    // terminator. Recorded here so intake never has to re-open the image.
    bool blank = false;

    // -- Per HALF, which is not the same question -----------------------------
    //
    // s23, found by the first real corpus run and not by any test: `blank` is a
    // fact about the SHEET, and split:: copied it to both halves -- so on a
    // spread the BlankPage concern was structurally unreachable. Nine sheets in
    // the Hellboy corpus have a blank half, including page 164, the page
    // DESIGN's whole transparency rule is built on, and not one of them was
    // ever flagged. The flag existed, was tested, and could not fire.
    //
    // Measured over the region a cut CANNOT REACH -- outside the search window,
    // so [0, win_x0) and [win_x1, width). A half that is really empty is empty
    // out there too, and asking about a region no plausible cut can move means
    // the answer does not depend on a cut that may not exist yet. The
    // unmeasurable images are exactly the ones with a blank half, so making
    // blankness depend on the resolved cut would be circular.
    //
    // The ink floor is row_min_ink, reused rather than reinvented: a half is
    // blank when no row in its certain region carries enough ink to be type.
    // Scanner speckle does not make a blank leaf non-blank, and a sparse half --
    // the title page on A__0000 -- is NOT blank, which is the distinction the
    // 2b gate needs to keep.
    //
    // Meaningful only when verdict == Spread. A single page's emptiness is
    // `blank`; there are no halves to ask about.
    bool left_blank  = false;
    bool right_blank = false;
};

// -- The greyscale view -------------------------------------------------------

// A non-owning view of an 8-bit greyscale plane. 0 is black, 255 is white.
// Non-owning because the caller already has the pixels (from image::, from a
// test fixture, from a mapped file) and copying 10 megapixels to ask a yes/no
// question is not a cost this pass should impose.
struct Grey {
    const std::uint8_t* data = nullptr;
    int  width  = 0;
    int  height = 0;
    long stride = 0;   // bytes per row; 0 means "tightly packed", i.e. width

    bool valid() const {
        return data && width > 0 && height > 0;
    }
    long row_stride() const {
        return stride > 0 ? stride : static_cast<long>(width);
    }
    std::uint8_t at(int x, int y) const {
        return data[static_cast<long>(y) * row_stride() + x];
    }

    // A sub-view, [x0,x1) x [y0,y1). Non-owning like its parent -- the whole
    // point of the stride field is that a crop moves no pixels, and phase 2
    // answers in crop rects (Split.hpp) that phase 3 then has to read. Without
    // this, every consumer of a half-of-a-spread copies a few megapixels to ask
    // a question about them.
    //
    // Clamped to the parent rather than trusted: an out-of-range crop here
    // would read past the buffer, and the caller's rect came from a plan that a
    // human is allowed to edit.
    Grey crop(int x0, int y0, int x1, int y1) const {
        if (!valid()) return Grey{};
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > width)  x1 = width;
        if (y1 > height) y1 = height;
        // Redundant with valid(), which already rejects a zero dimension --
        // a mutation weakening this to `<` is equivalent, and was verified so.
        // It stays because returning a null Grey says "there is nothing here"
        // where a zero-width view pointing into the middle of the buffer says
        // "here is nothing", and only one of those survives being copied
        // somewhere that forgets to ask.
        if (x1 <= x0 || y1 <= y0) return Grey{};
        Grey s;
        s.data   = data + static_cast<long>(y0) * row_stride() + x0;
        s.width  = x1 - x0;
        s.height = y1 - y0;
        s.stride = row_stride();   // explicit: a crop is never tightly packed
        return s;
    }
};

// -- Per image ----------------------------------------------------------------

// Run both of Scott's tests on one image. Independent of every other image, so
// this parallelises; the ORDERING pass that consumes it does not (see below).
Result classify(const Grey& g, const Constants& k = {});

// -- Per folder ---------------------------------------------------------------

// The book's consensus gutter offset, as a signed fraction of width. Median of
// the Measured results, because a median is unmoved by the handful of images
// where a near-miss run wins. Returns 0.0 (the midline) when nothing in the
// folder was measurable, which is the honest answer rather than a guess.
double consensus_offset(const std::vector<Result>& results);

// The cut to actually use for one image: its own measurement when it has one,
// the book's consensus when it does not. This is the single place the fallback
// happens, so no caller can forget it.
int resolve_cut(const Result& r, double consensus_offset_frac);

}  // namespace optik::model::spread
