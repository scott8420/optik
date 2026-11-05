#pragma once
#include "model/Spread.hpp"

#include <cstddef>
#include <string>
#include <vector>

// Split (s22 -- pipeline phase 2: "split spreads and renumber").
//
// DESIGN "Renumbering". Phase 1 asked each image what it holds; phase 2 turns
// the answers into the book's page sequence. Image i yields pages 2i+1 and
// 2i+2 when split, one page otherwise, and the output is a clean contiguous
// 1..N -- input filenames carry no meaning past sort order.
//
// -- Two decisions this file encodes ------------------------------------------
//
// 1. A HALF IS A CROP RECT, NOT A NEW FILE.
//
// The alternative was writing two images into scans/ per spread. Crop rect wins
// on three counts. The masters stay byte-untouched, so provenance survives and
// a bad cut costs a number change rather than a re-import. The halves are
// disposable and rebuildable, which is the split the project already draws
// between cache and project store -- and a written half is precious data by
// accident, which is how a disposable thing quietly becomes something you are
// afraid to delete. And the split joins the existing recipe instead of becoming
// a new destructive step in front of it.
//
// The cost, named: every consumer resolves a crop instead of opening a file.
// That is one adapter at the image seam, paid once.
//
// The rects TILE the source exactly -- left is [0, cut), right is [cut, width),
// nothing dropped, nothing overlapping. The gutter is not trimmed. Trimming
// would be a judgment about margins made in the wrong phase and at the wrong
// end of the pipeline; boxing excludes the margin in phase 3, where the author
// can see it and argue.
//
// 2. THE COUNTER IS FOLDER-WIDE AND FORWARD-ONLY, so the API refuses to run
//    per-file.
//
// A page cannot be numbered without knowing everything before it. There is
// deliberately no `plan_one(scan)` here: the only entry point takes the whole
// folder and returns the whole sequence, because a resumable version of this
// pass would be a correctness bug wearing a performance costume. It is also
// what makes phase 2b cheap -- change a verdict, call plan() again, the tail
// renumbers itself.
//
// Mixed input works by construction: singles contribute one, spreads two, the
// running count carries. That is not a special case, it is the loop.
namespace optik::model::split {

// -- Named constants ----------------------------------------------------------
// The review thresholds. These decide what gets the author's EYE at the phase
// 2b gate, not what gets cut -- so being wrong here costs a glance, never a
// page. Set generously on purpose.
struct Constants {
    // A measured gutter this far from the book's consensus, as a fraction of
    // width, is worth a look. The corpus held a 0.46% standard deviation across
    // 100 sheets, so 2% is several sigma out and still cheap to check.
    double offset_outlier_frac = 0.02;

    // A verdict shared by less than this fraction of the folder is a minority
    // report. One single page in a folder of spreads is either a real cover or
    // a missed gutter, and the two look identical from here.
    double verdict_minority_frac = 0.20;
};

// Which half of the source image a page is. Whole means the image was not
// split, and is the common case for an already-split archive.
enum class Half { Whole, Left, Right };

// Why a page might want the author's eye before OCR. Flags, not errors --
// every one of these still produces a page, and every one is a cheap fix here
// and an expensive one after corrections are keyed to page numbers.
enum Concern : unsigned {
    ConcernNone          = 0u,
    // The gutter could not be measured on this image; the cut came from the
    // book's consensus. Correct far more often than not, and still the first
    // thing to look at.
    GutterFromConsensus  = 1u << 0,
    // The gutter WAS measured and disagrees with the rest of the book.
    OffsetOutlier        = 1u << 1,
    // This image's verdict is the minority in its folder.
    VerdictOutlier       = 1u << 2,
    // The author set this cut by hand at the 2b gate. Not a worry -- the
    // opposite -- but it is the one thing on a page that a re-measurement must
    // never quietly undo, so it is visible rather than implicit.
    AuthorSet            = 1u << 4,
    // No ink at all. Not a defect -- DESIGN's page-164 rule needs these pages
    // to exist so the joiner can see across them -- but the author should know
    // where the holes are before deciding what they mean.
    BlankPage            = 1u << 3,
};

// One page of the book, sourced from a crop of one scan.
struct PageSource {
    std::size_t number = 0;        // 1..N, folder-wide, contiguous

    // s7 dropped the per-page original filename, reasoning that the scan is
    // copied into the project so there is nothing to re-link to. That reasoning
    // assumed one image = one page. Once one image becomes two, "which scan did
    // page 47 come from, and was it the left or right half?" is the question
    // asked the moment a split looks wrong -- and nothing answered it. Restored
    // under changed premises, not by accident.
    std::string source_name;
    Half        half = Half::Whole;

    // The crop on the source image: [x0, x1) x [y0, y1).
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    unsigned concerns = ConcernNone;

    int  width()  const { return x1 - x0; }
    int  height() const { return y1 - y0; }

    // Wants the author's eye. NOT simply "carries a concern" -- AuthorSet is
    // the author's answer, so counting it would mean every page they fixed
    // stayed on the worklist and the count never reached zero. A doubt the
    // author has settled is not a doubt.
    //
    // BlankPage stays in: a blank leaf is a fact, but what it MEANS -- a
    // stripped plate to see through, or a real boundary -- is a question only
    // the author can answer (DESIGN, page 164).
    bool flagged() const { return (concerns & ~unsigned(AuthorSet)) != ConcernNone; }
};

// One scan on the way in: its name and what phase 1 said about it. Name and
// verdict travel together in ONE vector rather than two parallel ones, because
// two parallel vectors is a desync waiting for the first sort.
struct Scan {
    std::string    name;
    spread::Result result;
};

struct Plan {
    std::vector<PageSource> pages;

    // The book's gutter offset, carried out so a surface can show what the
    // unmeasurable images were cut at, and so it is visible rather than buried.
    double consensus_offset = 0.0;

    std::size_t spreads = 0;
    std::size_t singles = 0;
    std::size_t flagged = 0;
};

// The whole folder in, the whole sequence out. Sorts internally (see below), so
// a caller cannot get the order wrong by handing them over unsorted.
Plan plan(std::vector<Scan> scans, const Constants& k = {});

// NATURAL order: digit runs compare numerically, so scan_2 precedes scan_10.
// Plain lexicographic order is correct on zero-padded names and silently
// interleaves a book on unpadded ones -- and an interleaved book does not fail
// loudly, it reads like bad OCR. Exposed because intake wants the same order.
bool natural_less(const std::string& a, const std::string& b);

}  // namespace optik::model::split
