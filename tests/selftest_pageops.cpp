// selftest_pageops.cpp -- the page verbs (s26, milestone 12): move, delete,
// rotate, re-cut. Each in one journal step, undone whole, the audit clean
// after every one; a turned page read upright by the engine; turns in book.json.
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/PageOps.hpp"
#include "core/Picture.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"

#include <leptonica/allheaders.h>

#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;
using model::split::Half;

namespace {

std::string data(const char* name) { return std::string(OPTIK_TEST_DATA) + "/" + name; }

fs::path temp_dir(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

std::string first(const std::vector<std::string>& v) { return v.empty() ? "" : v[0]; }

std::string order(const core::Book& b) {
    std::string s;
    for (const auto& p : b.pages()) s += p.id + " ";
    return s;
}

// The spread (two pages) then a single page: three pages, two scans.
bool imported(const fs::path& dir, core::Book& b, core::Journal& j) {
    if (core::Book::create(dir.string(), "Ops", b) != core::Status::Ok) return false;
    const core::ImportPlan plan = core::plan_import({data("spread.png"), data("page_left.png")}, true);
    std::vector<core::Scan> scans = core::copy_scans(b.folder(), plan.paths);
    return j.run(b, "Import", [&](core::Edit& e) {
        std::vector<core::Scan> with_ids;
        for (auto& s : scans) with_ids.push_back(e.add_scan(s));
        std::size_t at = 0;
        for (auto& p : core::pages_for(plan, with_ids)) e.insert_page(at++, p);
    });
}

ocr::Area some_area() {
    ocr::Area a;
    a.box = {10, 10, 200, 40};
    ocr::Para para;
    para.box = a.box;
    ocr::Line l;
    l.box = a.box;
    ocr::Word w;
    w.text = "word";
    w.conf = 90;
    w.box = {10, 10, 60, 40};
    l.words.push_back(w);
    para.lines.push_back(l);
    a.paras.push_back(para);
    return a;
}

}  // namespace

void pageops_tests() {
    std::cout << "-- core: page operations -- move, delete, rotate, re-cut (s26)\n";
    const fs::path dir = temp_dir("pageops");
    core::Book b;
    core::Journal j;
    const bool ok = imported(dir, b, j);
    check("pageops: the book is a spread's two pages and a single", ok && b.pages().size() == 3);
    if (!ok || b.pages().size() != 3) return;
    // Natural order: page_left.png is the first scan, spread.png the second.
    std::string spread_scan;
    for (const auto& sc : b.scans()) if (sc.source_name == "spread.png") spread_scan = sc.id;
    const auto halves = core::pages_of_scan(b, spread_scan);
    check("pageops: the spread's two pages, after the single", halves.size() == 2 && b.index_of(halves[0]) == 1);
    if (halves.size() != 2) return;
    const std::string S = b.pages()[0].id, L = halves[0], R = halves[1];
    std::string why;

    // -- move (on the three in book order: S L R)
    {
    const std::string P0 = S, P1 = L, P2 = R;

    // -- move
    check("move: down one", j.run(b, "Move", [&](core::Edit& e) { core::move_page_by(e, P0, 1); }) &&
                            order(b) == P1 + " " + P0 + " " + P2 + " ", order(b));
    check("move: past the end does nothing",
          !j.run(b, "Move", [&](core::Edit& e) { core::move_page_by(e, P2, 1); }));
    check("move: up five, clamped at the top",
          j.run(b, "Move", [&](core::Edit& e) { core::move_page_by(e, P2, -5); }) && b.pages()[0].id == P2);
    check("move: undo puts the order back", j.undo(b) && j.undo(b) && order(b) == P0 + " " + P1 + " " + P2 + " ");
    }

    // -- delete
    const core::Book before_delete = b;
    check("delete: the page goes, its scan stays",
          j.run(b, "Delete", [&](core::Edit& e) { core::delete_page(e, R); }) &&
          !b.page(R) && b.pages().size() == 2 && b.scans().size() == 2 && core::audit(b).empty());
    check("delete: undo brings the same page back in its place",
          j.undo(b) && core::same_book(b, before_delete, &why), why);

    // -- rotate
    j.run(b, "Give it a reading", [&](core::Edit& e) {
        core::Page& p = e.page(S);
        p.areas = {some_area()};
        p.read = true;
    });
    const core::Book read = b;
    const core::Size2i up = core::page_size(*b.page(S));
    check("rotate: a quarter turn clockwise", j.run(b, "Rotate", [&](core::Edit& e) { core::rotate_page(e, S, 1); }) &&
          b.page(S)->turns == 1);
    const core::Size2i side = core::page_size(*b.page(S));
    check("rotate: the page's sides swap", side.w == up.h && side.h == up.w);
    check("rotate: its reading is cleared (read from other pixels)", !b.page(S)->read && b.page(S)->areas.empty());
    check("rotate: no turn is not a step", !j.run(b, "Rotate", [&](core::Edit& e) { core::rotate_page(e, S, 4); }));
    j.run(b, "Rotate", [&](core::Edit& e) { core::rotate_page(e, S, -1); });
    check("rotate: back anticlockwise is upright", b.page(S)->turns == 0);
    j.run(b, "Rotate", [&](core::Edit& e) { core::rotate_page(e, S, -1); });
    check("rotate: anticlockwise from upright is three turns", b.page(S)->turns == 3);
    check("rotate: undoing the turns gives the reading back", j.undo(b) && j.undo(b) && j.undo(b) &&
          core::same_book(b, read, &why), why);
    j.run(b, "Rotate", [&](core::Edit& e) { core::rotate_page(e, S, 2); });
    check("rotate: save and open keep the turn",
          b.save() == core::Status::Ok && [&] {
              core::Book again;
              return core::Book::open(b.folder(), again) == core::Status::Ok && again.page(S) &&
                     again.page(S)->turns == 2 && core::same_book(b, again, &why);
          }(), why);
    j.undo(b);

    // A read asked before a turn lands after it: dropped (s26).
    {
        core::Reading late;
        late.ok = true;
        late.page_id = S;
        late.crop = b.page(S)->crop;
        late.turns = b.page(S)->turns;
        late.areas = {some_area()};
        j.run(b, "Turn", [&](core::Edit& e) { core::rotate_page(e, S, 1); });
        bool landed = true;
        j.run(b, "Land", [&](core::Edit& e) { landed = core::land(e, late); });
        check("race: a reading asked before the page was turned is dropped", !landed && !b.page(S)->read);
        j.undo(b);
        j.run(b, "Land", [&](core::Edit& e) { landed = core::land(e, late); });
        check("race: ...and lands once the page is as it was", landed && b.page(S)->read);
        j.undo(b);
    }

    // -- the picture and the engine see the turned page
    {
        core::Picture pic;
        check("picture: a turned page is drawn turned (sides swapped)",
              core::load_picture(data("page_left.png"), {}, 1, 0, pic) &&
              pic.width == 1400 && pic.height == 1000 && pic.src_w == 1400);
        // A sideways scan: page_left turned anticlockwise on disk; the page
        // turned one quarter clockwise reads upright.
        const fs::path side_scan = dir / "sideways.png";
        Pix* src = pixRead(data("page_left.png").c_str());
        Pix* rot = src ? pixRotateOrth(src, 3) : nullptr;
        const bool wrote = rot && pixWrite(side_scan.c_str(), rot, IFF_PNG) == 0;
        if (rot) pixDestroy(&rot);
        if (src) pixDestroy(&src);
        ocr::Engine eng;
        if (wrote && eng.init("eng", &why)) {
            std::vector<ocr::Area> areas;
            const bool read_ok = eng.read_page(side_scan.string(), {}, 1, areas, &why);
            std::string text;
            bool inside = true;
            for (const auto& a : areas) {
                for (const auto& para : a.paras) text += ocr::para_text(para) + "\n";
                if (a.box.x1 > 1000 + 2 || a.box.y1 > 1400 + 2) inside = false;
            }
            check("engine: a sideways scan turned upright is read",
                  read_ok && text.find("quick brown fox") != std::string::npos, text.substr(0, 60));
            check("engine: ...its boxes are in the turned page's space", inside);
            std::vector<ocr::Para> paras;
            check("engine: an area on the turned page reads",
                  eng.read_area(side_scan.string(), {}, 1, {80, 180, 900, 240}, paras, &why) &&
                  !paras.empty() && ocr::para_text(paras[0]).find("quick") != std::string::npos, why);
        } else {
            check("engine: a sideways scan made and the engine ready", false, why);
        }
    }

    // -- re-cut
    const core::Rect full{0, 0, 2060, 1400};   // spread.png
    const core::Page left0 = *b.page(L);
    check("recut: the spread's pages are its first two", core::pages_of_scan(b, spread_scan) ==
          std::vector<std::string>{L, R});
    check("recut: the cut it already has is not a step",
          !j.run(b, "Re-cut", [&](core::Edit& e) { core::recut_scan(e, spread_scan, full, left0.crop.x1); }));
    check("recut: a cut outside the scan is refused",
          !j.run(b, "Re-cut", [&](core::Edit& e) { core::recut_scan(e, spread_scan, full, 2060); }));

    std::vector<std::string> changed;
    j.run(b, "Re-cut", [&](core::Edit& e) { changed = core::recut_scan(e, spread_scan, full, 1000); });
    const core::Page* l = b.page(L);
    const core::Page* r = b.page(R);
    check("recut: moving the cut keeps both ids and tiles the scan",
          l && r && l->crop == core::Rect{0, 0, 1000, 1400} && r->crop == core::Rect{1000, 0, 2060, 1400} &&
          b.index_of(R) == b.index_of(L) + 1);
    check("recut: both pages are to be read again", changed.size() == 2 && !l->read && !r->read);

    j.run(b, "One page", [&](core::Edit& e) { changed = core::recut_scan(e, spread_scan, full, 0); });
    check("recut: one page -- the first keeps its id and the whole scan, the second goes",
          b.page(L) && !b.page(R) && b.page(L)->half == Half::Whole && b.page(L)->crop == full &&
          b.pages().size() == 2 && core::audit(b).empty(), first(core::audit(b)));

    j.run(b, "Two pages", [&](core::Edit& e) { changed = core::recut_scan(e, spread_scan, full, 1060); });
    check("recut: two pages again -- the right is a new page, after the left",
          b.pages().size() == 3 && b.index_of(L) == 1 && changed.size() == 2 && changed[1] != R &&
          b.pages()[2].id == changed[1] && b.pages()[2].half == Half::Right && core::audit(b).empty());

    // Moved apart, re-cut brings the scan's pages together again, from the first.
    j.run(b, "Apart", [&](core::Edit& e) { core::move_page_by(e, L, -1); });   // L S R'
    j.run(b, "Re-cut", [&](core::Edit& e) { core::recut_scan(e, spread_scan, full, 1030); });
    check("recut: the scan's pages come back together, where the first one was",
          b.pages().size() == 3 && b.pages()[0].id == L && b.pages()[1].id == changed[1] &&
          b.pages()[2].id == S, order(b));

    // A scan whose pages were all deleted is re-cut back in where it was.
    j.run(b, "Delete both", [&](core::Edit& e) {
        for (const auto& id : core::pages_of_scan(e.book(), spread_scan)) core::delete_page(e, id);
    });
    j.run(b, "Back", [&](core::Edit& e) { changed = core::recut_scan(e, spread_scan, full, 1060); });
    check("recut: a scan with no pages left comes back after the earlier scan's page",
          b.pages().size() == 3 && b.pages()[0].id == S && b.pages()[1].half == Half::Left &&
          b.pages()[2].half == Half::Right, order(b));

    const std::size_t steps = j.position();
    while (j.position() > 1) j.undo(b);   // to the import
    check("recut: undo all the way is the imported book", b.pages().size() == 3 &&
          order(b) == S + " " + L + " " + R + " " && b.page(L)->crop == left0.crop && core::audit(b).empty());
    while (j.position() < steps) j.redo(b);
    check("recut: redo all the way is sound", core::audit(b).empty(), first(core::audit(b)));

    std::error_code ec;
    fs::remove_all(dir, ec);
}
