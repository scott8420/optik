// selftest_export.cpp -- the export's options (s29, milestone 18): which pages,
// running heads kept, page breaks. The defaults are the export as it was; a
// range starts its own paragraphs; heads kept do not stop the page-turn join;
// breaks before headings never open the book; breaks at pages keep the pages
// (no join); ODT and text carry them; the window's range is clamped, not trusted.
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Undo.hpp"

#include <zip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;
using Opt = core::ExportOptions;

namespace {

fs::path temp_dir(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

ocr::Area area(ocr::Kind k, std::vector<std::vector<std::string>> lines, int y) {
    ocr::Area a;
    a.kind = k;
    a.box = {10, y, 900, y + 20 * int(lines.size())};
    ocr::Para p;
    for (auto& ws : lines) {
        ocr::Line l;
        for (auto& t : ws) { ocr::Word w; w.text = t; w.conf = 95; l.words.push_back(w); }
        p.lines.push_back(l);
    }
    a.paras.push_back(p);
    return a;
}

core::Page page(std::vector<ocr::Area> areas) {
    core::Page p;
    p.scan_id = "scn_1";
    p.crop = {0, 0, 1000, 1400};
    p.read = true;
    p.areas = std::move(areas);
    return p;
}

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

int count(const std::string& s, const std::string& what) {
    int n = 0;
    for (std::size_t at = 0; (at = s.find(what, at)) != std::string::npos; at += what.size()) ++n;
    return n;
}

int kinds(const std::vector<core::Block>& v, core::Block::Kind k) {
    return int(std::count_if(v.begin(), v.end(), [k](const core::Block& b) { return b.kind == k; }));
}
int breaks(const std::vector<core::Block>& v) {
    return int(std::count_if(v.begin(), v.end(), [](const core::Block& b) { return b.break_before; }));
}

bool same(const std::vector<core::Block>& a, const std::vector<core::Block>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].text != b[i].text || a[i].page_id != b[i].page_id ||
            a[i].break_before != b[i].break_before) return false;
    return true;
}

}  // namespace

