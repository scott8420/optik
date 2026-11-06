// selftest_audit.cpp -- the book's invariants (core/Audit), and the journal
// walked at random: whatever the verbs, the audit stays clean, undo to the
// start gives the start back, redo to the end gives the end back, and a save
// and an open agree. (s26, milestone 11 -- the core half of the sweep; the
// app half drives the window under Xvfb: app/Sweep.cpp, scripts/sweep.sh.)
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"

#include <filesystem>
#include <iostream>
#include <random>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

std::string data(const char* name) { return std::string(OPTIK_TEST_DATA) + "/" + name; }

fs::path temp_dir(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

// A book made the way New from Scans makes one: spread + single page.
bool imported(const fs::path& dir, core::Book& b, core::Journal& j) {
    if (core::Book::create(dir.string(), "Audit", b) != core::Status::Ok) return false;
    const std::vector<std::string> paths{data("spread.png"), data("page_left.png")};
    const core::ImportPlan plan = core::plan_import(paths, true);
    std::vector<core::Scan> scans = core::copy_scans(b.folder(), plan.paths);
    return j.run(b, "Import", [&](core::Edit& e) {
        std::vector<core::Scan> with_ids;
        for (auto& s : scans) with_ids.push_back(e.add_scan(s));
        std::size_t at = e.book().pages().size();
        for (auto& p : core::pages_for(plan, with_ids)) e.insert_page(at++, p);
    });
}

ocr::Area area(const core::Rect& box, std::initializer_list<const char*> words) {
    ocr::Area a;
    a.box = box;
    ocr::Para para;
    para.box = box;
    ocr::Line line;
    line.box = box;
    int x = box.x0;
    for (const char* t : words) {
        ocr::Word w;
        w.text = t;
        w.conf = 91.4f;
        w.box = {x, box.y0, x + 20, box.y0 + 10};
        x += 24;
        line.words.push_back(w);
    }
    para.lines.push_back(line);
    a.paras.push_back(para);
    return a;
}

bool has(const std::vector<std::string>& bad, const std::string& part) {
    for (const auto& s : bad) if (s.find(part) != std::string::npos) return true;
    return false;
}

std::string first(const std::vector<std::string>& bad) { return bad.empty() ? "" : bad[0]; }

}  // namespace

