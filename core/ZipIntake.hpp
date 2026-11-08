#pragma once
// ZipIntake (s33) -- scans that arrive as a zip.
//
// Librioteca keeps a book's scans as Author/Title/Archive.zip, and hands Optik
// that path. Optik's intake (libs/scan, no dependencies) only walks folders, so
// the zip is read here in core, which already links libzip:
//
//   list()    reads the zip's directory only -- cheap, safe on the UI thread --
//             and says how many scans it holds, in the same words intake uses.
//   unpack()  writes the scans out to a folder (a worker job: a book's TIFFs
//             are gigabytes), then intake::discover on that folder orders them
//             exactly as a scans folder would be ordered.
//
// s33b (Scott): the zip is first COPIED off the share to a local scratch
// folder (the app does that with GIO, which reads the gvfs/SMB mount the way
// Files does), and the book is made locally -- never on the share.
//
// The zip is only ever read. Entries are never written outside `dest`: an
// absolute name or a ".." part is passed over, not trusted.
#include "model/Intake.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace optik::core::zipin {

bool is_zip(const std::string& path);   // by extension, case-insensitive

struct Listing {
    bool        opened = false;   // the zip could be read at all
    std::string error;            // why not, when !opened
    std::size_t files_seen = 0;   // file entries (not directories)
    std::size_t scans = 0;        // of them, in the accepted set
    std::size_t skipped = 0;      // the rest (other types, hidden, __MACOSX, unsafe names)

    bool ok() const { return opened && scans > 0; }
    std::string explain() const;  // the one line a surface shows
};

Listing list(const std::string& zip, const model::intake::Options& opt = {});

// Live counters a worker fills and the window reads (atomics: no locks, the UI
// never waits). Phase 1 copies bytes, phase 2 unpacks scans, phase 3 imports.
struct Progress {
    std::atomic<int>           phase{0};
    std::atomic<std::uint64_t> done{0};
    std::atomic<std::uint64_t> total{0};
};

struct Unpacked {
    std::string folder;           // where the scans now are (== dest)
    std::size_t written = 0;
    bool        cancelled = false;
    std::string error;            // the first failure, if any
};

// Writes the zip's scans under `dest` (created; relative paths kept). `stop`,
// when set, is checked between entries; a stopped unpack reports cancelled.
Unpacked unpack(const std::string& zip, const std::string& dest,
                const model::intake::Options& opt = {},
                const std::atomic<bool>* stop = nullptr,
                Progress* progress = nullptr);   // s33b: done/total = scans

// The book's name for a zip: its stem, or -- for a generic name like
// "Archive.zip" -- the folder it sits in ("Retief's War/Archive.zip" ->
// "Retief's War").
std::string book_name(const std::string& zip);

// The book's folder name: "<Author> - <Title>" for a generic zip in a library
// (".../Alexander, Lloyd/The Black Cauldron/Archive.zip"), else book_name.
std::string folder_name(const std::string& zip);

// Where the book goes (s33b): local, <books_root>/<folder_name>.optik.
std::string book_folder(const std::string& zip, const std::string& books_root);

// A fresh scratch folder for one unpack, under $XDG_CACHE_HOME (or
// ~/.cache)/optik/intake/. The caller removes it when the import is done.
std::string scratch_folder(const std::string& name);

}  // namespace optik::core::zipin
