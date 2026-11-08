// selftest_composix.cpp -- the ODT as Composix's way in (s31, milestones 22-24).
// Composix's File > Import ODT lays the book out; what it reads must be there:
//   22  a chapter's label over its title is Heading 1 over a Subtitle (a line
//       break inside a heading made two paragraphs there, each starting a page);
//       other heading lines run on; a run of two formats is ONE named style
//   23  the book's page: its trim and margins from the scans' resolution and
//       the text's bounds, facing pages (inner / outer); unknown -> Writer's
//   24  pictures: picture areas cut from the page as read, embedded as PNG at
//       their printed size (never past the text area), where they stand; a
//       paragraph broken by one still joins
// The rules are asserted, not a machine's numbers: synthetic scans of known size
// and resolution, so the outcome is certain anywhere.
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Undo.hpp"

#include <leptonica/allheaders.h>
#include <zip.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;
using core::Block;

namespace {

fs::path temp_dir(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

std::string entry(const std::string& odt, const char* name, bool* stored = nullptr) {
    int err = 0;
    zip_t* z = zip_open(odt.c_str(), ZIP_RDONLY, &err);
    if (!z) return {};
    std::string out;
    if (stored) {
        zip_stat_t st;
        *stored = zip_stat(z, name, 0, &st) == 0 && st.comp_method == ZIP_CM_STORE;
    }
    if (zip_file_t* f = zip_fopen(z, name, 0)) {
        char buf[4096];
        zip_int64_t n;
        while ((n = zip_fread(f, buf, sizeof buf)) > 0) out.append(buf, std::size_t(n));
        zip_fclose(f);
    }
    zip_close(z);
    return out;
}

int count(const std::string& s, const std::string& what) {
    int n = 0;
    for (std::size_t at = 0; (at = s.find(what, at)) != std::string::npos; at += what.size()) ++n;
    return n;
}

bool near(double a, double b, double tol = 0.01) { return std::fabs(a - b) <= tol; }

ocr::Area area(ocr::Kind k, std::vector<std::vector<std::string>> lines, ocr::Rect box) {
    ocr::Area a;
    a.kind = k;
    a.box = box;
    ocr::Para p;
    for (auto& ws : lines) {
        ocr::Line l;
        for (auto& t : ws) { ocr::Word w; w.text = t; w.conf = 95; l.words.push_back(w); }
        p.lines.push_back(l);
    }
    if (!p.lines.empty()) a.paras.push_back(p);
    return a;
}

// A PNG of `w` x `h` (white, a grey block at `pic`), its resolution `ppi` (0: none said).
bool write_scan(const std::string& path, int w, int h, int ppi, ocr::Rect pic) {
    Pix* p = pixCreate(w, h, 8);
    if (!p) return false;
    pixSetAllArbitrary(p, 255);
    if (!pic.empty()) {
        Box* b = boxCreate(pic.x0, pic.y0, pic.width(), pic.height());
        pixSetInRectArbitrary(p, b, 90);
        boxDestroy(&b);
    }
    pixSetResolution(p, ppi, ppi);
    const bool ok = pixWrite(path.c_str(), p, IFF_PNG) == 0;
    pixDestroy(&p);
    return ok;
}

std::string png_of(int w, int h) {
    Pix* p = pixCreate(w, h, 8);
    pixSetAllArbitrary(p, 128);
    l_uint8* d = nullptr;
    size_t n = 0;
    pixWriteMem(&d, &n, p, IFF_PNG);
    std::string s(reinterpret_cast<const char*>(d), n);
    lept_free(d);
    pixDestroy(&p);
    return s;
}

}  // namespace

void composix_tests() {
    std::cout << "-- composix (m22): a chapter's label, its title\n";
    check("label: CHAPTER II, Chapter the Fourth, BOOK THREE, Part 2:",
          core::is_chapter_label("CHAPTER II") && core::is_chapter_label("Chapter the Fourth") &&
          core::is_chapter_label("BOOK THREE") && core::is_chapter_label("Part 2:"));
    check("label: a number or a numeral alone (12., XIV)",
          core::is_chapter_label("12.") && core::is_chapter_label("XIV") && core::is_chapter_label(" 3 "));
    check("label: a formatted label is still one", core::is_chapter_label(core::fmt_open(ocr::kItalic) + "Chapter" + "\x02 I"));
    check("label: a title is not (THE MAN WHO, AN IVORY CHARM, empty, a long line)",
          !core::is_chapter_label("THE MAN WHO") && !core::is_chapter_label("AN IVORY CHARM") &&
          !core::is_chapter_label("") && !core::is_chapter_label("Chapter one in which we meet") &&
          !core::is_chapter_label("1914 AND AFTER"));

    const fs::path dir = temp_dir("composix");
    fs::create_directories(dir);
    {
        std::vector<Block> v;
        v.emplace_back(Block::Heading, "CHAPTER II\nAN IVORY CHARM", "p1", true);
        v.emplace_back(Block::Paragraph, "Morning came.", "p1");
        v.emplace_back(Block::Heading, "THE MAN WHO\nCAME TO DINNER", "p2", true);
        v.emplace_back(Block::Heading, "Chapter III\nTHE LONG\nNIGHT", "p3");
        v.emplace_back(Block::Heading, "CHAPTER IV", "p4");
        const std::string odt = (dir / "heads.odt").string();
        core::write_odt(v, "Heads", odt);
        const std::string xml = entry(odt, "content.xml");
        check("heading: label over title -> Heading 1 (the break) then Subtitle",
              xml.find("<text:h text:style-name=\"H_break\" text:outline-level=\"1\">CHAPTER II</text:h>\n"
                       "   <text:p text:style-name=\"Subtitle\">AN IVORY CHARM</text:p>") != std::string::npos);
        check("heading: no line break left in any heading (Composix: one paragraph each)",
              count(xml, "<text:line-break/>") == 0);
        check("heading: a title's wrap runs on with a space",
              xml.find(">THE MAN WHO CAME TO DINNER</text:h>") != std::string::npos);
        check("heading: a title over two lines under a label is one Subtitle",
              xml.find(">Chapter III</text:h>\n   <text:p text:style-name=\"Subtitle\">THE LONG NIGHT</text:p>") != std::string::npos);
        check("heading: a label alone stays a heading, no Subtitle",
              xml.find(">CHAPTER IV</text:h>") != std::string::npos && count(xml, "\"Subtitle\"") == 2);
        check("heading: four headings in the outline, one break each as asked",
              count(xml, "text:outline-level=\"1\"") == 4 && count(xml, "text:style-name=\"H_break\"") == 2);
        check("styles: Subtitle is defined, centred", entry(odt, "styles.xml").find(
              "<style:style style:name=\"Subtitle\" style:family=\"paragraph\"") != std::string::npos);
        check("page: none measured -> no page layout (Writer's own)",
              entry(odt, "styles.xml").find("style:page-layout") == std::string::npos);
    }

    std::cout << "-- composix (m23): the book's page\n";
    {
        // 5.5 x 8.5 in at 300 ppi; a recto's text 0.75 in from the inner (left)
        // edge, 0.4 in from the outer; the verso the mirror of it.
        const std::vector<core::Measured> m{
            {1650, 2550, {225, 225, 1530, 2325}, false},
            {1650, 2550, {120, 225, 1425, 2325}, true},
            {1650, 2550, {120, 300, 1425, 2325}, true}};
        const core::PageSize ps = core::page_size(m, 300);
        check("trim: 1650 x 2550 px at 300 ppi is 396 x 612 pt (5.5 x 8.5 in)",
              ps.valid() && near(ps.w, 396) && near(ps.h, 612), std::to_string(ps.w) + " x " + std::to_string(ps.h));
        check("margins: inner / outer from recto and verso alike (54 / 28.8 pt)",
              near(ps.inner, 54) && near(ps.outer, 28.8), std::to_string(ps.inner) + " / " + std::to_string(ps.outer));
        check("margins: the median page's (top 54, not the odd 72)", near(ps.top, 54) && near(ps.bottom, 54));
        check("resolution: unknown or a default (0, 72, 96) -> no page",
              !core::page_size(m, 0).valid() && !core::page_size(m, 72).valid() && !core::page_size(m, 96).valid() &&
              // (a small scan, so only the resolution can refuse it: 700 px at 72 is a plausible 9.7 in)
              !core::page_size({{700, 1000, {}, false}}, 72).valid() && core::page_size({{700, 1000, {}, false}}, 150).valid());
        check("resolution: a page past 20 in (a wrong one) -> no page",
              !core::page_size({{9000, 12000, {}, false}}, 300).valid());
        const core::PageSize wide = core::page_size({{1650, 2550, {800, 1200, 900, 1300}, false}}, 300);
        check("margins: never more than a third of the page", wide.valid() && wide.inner <= wide.w / 3 + 0.01 &&
              wide.top <= wide.h / 3 + 0.01);
        check("no pages -> no page", !core::page_size({}, 300).valid());

        core::OdtBook ob;
        ob.title = "Paged";
        ob.page = ps;
        const std::string odt = (dir / "paged.odt").string();
        core::write_odt({Block(Block::Paragraph, "Text.", "p1")}, ob, odt);
        const std::string st = entry(odt, "styles.xml");
        check("ODT: the page layout, mirrored, its size and margins (inner left)",
              st.find("<style:page-layout style:name=\"pm1\" style:page-usage=\"mirrored\">") != std::string::npos &&
              st.find("fo:page-width=\"396.00pt\" fo:page-height=\"612.00pt\"") != std::string::npos &&
              st.find("fo:margin-left=\"54.00pt\" fo:margin-right=\"28.80pt\"") != std::string::npos);
        check("ODT: the Standard master page uses it",
              st.find("<style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\"/>") != std::string::npos);
    }

    std::cout << "-- composix (m24): pictures\n";
    {
        std::vector<Block> v;
        v.emplace_back(Block::Paragraph, "Before.", "p1");
        v.emplace_back(Block::Picture, "", "p1");
        v.emplace_back(Block::Paragraph, "Between.", "p1");
        v.emplace_back(Block::Picture, "", "p2", true);   // left out: its break passes on
        v.emplace_back(Block::Paragraph, "After.", "p2");
        v.emplace_back(Block::Picture, "", "p3");          // too wide for the text area
        core::OdtBook ob;
        ob.page = core::page_size({{1650, 2550, {225, 225, 1530, 2325}, false}}, 300);
        const std::string png = png_of(60, 30), big = png_of(80, 40);
        ob.art = {{png, 144, 72}, {}, {big, 800, 400}};
        const std::string odt = (dir / "pics.odt").string();
        const bool wrote = core::write_odt(v, ob, odt);
        const std::string xml = entry(odt, "content.xml"), man = entry(odt, "META-INF/manifest.xml");
        bool stored = false;
        check("pictures: written, each PNG in Pictures/ as it was, stored",
              wrote && entry(odt, "Pictures/picture1.png", &stored) == png && stored &&
              entry(odt, "Pictures/picture2.png") == big);
        check("pictures: one left out (no art) -- two frames, two manifest entries",
              count(xml, "<draw:frame ") == 2 && count(man, "media-type=\"image/png\"") == 2);
        check("pictures: as a character in a centred paragraph, its printed size",
              xml.find("<text:p text:style-name=\"P_pic\"><draw:frame draw:style-name=\"fr_pic\" draw:name=\"Picture 1\" "
                       "text:anchor-type=\"as-char\" svg:width=\"144.00pt\" svg:height=\"72.00pt\"") != std::string::npos);
        check("pictures: where they stand (after Before., before Between.)",
              xml.find("Before.") < xml.find("picture1.png") && xml.find("picture1.png") < xml.find("Between."));
        check("pictures: one left out passes its page break to what follows",
              xml.find("<text:p text:style-name=\"P_break\">After.</text:p>") != std::string::npos);
        // The text area: 396 - 54 - 28.8 = 313.2 wide; 800 x 400 -> 313.2 x 156.6.
        check("pictures: never wider than the text area (scaled, its shape kept)",
              xml.find("svg:width=\"313.20pt\" svg:height=\"156.60pt\"") != std::string::npos);
        check("pictures: their automatic styles declared",
              xml.find("style:name=\"P_pic\"") != std::string::npos && xml.find("style:name=\"fr_pic\" style:family=\"graphic\"") != std::string::npos);
        const std::string none = (dir / "none.odt").string();
        core::write_odt({Block(Block::Paragraph, "Text.", "p1")}, "None", none);
        check("pictures: none -> no picture styles, no Pictures/",
              entry(none, "content.xml").find("P_pic") == std::string::npos && entry(none, "Pictures/picture1.png").empty());
    }

    std::cout << "-- composix (m24): flowed, planned, cut from the scan\n";
    {
        core::Book b;
        if (core::Book::create((dir / "book").string(), "Charm", b) != core::Status::Ok) {
            check("composix: a book", false);
            return;
        }
        const ocr::Rect pic{400, 600, 1000, 900};
        const std::string sdir = b.folder() + "/scans";
        fs::create_directories(sdir);
        const bool scans = write_scan(sdir + "/a.png", 1650, 2550, 300, pic) && write_scan(sdir + "/b.png", 1650, 2550, 0, pic);
        check("scans: written (300 ppi, and one that says none)", scans);
        using K = ocr::Kind;
        core::Journal j;
        j.run(b, "pages", [&](core::Edit& e) {
            const auto& sa = e.add_scan(core::Scan{"", "a.png", "a.png"});
            core::Page p1;
            p1.scan_id = sa.id;
            p1.crop = {0, 0, 1650, 2550};
            p1.read = true;
            p1.areas = {area(K::Text, {{"It", "was", "a"}, {"dark", "and", "storm-"}}, {225, 225, 1530, 500}),
                        area(K::Picture, {}, pic)};
            e.insert_page(0, p1);
            core::Page p2 = p1;
            p2.areas = {area(K::Text, {{"y", "night."}}, {225, 225, 1530, 2325})};
            e.insert_page(1, p2);
        });
        auto v = core::flow(b);
        check("flow: a picture area is a Picture block, its box kept",
              v.size() == 2 && v[0].kind == Block::Paragraph && v[1].kind == Block::Picture && v[1].rect == pic);
        check("flow: the paragraph broken by the picture and the page turn still joins, the picture after it",
              !v.empty() && v[0].text == "It was a dark and stormy night.", v.empty() ? "" : v[0].text);
        core::ExportOptions no;
        no.pictures = false;
        check("flow: pictures off -> none", core::flow(b, no).size() == 1);

        const core::ExportPlan plan = core::plan_of(b, v);
        check("plan: one picture source (the scan, the box, levelling off)",
              plan.art.size() == 1 && plan.art[0].rect == pic && !plan.art[0].look.level &&
              plan.art[0].path == b.scan_path(b.pages()[0].scan_id));
        check("plan: the body page measured", plan.pages.size() == 1 && !plan.pages[0].text.empty());
        const core::OdtBook ob = core::realise(plan);
        check("realise: the page from the scan's 300 ppi (396 x 612 pt)",
              ob.page.valid() && near(ob.page.w, 396) && near(ob.page.h, 612));
        int pw = 0, ph = 0;
        bool grey = false;
        if (!ob.art.empty() && !ob.art[0].png.empty()) {
            Pix* p = pixReadMem(reinterpret_cast<const l_uint8*>(ob.art[0].png.data()), ob.art[0].png.size());
            if (p) {
                pw = int(pixGetWidth(p));
                ph = int(pixGetHeight(p));
                Pix* g = pixConvertTo8(p, 0);
                l_uint32 val = 0;
                if (g) { pixGetPixel(g, pw / 2, ph / 2, &val); pixDestroy(&g); }
                grey = val > 40 && val < 140;   // the block's grey, not paper, not levelled to black
                pixDestroy(&p);
            }
        }
        check("realise: the picture cut to its box (600 x 300 px), its grey kept",
              ob.art.size() == 1 && pw == 600 && ph == 300 && grey, std::to_string(pw) + " x " + std::to_string(ph));
        check("realise: its printed size at 300 ppi (144 x 72 pt)",
              !ob.art.empty() && near(ob.art[0].w, 144) && near(ob.art[0].h, 72));
        check("realise: cancelled -> no pictures cut",
              core::realise(plan, [] { return true; }).art.size() == 1 &&
              core::realise(plan, [] { return true; }).art[0].png.empty());
        int asked = 0;   // cancelled after the page is measured, before the pictures
        const core::OdtBook late = core::realise(plan, [&asked] { return asked++ >= 1; });
        check("realise: cancelled between the page and the pictures -> the page, no pictures",
              late.page.valid() && late.art.size() == 1 && late.art[0].png.empty());

        // A scan that doesn't say its resolution: no page; the picture at 300.
        j.run(b, "scan b", [&](core::Edit& e) {
            const auto& sb = e.add_scan(core::Scan{"", "b.png", "b.png"});
            core::Page p = b.pages()[0];
            p.id.clear();
            p.scan_id = sb.id;
            e.insert_page(0, p);
        });
        core::ExportOptions first;
        first.last = 1;
        const auto v2 = core::flow(b, first);
        const core::OdtBook nb = core::realise(core::plan_of(b, v2));
        check("realise: no resolution said -> Writer's page; the picture still cut, at 300",
              v2.size() == 2 && !nb.page.valid() && nb.art.size() == 1 && !nb.art[0].png.empty() && near(nb.art[0].w, 144));

        const std::string odt = (dir / "book.odt").string();
        check("the whole way: the ODT with its page and its picture",
              !ob.art.empty() && core::write_odt(v, ob, odt) && entry(odt, "styles.xml").find("396.00pt") != std::string::npos &&
              entry(odt, "Pictures/picture1.png") == ob.art[0].png);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
