#pragma once
#include <string>
#include <vector>

// Intake (s22 -- discovery, the step before the pipeline starts).
//
// Finds the scans in a folder and puts them in order. That is all it does, and
// the narrowness is the point: discovery has no opinion about images, so it
// needs no image library and is provable headless.
//
// -- Three silent failures this file exists to end -----------------------------
//
// The s21 intake was non-recursive and allowed only .tif/.tiff/.png. A folder
// of JPEGs, or scans one level down, produced ZERO pages -- and the batch that
// followed then no-opped without saying why. Nothing anywhere reported that the
// folder had 400 files in it and Optik had looked at none of them.
//
// So, in order of how badly each one bit:
//
// 1. RECURSIVE by default. A scanner that writes into per-session subfolders is
//    normal, not exotic.
//
// 2. A WIDER extension set, and it is a parameter rather than a literal. JPEG
//    is what most scanning software writes by default; excluding it was a
//    decision nobody made on purpose.
//
// 3. EVERY REJECTION IS REPORTED. A file that was passed over says so and says
//    why. This is the one that actually matters -- the other two are bugs, and
//    this is the property that would have made them visible in seconds instead
//    of across two sessions. An empty result is never returned bare; it arrives
//    with the count of what was looked at and the reason each one lost.
//
// The rule underneath: a pass that finds nothing must be loud about it. "Zero
// pages" and "zero pages because you pointed me at the wrong folder" are the
// same value and completely different facts.
namespace optik::model::intake {

struct Options {
    // Lowercase, with the dot. Compared case-insensitively, because a folder of
    // .JPG off a camera is the same folder.
    std::vector<std::string> extensions = {
        ".tif", ".tiff", ".png", ".jpg", ".jpeg", ".bmp", ".webp"
    };

    // Walk subfolders. On by default -- see above.
    bool recursive = true;

    // Skip dotfiles and their directories (.thumbnails, .DS_Store, and the
    // sidecar clutter every scanning tool leaves behind).
    bool skip_hidden = true;
};

// Why a file in the folder did not become a scan. Recorded per file so the
// surface can say "412 files, 400 of them .arw raw" rather than "no pages".
enum class Skip {
    Extension,   // not in the accepted set
    Hidden,      // dotfile or inside a dot-directory
    NotAFile,    // socket, symlink to nowhere, unreadable entry
};

struct Skipped {
    std::string path;
    Skip        reason = Skip::Extension;
    std::string extension;   // as found, for the "400 of them .arw" summary
};

struct Found {
    std::string path;    // absolute, as walked
    std::string name;    // filename, which is what a Page records as its source
};

struct Report {
    std::vector<Found>   scans;      // in natural order, ready for split::plan
    std::vector<Skipped> skipped;

    std::size_t files_seen = 0;
    bool folder_exists = false;
    bool folder_readable = false;

    // Deliberately NOT "did we find anything". A caller asking `ok()` wants to
    // know whether the QUESTION was answerable, which is a different thing from
    // whether the answer was zero -- an empty but readable folder is a fine
    // answer to a badly aimed question, and it must not look like a failure of
    // the walk.
    bool ok() const { return folder_exists && folder_readable; }

    // The one-line summary a surface shows when scans is empty. Written here
    // rather than in the UI so every surface says the same thing and none of
    // them can decide to say nothing.
    std::string explain() const;
};

// Walk the folder and report. Never throws on a bad path; a missing or
// unreadable folder comes back as a Report saying so.
Report discover(const std::string& folder, const Options& opt = {});

}  // namespace optik::model::intake
