// selftest_core.cpp -- the book, the journal, reading, the pool, export.
//
// The OCR checks read rendered pages (tests/data, made by
// scripts/make_fixtures.py) and assert what any Tesseract 5 must manage on
// clean 30px serif type: the words are found, the running head is a head, the
// hyphen is mended, the page-turn paragraph is joined. Never an exact box.
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Jobs.hpp"
#include "core/Picture.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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

std::string page_words(const core::Page& p, bool text_only = true) {
    std::string s;
    for (const auto& a : p.areas) {
        if (text_only && !ocr::kind_is_text(a.kind)) continue;
        for (const auto& para : a.paras) { s += ocr::para_text(para); s += '\n'; }
    }
    return s;
}

bool has_kind(const core::Page& p, ocr::Kind k, const std::string& containing) {
    for (const auto& a : p.areas)
        if (a.kind == k)
            for (const auto& para : a.paras)
                if (ocr::para_text(para).find(containing) != std::string::npos) return true;
    return false;
}

// Run the pool until it has nothing left, landing as it goes. Bounded: a hang
// is a failure, not a stuck selftest.
bool drain(core::Jobs& jobs, int seconds = 120) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!jobs.idle()) {
        jobs.pump();
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    jobs.pump();
    return true;
}

ocr::Word word(const char* t, float conf) {
    ocr::Word w; w.text = t; w.conf = conf; w.box = {0, 0, 10, 10}; return w;
}

core::Page text_page(std::initializer_list<std::initializer_list<const char*>> lines) {
    core::Page p;
    p.crop = {0, 0, 100, 100};
    ocr::Area a;
    a.box = {0, 0, 100, 100};
    ocr::Para para;
    for (auto l : lines) {
        ocr::Line line;
        for (const char* w : l) line.words.push_back(word(w, 95));
        para.lines.push_back(line);
    }
    a.paras.push_back(para);
    p.areas.push_back(a);
    p.read = true;
    return p;
}

}  // namespace