void audit_tests() {
    std::cout << "-- core: audit, the journal walked at random (s26)\n";
    const fs::path dir = temp_dir("audit");
    core::Book b;
    core::Journal j;
    const bool ok = imported(dir, b, j);
    check("audit: an imported book (a spread and a page) is sound",
          ok && b.pages().size() == 3 && core::audit(b).empty(), first(core::audit(b)));

    // Each broken rule is named.
    {
        core::Book bad = b;
        core::Journal jj;
        jj.run(bad, "break", [&](core::Edit& e) {
            core::Page p = e.book().pages()[0];
            p.id.clear();
            p.scan_id = "scn_999";
            e.insert_page(0, p);
        });
        check("audit: a page on a scan the book lacks is named", has(core::audit(bad), "no scan scn_999"));
    }
    {
        core::Book bad = b;
        core::Journal jj;
        const std::string id = b.pages()[0].id;
        jj.run(bad, "break", [&](core::Edit& e) {
            core::Page& p = e.page(id);
            p.read = true;
            p.areas.push_back(area({p.crop.width() - 10, 0, p.crop.width() + 40, 30}, {"over"}));
        });
        check("audit: an area off the page's edge is named", has(core::audit(bad), "off the page"));
    }
    {
        core::Book bad = b;
        core::Journal jj;
        const std::string id = b.pages()[1].id;
        jj.run(bad, "break", [&](core::Edit& e) {
            e.page(id).areas.push_back(area({10, 10, 60, 30}, {"stray"}));
        });
        check("audit: an engine area on a page never read is named",
              has(core::audit(bad), "not read, yet holds an engine area"));
    }

    // The race the sweep found (s26): an area's re-read lands after the area
    // list changed under it. It must land in ITS area or nowhere.
    {
        core::Book rb = b;
        core::Journal jj;
        const std::string id = rb.pages()[0].id;
        jj.run(rb, "two areas", [&](core::Edit& e) {
            core::Page& p = e.page(id);
            p.read = true;
            p.areas = {area({10, 10, 110, 40}, {"first"}), area({10, 60, 110, 90}, {"second"})};
        });
        core::ReadRequest req = core::request_for_area(rb, *rb.page(id), 0, {10, 10, 130, 40});
        core::Reading r;
        r.ok = true;
        r.page_id = id;
        r.area = req.area;
        r.area_box = req.area_box;
        r.crop = req.crop;
        r.turns = req.turns;
        r.area_was = req.area_was;
        r.paras = area({10, 10, 130, 40}, {"reread"}).paras;
        const ocr::Area second = rb.page(id)->areas[1];
        jj.run(rb, "delete area 0", [&](core::Edit& e) {
            auto& v = e.page(id).areas;
            v.erase(v.begin());
        });
        bool landed = true;
        jj.run(rb, "land", [&](core::Edit& e) { landed = core::land(e, r); });
        check("race: a re-read of a deleted area is dropped",
              !landed && rb.page(id)->areas.size() == 1 && rb.page(id)->areas[0] == second);
        jj.undo(rb);   // the area is back, now at index 0 again
        jj.run(rb, "insert before", [&](core::Edit& e) {
            auto& v = e.page(id).areas;
            v.insert(v.begin(), area({10, 200, 110, 230}, {"new"}));
        });
        jj.run(rb, "land", [&](core::Edit& e) { landed = core::land(e, r); });
        const auto& now = rb.page(id)->areas;
        check("race: a re-read finds its area by its box when its index moved",
              landed && now.size() == 3 && now[1].box == core::Rect{10, 10, 130, 40} &&
              now[0].paras[0].lines[0].words[0].text == "new" && now[2] == second);
    }

    // The walk: random verbs through the one door. Seeded, so a failure repeats.
    std::mt19937 rng(26);
    auto pick = [&](int n) { return int(std::uniform_int_distribution<int>(0, n - 1)(rng)); };
    const core::Book start = b;
    const std::size_t start_pos = j.position();
    int steps = 0;
    std::string why;
    bool clean = true;
    for (int i = 0; i < 300; ++i) {
        const auto& pages = b.pages();
        const int verb = pages.empty() ? 0 : pick(7);
        const std::string id = pages.empty() ? "" : pages[std::size_t(pick(int(pages.size())))].id;
        bool ran = false;
        switch (verb) {
        case 0:   // a copy of a page, as a fresh page (re-cut makes these)
            if (!b.pages().empty())
                ran = j.run(b, "copy", [&](core::Edit& e) {
                    core::Page p = e.book().pages()[0];
                    p.id.clear();
                    e.insert_page(std::size_t(pick(int(e.book().pages().size()) + 1)), p);
                });
            break;
        case 1:
            ran = j.run(b, "move", [&](core::Edit& e) {
                e.move_page(id, std::size_t(pick(int(e.book().pages().size()))));
            });
            break;
        case 2:
            if (b.pages().size() > 1) ran = j.run(b, "delete", [&](core::Edit& e) { e.remove_page(id); });
            break;
        case 3:
            ran = j.run(b, "area", [&](core::Edit& e) {
                core::Page& p = e.page(id);
                const int w = p.crop.width(), h = p.crop.height();
                const int x = pick(std::max(1, w - 60)), y = pick(std::max(1, h - 30));
                p.read = true;
                p.areas.push_back(area({x, y, x + 50, y + 20}, {"one", "two"}));
            });
            break;
        case 4:
            ran = j.run(b, "word", [&](core::Edit& e) {
                core::Page& p = e.page(id);
                if (p.areas.empty()) return;
                auto& w = p.areas[0].paras[0].lines[0].words[0];
                if (!w.edited) { w.original = w.text; w.edited = true; }
                w.text += "x";
            });
            break;
        case 5:
            ran = j.run(b, "settings", [&](core::Edit& e) {
                e.meta().settings.uncertain_below = pick(101);
                e.meta().settings.mend_quotes = !e.meta().settings.mend_quotes;
            });
            break;
        case 6:
            if (j.can_undo() && j.position() > start_pos && pick(3) == 0) { j.undo(b); ran = true; }
            break;
        }
        steps += ran;
        const auto bad = core::audit(b);
        if (!bad.empty()) { clean = false; why = "after step " + std::to_string(i) + ": " + bad[0]; break; }
    }
    check("walk: 300 random verbs, the audit clean after every one", clean && steps > 100,
          clean ? std::to_string(steps) + " steps" : why);

    const core::Book end = b;
    const std::size_t end_pos = j.position();
    while (j.position() > start_pos) j.undo(b);
    check("walk: undo to the start gives the start back", core::same_book(b, start, &why), why);
    while (j.position() < end_pos) j.redo(b);
    check("walk: redo to the end gives the end back", core::same_book(b, end, &why), why);

    check("walk: save", b.save() == core::Status::Ok);
    core::Book again;
    check("walk: what is saved opens as what it was",
          core::Book::open(b.folder(), again) == core::Status::Ok && core::same_book(b, again, &why), why);
    check("walk: the opened book is sound", core::audit(again).empty(), first(core::audit(again)));

    std::error_code ec;
    fs::remove_all(dir, ec);
}
