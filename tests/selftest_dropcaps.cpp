// selftest_dropcaps.cpp -- milestone 24 (s32): a chapter's large initial found,
// spliced back into its first word, and exported as a drop cap (Rebuild) or a
// plain capital (Strip).
//
// The pass's rules are checked on built areas with a stand-in reader (exact):
// each of the engine's three shapes -- the cap as its own area or a picture, as
// a tall first word, missed with the lines beside it indented -- and what is not
// a cap (a gap that reads as nothing, as digits; a paragraph indent; a run too
// deep). The rendered page (tests/data/dropcap.png, s32) is checked for what
// holds on any Tesseract: the cap is found, its paragraph is one, the first
// word starts with its letter and wants a look; with the pass off, none.
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Passes.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"

#include <zip.h>

#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

using K = ocr::Kind;

// The page of tests/data/dropcap.png, as the engine reads it (s32, Tesseract
// 5.3.4): the cap missed, its three lines indented at x 210, the paragraph's
// rest an area of its own at the margin (x 91). Line height ~25, pitch 46.
ocr::Line line_at(int x0, int y0, const std::vector<std::string>& words, int h = 25) {
    ocr::Line l;
    int x = x0;
    for (const auto& t : words) {
        ocr::Word w;
        w.text = t;
        w.conf = 95;
        w.box = {x, y0, x + 14 * int(t.size()), y0 + h};
        x = w.box.x1 + 20;
        l.words.push_back(w);
    }
    l.box = {x0, y0, std::max(x - 20, x0 + 1), y0 + h};
    return l;
}

ocr::Area area_of(K k, std::vector<ocr::Line> lines) {
    ocr::Area a;
    a.kind = k;
    ocr::Para p;
    p.box = lines.empty() ? ocr::Rect{} : lines[0].box;
    for (const auto& l : lines) p.box = ocr::unite(p.box, l.box);
    p.lines = std::move(lines);
    a.box = p.box;
    a.paras.push_back(std::move(p));
    return a;
}

ocr::Area heading() { return area_of(K::Heading, {line_at(400, 266, {"CHAPTER", "III"})}); }

std::vector<ocr::Area> missed_split() {
    return {heading(),
            area_of(K::Text, {line_at(210, 475, {"HE", "mill", "stood"}), line_at(210, 521, {"its", "wheel"}),
                              line_at(210, 567, {"dark,", "and"})}),
            area_of(K::Text, {line_at(91, 613, {"go", "near", "it"}), line_at(91, 659, {"the", "hill", "behind"})})};
}

// A reader that answers with `text` at the cap's ink box, and counts its calls.
struct Fake {
    std::string text = "T";
    ocr::Rect   ink{92, 476, 193, 590};
    int         calls = 0;
    ocr::Rect   asked;
    core::DropcapReader fn() {
        return [this](const ocr::Rect& r, ocr::Word& w) {
            ++calls;
            asked = r;
            w = ocr::Word{};
            w.text = text;
            if (!text.empty()) { w.box = ink; w.conf = 88; }
            return true;
        };
    }
};

const ocr::Para* cap_para(const std::vector<ocr::Area>& areas) {
    for (const auto& a : areas)
        for (const auto& p : a.paras)
            if (p.cap_letters > 0) return &p;
    return nullptr;
}

std::string para_text_of(const ocr::Para& p) { return ocr::para_text(p); }

std::string entry(const std::string& odt, const char* name) {
    int err = 0;
    zip_t* z = zip_open(odt.c_str(), ZIP_RDONLY, &err);
    if (!z) return {};
    std::string out;
    if (zip_file_t* f = zip_fopen(z, name, 0)) {
        char buf[4096];
        zip_int64_t n;
        while ((n = zip_fread(f, buf, sizeof buf)) > 0) out.append(buf, std::size_t(n));
        zip_fclose(f);
    }
    zip_close(z);
    return out;
}

fs::path temp_dir(const char* tag) {
    fs::path d = fs::temp_directory_path() / ("optik_dc_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(d, ec);
    return d;
}

}  // namespace