void core_tests() {
    std::cout << "-- core: book, journal\n";
    {
        const fs::path dir = temp_dir("book");
        core::Book b;
        check("book: create makes the folder and book.json",
              core::Book::create(dir.string(), "", b) == core::Status::Ok &&
              fs::exists(dir / "book.json") && fs::exists(dir / "scans"));
        check("book: the title defaults to the folder's name",
              b.meta().title == dir.stem().string(), b.meta().title);
        core::Book again;
        check("book: create onto an existing folder refuses",
              core::Book::create(dir.string(), "", again) == core::Status::PathExists);
        check("book: open on a folder without book.json says so",
              core::Book::open(temp_dir("nobook").string(), again) == core::Status::NotABook);

        core::Journal j;
        const bool ran = j.run(b, "Add a page", [](core::Edit& e) {
            e.insert_page(0, text_page({{"Hello", "world"}}));
        });
        check("journal: a change is one step", ran && j.size() == 1 && b.pages().size() == 1);
        const std::string id = b.pages()[0].id;
        check("journal: an inserted page gets an id", !id.empty());

        check("journal: a step that changes nothing is not recorded",
              !j.run(b, "Nothing", [&](core::Edit& e) { (void)e.page(id); }) && j.size() == 1);

        // A user-typed word with characters a careless format would lose.
        const std::string typed = "Caf\xC3\xA9 \"quoted\" & <tag>  two  spaces";
        j.run(b, "Edit word", [&](core::Edit& e) {
            auto& w = e.page(id).areas[0].paras[0].lines[0].words[0];
            w.original = w.text;
            w.text = typed;
            w.edited = true;
        });
        check("journal: undo restores the page's picture",
              j.undo(b) && b.page(id)->areas[0].paras[0].lines[0].words[0].text == "Hello");
        check("journal: redo puts the edit back",
              j.redo(b) && b.page(id)->areas[0].paras[0].lines[0].words[0].text == typed);
        check("journal: undo of an insert removes the page",
              j.undo(b) && j.undo(b) && b.pages().empty());
        check("journal: redo of an insert brings it back with the same id",
              j.redo(b) && b.pages().size() == 1 && b.pages()[0].id == id);
        j.redo(b);

        const auto id_before = b.next_id();
        j.run(b, "Title", [](core::Edit& e) { e.meta().title = "Draft"; });
        j.undo(b);
        j.run(b, "Add another", [](core::Edit& e) { e.insert_page(9, text_page({{"x"}})); });
        check("journal: a new step drops the redo tail", !j.can_redo() && j.size() == 3);
        check("journal: ids are never reused after an undo",
              b.pages().back().id != id && b.next_id() > id_before);

        j.mark();
        check("journal: saved is clean", !j.dirty());
        j.undo(b);
        check("journal: undo past the save is dirty", j.dirty());
        j.redo(b);
        check("journal: back to the save is clean again", !j.dirty());

        j.run(b, "Move", [&](core::Edit& e) { e.move_page(b.pages().back().id, 0); });
        check("journal: a reorder alone is a step", b.pages()[0].id != id && j.size() == 4);
        j.undo(b);
        check("journal: undo of a reorder restores the order", b.pages()[0].id == id);

        j.run(b, "Settings", [](core::Edit& e) { e.meta().settings.uncertain_below = 60; });
        check("journal: a settings change is undoable",
              j.undo(b) && b.meta().settings.uncertain_below == 80);
        j.redo(b);

        // Round trip: every byte of the typed word, every flag, every setting.
        check("book: save", b.save() == core::Status::Ok);
        core::Book loaded;
        check("book: open what was saved", core::Book::open(dir.string(), loaded) == core::Status::Ok);
        const auto* lp = loaded.page(id);
        check("book: user-typed text survives save and load byte for byte",
              lp && lp->areas[0].paras[0].lines[0].words[0].text == typed);
        check("book: an edited word keeps the engine's original",
              lp && lp->areas[0].paras[0].lines[0].words[0].original == "Hello" &&
              lp->areas[0].paras[0].lines[0].words[0].edited);
        check("book: pages, order, settings and next id survive",
              loaded.pages() == b.pages() && loaded.meta() == b.meta() &&
              loaded.next_id() == b.next_id());

        std::ofstream(dir / "book.json") << "{ not json";
        check("book: a damaged book.json is reported, not crashed on",
              core::Book::open(dir.string(), loaded) == core::Status::BadJson);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::cout << "-- core: export\n";
    {
        check("export: a line-end hyphen before lower case is mended",
              core::join_lines({"and every-", "one stayed"}, true) == "and everyone stayed");
        check("export: ...but not before a capital",
              core::join_lines({"the well-", "Known"}, true) == "the well- Known");
        check("export: ...nor when the book says not to",
              core::join_lines({"and every-", "one"}, false) == "and every- one");
        check("export: a dash pair at a line end is not a hyphen",
              core::join_lines({"he said--", "and went"}, true) == "he said-- and went");

        const fs::path dir = temp_dir("flow");
        core::Book b;
        core::Book::create(dir.string(), "Flow", b);
        core::Journal j;
        j.run(b, "pages", [](core::Edit& e) {
            e.insert_page(0, text_page({{"It", "was", "a"}, {"dark", "and", "storm-"}}));
            core::Page p2 = text_page({{"y", "night."}});
            ocr::Area head;
            head.kind = ocr::Kind::Header;
            head.box = {0, 0, 100, 5};
            ocr::Para hp; ocr::Line hl; hl.words.push_back(word("RUNNING", 99));
            hp.lines.push_back(hl); head.paras.push_back(hp);
            p2.areas.insert(p2.areas.begin(), head);
            e.insert_page(1, p2);
            e.insert_page(2, text_page({{"Next", "morning."}}));
        });
        auto blocks = core::flow(b);
        check("export: running heads are left out, the page-turn paragraph is joined",
              blocks.size() == 2 && blocks[0].text == "It was a dark and stormy night.",
              blocks.empty() ? "" : blocks[0].text);
        check("export: a paragraph that ends a sentence is not joined",
              blocks.size() == 2 && blocks[1].text == "Next morning.");
        j.run(b, "split para", [](core::Edit& e) {
            core::Page p = text_page({{"It", "was", "the", "kind"}});
            p.areas.push_back(text_page({{"of", "evening."}}).areas[0]);
            p.areas.push_back(text_page({{"And", "then."}}).areas[0]);
            e.insert_page(3, p);
        });
        blocks = core::flow(b);
        check("export: a paragraph the engine split mid-sentence on one page is joined",
              blocks.size() == 4 && blocks[2].text == "It was the kind of evening.",
              blocks.size() > 2 ? blocks[2].text : "");
        check("export: ...but a capital after a sentence's end starts a new one",
              blocks.size() == 4 && blocks[3].text == "And then.");
        j.undo(b);
        blocks = core::flow(b);
        j.run(b, "no join", [](core::Edit& e) { e.meta().settings.join_pages = false; });
        check("export: join_pages off keeps the page turn", core::flow(b).size() == 3);

        const std::string odt = (dir / "out.odt").string();
        std::string why;
        check("export: writes an ODT", core::write_odt(blocks, "Flow & <Test>", odt, &why), why);
        std::ifstream f(odt, std::ios::binary);
        std::string head(80, '\0');
        f.read(head.data(), 80);
        check("export: the ODT's first entry is the stored mimetype",
              head.compare(30, 8, "mimetype") == 0 &&
              head.find("application/vnd.oasis.opendocument.text") != std::string::npos);
        check("export: writes plain text", core::write_text(blocks, (dir / "out.txt").string()));
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::cout << "-- core: picture, copy\n";
    {
        core::Picture pic;
        check("picture: decodes a page's crop, scaled to fit",
              core::load_picture(data("spread.png"), {1060, 0, 2060, 1400}, 700, pic) &&
              pic.height == 700 && pic.width == 500 && std::abs(pic.scale - 0.5) < 1e-6,
              std::to_string(pic.width) + "x" + std::to_string(pic.height));
        check("picture: white paper is white, opaque",
              pic.valid() && pic.px[0] == 0xFFFFFFFFu);
        check("picture: never enlarged",
              core::load_picture(data("page_left.png"), {}, 4000, pic) && pic.width == 1000);
        check("picture: a missing file is false", !core::load_picture(data("nope.png"), {}, 0, pic));
        const fs::path dir = temp_dir("copy");
        const auto a = core::copy_scans(dir.string(), {data("page_left.png")});
        const auto b = core::copy_scans(dir.string(), {data("page_left.png")});
        check("copy: a second scan of the same name is numbered, not overwritten",
              a.size() == 1 && b.size() == 1 && a[0].file != b[0].file &&
              b[0].source_name == "page_left.png" && fs::exists(dir / "scans" / b[0].file));
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::cout << "-- core: running heads\n";
    {
        std::vector<ocr::Area> areas(3);
        auto one_line = [](ocr::Area& a, ocr::Rect box, const char* text) {
            a.box = box;
            ocr::Para p; ocr::Line l; l.words.push_back(word(text, 90));
            p.lines.push_back(l); a.paras.push_back(p);
        };
        one_line(areas[0], {100, 40, 600, 70}, "THE BONES OF GIANTS");
        one_line(areas[1], {100, 300, 900, 340}, "a body line in the middle");
        one_line(areas[2], {480, 1300, 520, 1330}, "12");
        core::mark_running_heads(areas, 1400);
        check("heads: one line in the top tenth is a header", areas[0].kind == ocr::Kind::Header);
        check("heads: one line mid-page stays text", areas[1].kind == ocr::Kind::Text);
        check("heads: one line in the bottom tenth is a footer", areas[2].kind == ocr::Kind::Footer);
        areas[0].kind = ocr::Kind::Text;
        areas[0].user = true;
        core::mark_running_heads(areas, 1400);
        check("heads: an area the author set is never re-marked", areas[0].kind == ocr::Kind::Text);
    }

    std::cout << "-- ocr: the engine on rendered pages\n";
    {
        ocr::Engine eng;
        std::string why;
        const bool ready = eng.init("eng", &why);
        check("ocr: the English language data loads", ready, why);
        if (ready) {
            std::vector<ocr::Area> areas;
            check("ocr: reads a whole page",
                  eng.read_page(data("page_left.png"), {}, areas, &why) && !areas.empty(), why);
            core::Page p;
            p.areas = areas;
            core::mark_running_heads(p.areas, 1400);
            const std::string body = page_words(p);
            check("ocr: the body's words are found",
                  body.find("quick brown fox") != std::string::npos, body.substr(0, 60));
            check("ocr: the running head is a header, not body text",
                  has_kind(p, ocr::Kind::Header, "GIANTS") && body.find("GIANTS") == std::string::npos);
            check("ocr: the folio is a footer", has_kind(p, ocr::Kind::Footer, "12"));
            bool confident = false, boxed = true;
            for (const auto& a : p.areas)
                for (const auto& para : a.paras)
                    for (const auto& l : para.lines)
                        for (const auto& w : l.words) {
                            if (w.text == "quick" && w.conf > 70) confident = true;
                            if (w.box.empty() || w.box.x1 > 1000 || w.box.y1 > 1400) boxed = false;
                        }
            check("ocr: a clean word is read with confidence", confident);
            check("ocr: every word has a box inside the page", boxed);

            // Re-read one rectangle: the first body line only.
            std::vector<ocr::Para> paras;
            check("ocr: reads one area as a block",
                  eng.read_area(data("page_left.png"), {}, {80, 180, 900, 240}, paras, &why) &&
                  !paras.empty() && ocr::para_text(paras[0]).find("quick") != std::string::npos &&
                  ocr::para_text(paras[0]).find("rain") == std::string::npos, why);

            // A crop: the right half of the spread is page_right in page space.
            check("ocr: a crop reads in page space",
                  eng.read_page(data("spread.png"), {1060, 0, 2060, 1400}, areas, &why));
            core::Page r; r.areas = areas;
            bool inside = !areas.empty();
            for (const auto& a : areas) if (a.box.x1 > 1000) inside = false;
            check("ocr: ...its boxes are relative to the crop",
                  inside && page_words(r).find("shutter") != std::string::npos);
            check("ocr: a missing file is an error with a reason",
                  !eng.read_page(data("nope.png"), {}, areas, &why) && !why.empty());
        }
    }

    std::cout << "-- core: import, read on the pool, export the book\n";
    {
        const auto plan = core::plan_import({data("spread.png")}, true);
        check("import: a spread scan plans two pages", plan.pages.size() == 2,
              std::to_string(plan.pages.size()));
        check("import: ...the left first",
              plan.pages.size() == 2 && plan.pages[0].half == model::split::Half::Left &&
              plan.pages[1].half == model::split::Half::Right);
        const auto whole = core::plan_import({data("spread.png")}, false);
        check("import: split declined keeps the scan whole",
              whole.pages.size() == 1 && whole.pages[0].half == model::split::Half::Whole);

        const fs::path dir = temp_dir("pool");
        core::Book b;
        core::Book::create(dir.string(), "Pool", b);
        core::Journal j;
        const auto copied = core::copy_scans(b.folder(), plan.paths);
        j.run(b, "Import", [&](core::Edit& e) {
            std::vector<core::Scan> scans;
            for (const auto& s : copied) scans.push_back(e.add_scan(s));
            for (const auto& p : core::pages_for(plan, scans)) e.insert_page(e.book().pages().size(), p);
        });
        check("import: the scan is copied in and the pages land as one step",
              b.pages().size() == 2 && j.size() == 1 &&
              fs::exists(b.scan_path(b.pages()[0].scan_id)));

        core::Jobs jobs(2);
        int landed = 0;
        for (const auto& p : b.pages()) {
            const auto req = core::request_for(b, p);
            jobs.submit([req, &b, &j, &landed](ocr::Engine& eng) -> core::Jobs::Landing {
                auto reading = std::make_shared<core::Reading>(core::read(eng, req));
                return [reading, &b, &j, &landed] {
                    if (j.run(b, "Read page", [&](core::Edit& e) { core::land(e, *reading); }))
                        ++landed;
                };
            });
        }
        check("pool: both pages read and land", drain(jobs) && landed == 2,
              std::to_string(landed));
        check("pool: each landing is its own step", j.size() == 3);
        check("pool: the pages are marked read", b.pages()[0].read && b.pages()[1].read);
        check("pool: the right page found its running head",
              has_kind(b.pages()[1], ocr::Kind::Header, "CHAPTER"));

        const auto blocks = core::flow(b);
        std::string all;
        for (const auto& bl : blocks) all += bl.text + "\n";
        check("book: the hyphenated word comes out whole",
              all.find("town everyone stayed") != std::string::npos, all.substr(0, 120));
        check("book: the paragraph broken by the page turn comes out whole",
              all.find("the wind came down from the hills") != std::string::npos);
        check("book: no running head or folio in the text",
              all.find("GIANTS") == std::string::npos && all.find("CHAPTER") == std::string::npos &&
              all.find("\n12\n") == std::string::npos);

        // Cancel: queued work never lands.
        for (int i = 0; i < 6; ++i) {
            const auto req = core::request_for(b, b.pages()[0]);
            jobs.submit([req, &landed](ocr::Engine& eng) -> core::Jobs::Landing {
                auto reading = core::read(eng, req);
                return [&landed] { ++landed; };
            });
        }
        jobs.cancel();
        const int before = landed;
        drain(jobs);
        check("pool: cancel leaves nothing", landed == before);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
