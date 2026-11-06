#pragma once
// core/Book.hpp -- the book: its scans, its pages, and what each page says.
//
// GTK-FREE (core/ never sees gtkmm; the build enforces it). This is the model the
// whole window renders.
//
// A book is a folder:
//   <name>.optik/
//     book.json      title, settings, scans, pages (crops + areas + words)
//     scans/         the masters, copied byte-for-byte on import
//
// A PAGE is a rectangle on a scan. A spread scan gives two pages; a single scan
// gives one. Its areas and words are in page space (ocr/Layout.hpp).
//
// WRITES: the Book has no public mutators. Every user change goes through
// core::Journal (Undo.hpp), which hands the change an Edit -- the only thing
// that can write to a Book. Loads are named as loads (open). So "forgot to make
// it undoable" is not a thing a caller can do; it does not compile.

#include "model/Split.hpp"
#include "ocr/Layout.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace optik::core {

using ocr::Rect;

struct Scan {
    std::string id;            // scn_N
    std::string file;          // filename inside scans/, never a path
    std::string source_name;   // what it was called where it came from
    bool operator==(const Scan&) const = default;
};

struct Page {
    std::string id;            // pg_N, never reused
    std::string scan_id;
    model::split::Half half = model::split::Half::Whole;
    Rect        crop;          // on the scan
    unsigned    concerns = 0;  // split:: concerns, for "check this split"
    bool        read = false;  // recognised at least once
    int         turns = 0;     // quarter turns clockwise after the crop, 0..3 (s26);
                               // the page -- and every box on it -- is the turned crop
    std::vector<ocr::Area> areas;
    bool operator==(const Page&) const = default;
};

// The book's choices: FineReader's options page, per book. Each is a pass the
// author can decline (DESIGN: a checklist, not a pipeline).
struct Settings {
    std::string language = "eng";
    int  uncertain_below    = 80;    // a word under this confidence wants a look
    bool split_spreads      = true;  // import: cut two-page scans into pages
    bool mark_running_heads = true;  // read: running heads / folios -> header / footer
    bool join_pages         = true;  // export: a paragraph runs on over the page turn
    bool dehyphenate        = true;  // export: "exam-\nple" -> "example"
    bool mark_headings      = true;  // read: a short centred block with space below -> Heading
    bool mend_quotes        = true;  // read: ``Stop'' / ""Stop"" -> one typographic quote
    bool small_caps         = true;  // read: caps headings upper-cased, chapter openers doubted
    bool mark_italic        = true;  // read: italic words (measured) marked to check (s27)
    bool check_spelling     = true;  // read: words not in the dictionary marked to check (s27)
    bool operator==(const Settings&) const = default;
};

struct Meta {
    std::string title;
    Settings    settings;
    // The book's own words (s27): names and coinages the author has told the
    // spelling pass are right -- "Add to Book's Words". Kept sorted, one each
    // (case as the author accepted it; matched without regard to case).
    std::vector<std::string> words;
    bool operator==(const Meta&) const = default;
};

enum class Status { Ok, PathExists, NotABook, NotReadable, BadJson, SchemaTooNew, WriteFailed };
const char* status_text(Status s);

class Edit;
class Journal;

class Book {
public:
    // -- files. open() is a LOAD: not a user choice, not a journal step.
    static Status create(const std::string& folder, const std::string& title, Book& out);
    static Status open(const std::string& folder, Book& out);
    Status        save() const;


    // -- reading
    const std::string&       folder() const { return m_folder; }
    const Meta&              meta()   const { return m_meta; }
    const std::vector<Scan>& scans()  const { return m_scans; }
    const std::vector<Page>& pages()  const { return m_pages; }
    const Page* page(const std::string& id) const;
    int         index_of(const std::string& page_id) const;    // -1 if none
    const Scan* scan(const std::string& id) const;
    std::string scan_path(const std::string& scan_id) const;

    // Words wanting a look: under the threshold, doubted by a pass, or unknown
    // to the dictionary (s27) -- and not yet checked.
    bool uncertain(const ocr::Word& w) const;
    int  uncertain_count(const Page& p) const;

    std::uint64_t next_id() const { return m_next_id; }

private:
    friend class Edit;
    friend class Journal;

    std::string       m_folder;
    Meta              m_meta;
    std::vector<Scan> m_scans;
    std::vector<Page> m_pages;
    std::uint64_t     m_next_id = 1;

    std::string mint(const char* prefix);
};

// -- Import planning ---------------------------------------------------------
// Decode each scan, ask spread:: whether it is two pages, and plan the pages.
// Heavy (it decodes every scan) -- run it on a worker. Pure: touches no Book.
struct ImportPlan {
    std::vector<std::string> paths;          // the scans, natural order
    std::vector<model::split::PageSource> pages;
    std::vector<std::string> unreadable;
};
ImportPlan plan_import(const std::vector<std::string>& paths, bool split_spreads);

// Copy scans into <book>/scans/ -- filesystem only, so an import worker can do
// it while the window carries on. Returns the Scan records with file and
// source_name set and NO id (the journal step that adds them mints those);
// a file that could not be copied is left out.
std::vector<Scan> copy_scans(const std::string& book_folder, const std::vector<std::string>& paths);

// Turn a plan's pages into Book pages for the copied-in scans (matched by
// source name; call after the scans have ids). Page ids are minted on insert.
std::vector<Page> pages_for(const ImportPlan& plan, const std::vector<Scan>& scans);

// -- The reading pass that runs after the engine ---------------------------
// Running heads and folios: a one-line text area hugging the top or bottom edge
// of the page becomes Header / Footer. Kept, shown, left out of export.
void mark_running_heads(std::vector<ocr::Area>& areas, int page_height);

}  // namespace optik::core