void dropcaps_tests() {
    std::cout << "-- dropcaps: the cap missed, the lines beside it indented (s32)\n";
    {
        auto areas = missed_split();
        Fake f;
        const int n = core::mark_dropcaps(areas, f.fn());
        const ocr::Para* p = cap_para(areas);
        check("missed: one cap found", n == 1 && p);
        if (p) {
            const auto& w = p->lines[0].words[0];
            check("missed: T + HE -> THE", w.text == "THE", w.text);
            check("missed: the spliced word wants a look", w.doubt);
            check("missed: one letter, three lines deep", p->cap_letters == 1 && p->cap_lines == 3,
                  std::to_string(p->cap_letters) + "/" + std::to_string(p->cap_lines));
            check("missed: the paragraph cut beside the cap is one again", p->lines.size() == 5 && areas.size() == 2,
                  para_text_of(*p));
            check("missed: the word's box reaches the cap", w.box.x0 == 92 && w.box.y0 == 475);
            check("missed: the area holds the cap", areas[1].box.x0 <= 92 && areas[1].box.y1 >= 590);
        }
        check("missed: the reader asked the gap left of the lines, as deep as they are",
              f.calls == 1 && f.asked.x1 <= 210 && f.asked.x0 < 91 && f.asked.y0 <= 475 && f.asked.y1 >= 592);
        check("missed: the heading is left alone", areas[0].paras[0].lines[0].words[0].text == "CHAPTER");
    }
    {
        // The same paragraph read as ONE area: the indented lines, then the margin.
        std::vector<ocr::Area> areas{heading(),
            area_of(K::Text, {line_at(210, 475, {"HE", "mill"}), line_at(210, 521, {"its", "wheel"}),
                              line_at(91, 567, {"go", "near"}), line_at(91, 613, {"the", "hill"})})};
        Fake f;
        f.ink = {92, 476, 193, 545};
        core::mark_dropcaps(areas, f.fn());
        const ocr::Para* p = cap_para(areas);
        check("in one paragraph: found, two lines deep", p && p->cap_lines == 2 && p->lines[0].words[0].text == "THE");
        check("in one paragraph: its lines kept", p && p->lines.size() == 4);
    }
    {
        // The rest below starts a paragraph of its own (indented): not joined.
        auto areas = missed_split();
        areas[2] = area_of(K::Text, {line_at(131, 613, {"Go", "near"}), line_at(91, 659, {"the", "hill"})});
        Fake f;
        core::mark_dropcaps(areas, f.fn());
        const ocr::Para* p = cap_para(areas);
        check("an indented paragraph below stays its own", p && p->lines.size() == 3 && areas.size() == 3);
    }

    std::cout << "-- dropcaps: what is not a cap\n";
    {
        auto areas = missed_split();
        const auto before = areas;
        Fake f;
        f.text = "";
        check("a gap that reads as nothing: no cap, nothing changed",
              core::mark_dropcaps(areas, f.fn()) == 0 && areas == before && f.calls == 1);
        f.text = "12";
        check("a gap that reads as digits: no cap", core::mark_dropcaps(areas, f.fn()) == 0 && areas == before);
        f.text = "Tour";
        check("a gap that reads as a word: no cap", core::mark_dropcaps(areas, f.fn()) == 0 && areas == before);
        f.text = "T";
        f.ink = {92, 476, 110, 500};
        check("a small letter is not a cap", core::mark_dropcaps(areas, f.fn()) == 0 && areas == before);
    }
    {
        // A paragraph's own first-line indent: one line, the reader never asked.
        std::vector<ocr::Area> areas{
            area_of(K::Text, {line_at(140, 475, {"It", "was"}), line_at(91, 521, {"a", "dark"}), line_at(91, 567, {"night"})})};
        Fake f;
        check("a first-line indent is not a cap's gap", core::mark_dropcaps(areas, f.fn()) == 0 && f.calls == 0);
    }
    {
        // Five lines indented over the margin: a block quote's depth, not a cap's.
        std::vector<ocr::Area> areas{area_of(K::Text, {line_at(210, 475, {"a"}), line_at(210, 521, {"b"}),
                                                        line_at(210, 567, {"c"}), line_at(210, 613, {"d"}),
                                                        line_at(210, 659, {"e"}), line_at(91, 705, {"f"})})};
        Fake f;
        check("five lines indented: not asked", core::mark_dropcaps(areas, f.fn()) == 0 && f.calls == 0);
    }
    {
        auto areas = missed_split();
        areas[1].user = true;
        const auto before = areas;
        Fake f;
        check("an area the author set is left alone", core::mark_dropcaps(areas, f.fn()) == 0 && areas == before);
    }
    {
        auto areas = missed_split();
        areas[1].paras[0].lines[0].words[0].checked = true;
        Fake f;
        core::mark_dropcaps(areas, f.fn());
        check("a word the author checked is not spliced", !cap_para(areas) &&
              areas[1].paras[0].lines[0].words[0].text == "HE");
    }
    {
        auto areas = missed_split();
        check("no reader: the missed shape is not guessed", core::mark_dropcaps(areas, {}) == 0);
    }
    {
        // Something else stands in the gap (a side note): the gap is not the cap's.
        auto areas = missed_split();
        areas.push_back(area_of(K::Text, {line_at(95, 520, {"note"})}));
        Fake f;
        check("a gap with another area in it is not read", core::mark_dropcaps(areas, f.fn()) == 0 && f.calls == 0);
    }

    std::cout << "-- dropcaps: the cap read as its own area\n";
    {
        auto areas = missed_split();
        ocr::Area cap = area_of(K::Text, {line_at(92, 476, {"T"}, 114)});
        cap.box = cap.paras[0].box = {92, 476, 193, 590};
        cap.paras[0].lines[0].words[0].box = cap.box;
        areas.insert(areas.begin() + 1, cap);
        Fake f;
        core::mark_dropcaps(areas, f.fn());
        const ocr::Para* p = cap_para(areas);
        check("own area: spliced from its own word, not re-read", p && p->lines[0].words[0].text == "THE" && f.calls == 0);
        bool lone = false;
        for (const auto& a : areas)
            if (a.box == ocr::Rect{92, 476, 193, 590}) lone = true;
        check("own area: the cap's area is gone", !lone);
        check("own area: three lines deep", p && p->cap_lines == 3);
    }
    {
        auto areas = missed_split();
        ocr::Area pic;
        pic.kind = K::Picture;
        pic.box = {88, 470, 198, 596};
        areas.insert(areas.begin() + 1, pic);
        Fake f;
        core::mark_dropcaps(areas, f.fn());
        bool picture = false;
        for (const auto& a : areas) picture = picture || a.kind == K::Picture;
        check("a picture that reads as T: read, spliced, and no picture left",
              cap_para(areas) && !picture && f.calls >= 1 && f.asked == pic.box);
    }
    {
        auto areas = missed_split();
        ocr::Area pic;
        pic.kind = K::Picture;
        pic.box = {88, 470, 198, 596};
        areas.insert(areas.begin() + 1, pic);
        Fake f;
        f.text = "";
        core::mark_dropcaps(areas, f.fn());
        bool picture = false;
        for (const auto& a : areas) picture = picture || a.kind == K::Picture;
        check("a picture that reads as nothing stays a picture (and blocks the gap)", picture && !cap_para(areas));
    }

    std::cout << "-- dropcaps: the cap read as a tall first word\n";
    {
        ocr::Line l0 = line_at(92, 475, {"T", "HE", "mill"});
        l0.words[0].box = {92, 476, 193, 590};
        l0.box.y1 = 590;
        std::vector<ocr::Area> areas{
            area_of(K::Text, {l0, line_at(210, 521, {"its", "wheel"}), line_at(210, 567, {"dark,"}),
                              line_at(91, 613, {"go", "near", "it"})})};
        core::mark_dropcaps(areas, {});
        const ocr::Para* p = cap_para(areas);
        check("tall word: T HE -> THE", p && p->lines[0].words[0].text == "THE" && p->lines[0].words.size() == 2);
        check("tall word: three lines deep, one letter", p && p->cap_lines == 3 && p->cap_letters == 1);
    }
    {
        ocr::Line l0 = line_at(92, 475, {"T"});
        l0.words[0].box = {92, 476, 193, 590};
        l0.box = l0.words[0].box;
        std::vector<ocr::Area> areas{area_of(K::Text, {l0, line_at(210, 480, {"HE", "mill"}),
                                                        line_at(210, 521, {"its"}), line_at(210, 567, {"dark,"})})};
        core::mark_dropcaps(areas, {});
        const ocr::Para* p = cap_para(areas);
        check("tall word alone in its line: into the next line's first word",
              p && p->lines.size() == 3 && p->lines[0].words[0].text == "THE");
    }

    std::cout << "-- dropcaps: kept, and exported (Rebuild / Strip)\n";
    const fs::path dir = temp_dir("book");
    core::Book b;
    if (core::Book::create(dir.string(), "Caps", b) != core::Status::Ok) {
        check("dropcaps: a book", false);
        return;
    }
    {
        auto areas = missed_split();
        Fake f;
        core::mark_dropcaps(areas, f.fn());
        core::Journal j;
        j.run(b, "page", [&](core::Edit& e) {
            core::Page pg;
            pg.crop = {0, 0, 1000, 1400};
            pg.read = true;
            pg.areas = areas;
            e.insert_page(0, pg);
        });
        check("kept: the book saves", b.save() == core::Status::Ok);
        core::Book back;
        check("kept: the cap comes back from book.json",
              core::Book::open(dir.string(), back) == core::Status::Ok && back.pages().size() == 1 &&
              cap_para(back.pages()[0].areas) && cap_para(back.pages()[0].areas)->cap_lines == 3 &&
              back.meta().settings.find_dropcaps && back.meta().settings.rebuild_dropcaps);

        const auto blocks = core::flow(b);
        const core::Block* para = nullptr;
        for (const auto& x : blocks) if (x.kind == core::Block::Paragraph) { para = &x; break; }
        check("rebuild: the paragraph carries its cap", para && para->cap_letters == 1 && para->cap_lines == 3 &&
              para->text.rfind("THE mill", 0) == 0, para ? para->text : "");
        const std::string odt = (dir / "rebuild.odt").string();
        check("rebuild: written", core::write_odt(blocks, "Caps", odt));
        const std::string xml = entry(odt, "content.xml");
        check("rebuild: a drop-cap style of one letter, three lines",
              xml.find("<style:drop-cap style:lines=\"3\" style:length=\"1\"") != std::string::npos);
        check("rebuild: the paragraph is in it, its text whole",
              xml.find("<text:p text:style-name=\"P_dc3_1\">THE mill") != std::string::npos);
        check("rebuild: the style is the body's with the cap",
              xml.find("style:name=\"P_dc3_1\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\"") !=
                  std::string::npos);

        j.run(b, "strip", [](core::Edit& e) { e.meta().settings.rebuild_dropcaps = false; });
        const auto plain = core::flow(b);
        bool any = false;
        for (const auto& x : plain) any = any || x.cap_letters > 0;
        check("strip: no block carries a cap", !any);
        const std::string odt2 = (dir / "strip.odt").string();
        core::write_odt(plain, "Caps", odt2);
        const std::string xml2 = entry(odt2, "content.xml");
        check("strip: no drop cap, the plain capital in the body",
              xml2.find("drop-cap") == std::string::npos &&
              xml2.find("<text:p text:style-name=\"Text_20_body\">THE mill") != std::string::npos);
        check("strip: undone, the cap is back", j.undo(b) && core::flow(b)[1].cap_letters == 1);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << "-- dropcaps: a rendered chapter opener with a three-line T\n";
    {
        ocr::Engine eng;
        std::string why;
        if (!eng.init("eng", &why)) {
            check("rendered: the English language data loads", false, why);
            return;
        }
        core::ReadRequest r;
        r.page_id   = "pg_x";
        r.scan_path = std::string(OPTIK_TEST_DATA) + "/dropcap.png";
        r.crop      = {0, 0, 1000, 1400};
        const core::Reading rd = core::read(eng, r);
        check("rendered: the page reads", rd.ok, rd.why);
        if (!rd.ok) return;
        const ocr::Para* p = cap_para(rd.areas);
        check("rendered: the cap is found", p != nullptr);
        if (p) {
            const std::string t = para_text_of(*p);
            const auto& w = p->lines[0].words[0];
            check("rendered: the first word starts with the cap's T and wants a look",
                  !w.text.empty() && w.text[0] == 'T' && w.doubt, w.text);
            check("rendered: two to four lines deep", p->cap_lines >= 2 && p->cap_lines <= 4,
                  std::to_string(p->cap_lines));
            check("rendered: the paragraph is one, to its last line", t.find("mill") != std::string::npos &&
                  t.find("evening") != std::string::npos, t);
        }
        bool lone = false;
        for (const auto& a : rd.areas)
            if (a.box.x1 < 205 && a.box.y0 > 400) lone = true;
        check("rendered: nothing is left standing in the cap's place", !lone);

        // The engine's own read of a gap: ink first (s32) -- a blank one reads
        // as nothing, not as the engine's guess at noise.
        ocr::Word blank, cap;
        const ocr::Look look{r.crop, 0, 0.0, false, true};
        check("rendered: a blank gap reads as nothing",
              eng.read_word(r.scan_path, look, {60, 800, 205, 950}, blank) && blank.text.empty(), blank.text);
        check("rendered: the cap's gap reads T, its ink box three lines deep",
              eng.read_word(r.scan_path, look, {60, 460, 205, 610}, cap) && cap.text == "T" &&
              cap.box.height() > 80 && cap.box.x1 < 205, cap.text);

        r.settings.find_dropcaps = false;
        const core::Reading off = core::read(eng, r);
        check("rendered: the pass off -- no cap", off.ok && !cap_para(off.areas));
    }
}