void export_tests() {
    std::cout << "-- export: the options (s29)\n";
    const fs::path dir = temp_dir("export_opts");
    core::Book b;
    if (core::Book::create(dir.string(), "Options", b) != core::Status::Ok) {
        check("export options: a book", false);
        return;
    }
    core::Journal j;
    using K = ocr::Kind;
    // 1: heads, a paragraph broken at the turn, a folio. 2: its end, a folio.
    // 3: a chapter heading and a paragraph. 4: a paragraph.
    j.run(b, "pages", [](core::Edit& e) {
        e.insert_page(0, page({area(K::Header, {{"THE", "BOOK"}}, 10),
                               area(K::Text, {{"It", "was", "a"}, {"dark", "and", "storm-"}}, 100),
                               area(K::Footer, {{"1"}}, 1350)}));
        e.insert_page(1, page({area(K::Header, {{"THE", "BOOK"}}, 10),
                               area(K::Text, {{"y", "night.", "The", "end", "came."}}, 100),
                               area(K::Footer, {{"2"}}, 1350)}));
        e.insert_page(2, page({area(K::Heading, {{"CHAPTER", "II"}}, 200),
                               area(K::Text, {{"Morning", "came."}}, 300)}));
        e.insert_page(3, page({area(K::Text, {{"It", "was", "late."}}, 100)}));
    });
    const std::string P2 = b.pages()[1].id, P3 = b.pages()[2].id;

    const auto plain = core::flow(b);
    check("options: the defaults are the export as it was", same(core::flow(b, Opt{}), plain) &&
          plain.size() == 4 && plain[0].text == "It was a dark and stormy night. The end came." &&
          kinds(plain, core::Block::RunningHead) == 0 && breaks(plain) == 0,
          plain.empty() ? "" : plain[0].text);

    // -- the page range
    Opt o;
    o.first = 2; o.last = 3;
    auto v = core::flow(b, o);
    check("range: pages 2-3 start at page 2's own words (no join from outside)",
          v.size() == 3 && v[0].text == "y night. The end came." && v[0].page_id == P2 &&
          v[1].kind == core::Block::Heading && v[2].text == "Morning came.",
          v.empty() ? "" : v[0].text);
    o.first = 3; o.last = 3;
    v = core::flow(b, o);
    check("range: one page (This Page)", v.size() == 2 && v[0].page_id == P3 && v[1].page_id == P3);
    o.first = 3; o.last = 0;
    check("range: to 0 is to the last page", core::flow(b, o).size() == 3);
    o.first = -4; o.last = 99;
    check("range: past either end is clamped to the book", same(core::flow(b, o), plain));
    const core::PageSpan sp = core::span_of(b, o);
    check("range: the span says which pages", sp.from == 0 && sp.to == 3);
    o.first = 4; o.last = 2;
    check("range: inside out is no pages, nothing out", core::span_of(b, o).empty() && core::flow(b, o).empty());

    // -- running heads kept
    o = Opt{};
    o.keep_heads = true;
    v = core::flow(b, o);
    int at_para = -1, at_folio = -1;
    for (int i = 0; i < int(v.size()); ++i) {
        if (v[std::size_t(i)].text == "It was a dark and stormy night. The end came.") at_para = i;
        if (v[std::size_t(i)].text == "1" && v[std::size_t(i)].kind == core::Block::RunningHead) at_folio = i;
    }
    check("heads kept: every head and folio is a Running Head", kinds(v, core::Block::RunningHead) == 4);
    check("heads kept: the paragraph over the page turn is still joined", at_para >= 0);
    check("heads kept: the folio between follows the joined paragraph", at_para >= 0 && at_folio > at_para,
          std::to_string(at_para) + " " + std::to_string(at_folio));
    check("heads kept: the text is the same text", kinds(v, core::Block::Paragraph) == kinds(plain, core::Block::Paragraph));

    // -- page breaks
    o = Opt{};
    o.breaks = Opt::AtHeadings;
    v = core::flow(b, o);
    check("breaks at headings: the chapter starts a page",
          breaks(v) == 1 && v.size() == 4 && v[1].kind == core::Block::Heading && v[1].break_before);
    o.first = 3;
    v = core::flow(b, o);
    check("breaks at headings: never before the first block out",
          !v.empty() && v[0].kind == core::Block::Heading && !v[0].break_before && breaks(v) == 0);
    o = Opt{};
    o.breaks = Opt::AtPages;
    v = core::flow(b, o);
    check("breaks at pages: each page after the first starts one, no join over the turn",
          v.size() == 5 && breaks(v) == 3 && v[0].text == "It was a dark and storm-" && !v[0].break_before &&
          v[1].break_before && v[1].page_id == P2,
          std::to_string(v.size()) + " blocks, " + std::to_string(breaks(v)) + " breaks");
    o.keep_heads = true;
    v = core::flow(b, o);
    check("breaks at pages, heads kept: the page's head carries the break",
          breaks(v) == 3 && v.size() > 3 && v[3].kind == core::Block::RunningHead && v[3].break_before &&
          v[3].page_id == P2);

    // -- the writers
    const std::string odt = (dir / "opts.odt").string();
    const bool wrote = core::write_odt(v, "Options", odt);
    const std::string xml = entry(odt, "content.xml"), styles = entry(odt, "styles.xml");
    check("odt: written with heads and breaks", wrote && !xml.empty());
    check("odt: a break is an automatic style on the named one",
          xml.find("style:name=\"P_break\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\"") != std::string::npos &&
          xml.find("fo:break-before=\"page\"") != std::string::npos &&
          xml.find("<office:automatic-styles>") < xml.find("<office:body>"));
    check("odt: each break is used once per page turned",
          count(xml, "text:style-name=\"P_break\"") + count(xml, "text:style-name=\"H_break\"") +
          count(xml, "text:style-name=\"R_break\"") == 3);
    check("odt: Running Head is a named style Writer can restyle",
          styles.find("style:name=\"Running_20_Head\" style:display-name=\"Running Head\"") != std::string::npos &&
          count(xml, "text:style-name=\"Running_20_Head\"") + count(xml, "text:style-name=\"R_break\"") == 4);
    const std::string odt0 = (dir / "plain.odt").string();
    core::write_odt(plain, "Options", odt0);
    const std::string xml0 = entry(odt0, "content.xml");
    check("odt: without options, no break is used",
          count(xml0, "_break\">") == 0 && count(xml0, "text:style-name=\"Running_20_Head\"") == 0);

    const std::string txt = (dir / "opts.txt").string();
    std::string t;
    if (core::write_text(v, txt)) { std::ifstream f(txt, std::ios::binary); t.assign(std::istreambuf_iterator<char>(f), {}); }
    check("text: a page break is a form feed", count(t, "\f") == 3);
    const std::string txt0 = (dir / "plain.txt").string();
    std::string t0;
    if (core::write_text(plain, txt0)) { std::ifstream f(txt0, std::ios::binary); t0.assign(std::istreambuf_iterator<char>(f), {}); }
    check("text: without breaks, none", !t0.empty() && count(t0, "\f") == 0);

    // The book is not touched by an export's choices.
    check("options: exporting is no journal step", j.size() == 1);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
