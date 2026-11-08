// selftest_format.cpp -- the format bar's verbs and their export (s28).
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Format.hpp"
#include "core/Undo.hpp"

#include <zip.h>

#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

ocr::Area words(std::vector<std::string> ts) {
    ocr::Area a;
    ocr::Para p;
    ocr::Line l;
    for (auto& t : ts) { ocr::Word w; w.text = t; w.conf = 95; l.words.push_back(w); }
    p.lines.push_back(l);
    a.paras.push_back(p);
    return a;
}

std::string content_of(const std::string& path) {
    int err = 0;
    zip_t* z = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!z) return {};
    std::string out;
    if (zip_file_t* f = zip_fopen(z, "content.xml", 0)) {
        char buf[4096];
        zip_int64_t n;
        while ((n = zip_fread(f, buf, sizeof buf)) > 0) out.append(buf, std::size_t(n));
        zip_fclose(f);
    }
    zip_close(z);
    return out;
}

}  // namespace

void format_tests() {
    std::cout << "-- format: the bar's verbs (s28)\n";
    const fs::path dir = fs::temp_directory_path() / ("optik_selftest_fmt_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    core::Book b;
    core::Book::create(dir.string(), "", b);
    core::Journal j;
    j.run(b, "page", [](core::Edit& e) {
        core::Page p;
        p.crop = {0, 0, 100, 100};
        p.areas = {words({"The", "Secret", "of", "the", "Old", "Clock", "was", "read.1"})};
        p.read = true;
        e.insert_page(0, p);
    });
    const std::string id = b.pages()[0].id;
    auto hit = [&](int w) { return core::Hit{id, 0, 0, 0, w}; };
    auto w = [&](int i) -> const ocr::Word& { return b.pages()[0].areas[0].paras[0].lines[0].words[std::size_t(i)]; };

    const std::vector<core::Hit> title = {hit(1), hit(2), hit(3), hit(4), hit(5)};
    check("format: none have it yet", !core::all_have(b, title, ocr::kBold));
    const core::Book before = b;
    j.run(b, "bold", [&](core::Edit& e) { core::set_format(e, title, ocr::kBold, true); });
    check("format: bold on five words, one step", j.size() == 2 && w(1).bold && w(5).bold && !w(0).bold && !w(6).bold);
    check("format: all have it now", core::all_have(b, title, ocr::kBold));
    // A toggle on a mixed set puts it on all; on a set that all have it, off.
    const std::vector<core::Hit> mixed = {hit(0), hit(1)};
    check("format: a mixed set is not 'all'", !core::all_have(b, mixed, ocr::kBold));
    j.run(b, "italic", [&](core::Edit& e) { core::set_format(e, {hit(1), hit(2)}, ocr::kItalic, true); });
    j.run(b, "sup", [&](core::Edit& e) { core::set_format(e, {hit(7)}, ocr::kSuperscript, true); });
    j.run(b, "sc", [&](core::Edit& e) { core::set_format(e, {hit(6)}, ocr::kSmallCaps, true); });
    j.run(b, "u", [&](core::Edit& e) { core::set_format(e, {hit(6)}, ocr::kUnderline, true); });
    const bool nothing = j.run(b, "again", [&](core::Edit& e) { core::set_format(e, {hit(6)}, ocr::kUnderline, true); });
    check("format: setting what is set is no step", !nothing);
    const std::vector<core::Hit> stale = {core::Hit{id, 0, 0, 0, 99}};   // named: GCC 16 -Wdangling-pointer
    const bool stale_all = core::all_have(b, stale, ocr::kBold);
    const bool stale_ran = j.run(b, "stale", [&](core::Edit& e) { core::set_format(e, stale, ocr::kBold, true); });
    check("format: a stale word is skipped", !stale_all && !stale_ran);
    check("format: a format is not a check", !w(1).checked && !w(1).edited);

    std::cout << "-- format: export as Writer character styles\n";
    const auto blocks = core::flow(b);
    const std::string i_b = core::fmt_open(ocr::kItalic | ocr::kBold), bo = core::fmt_open(ocr::kBold);
    const std::string want = "The " + i_b + "Secret of" + std::string(1, core::kFmtOff) + " " + bo + "the Old Clock" +
                             core::kFmtOff + " " + core::fmt_open(ocr::kUnderline | ocr::kSmallCaps) + "was" +
                             core::kFmtOff + " " + core::fmt_open(ocr::kSuperscript) + "read.1" + core::kFmtOff;
    check("export: runs of one format fuse, a change of format starts a run",
          blocks.size() == 1 && blocks[0].text == want, blocks.empty() ? "" : core::plain(blocks[0].text));
    const std::string odt = (dir / "f.odt").string();
    core::write_odt(blocks, "t", odt);
    const std::string xml = content_of(odt);
    // s31: one named style a run (its first format's), the rest an automatic
    // style inside it -- Composix keeps one character style a run.
    check("export: italic + bold is Emphasis with bold inside it (T2), declared",
          xml.find("<text:span text:style-name=\"Emphasis\"><text:span text:style-name=\"T2\">Secret of</text:span></text:span>") !=
              std::string::npos &&
          xml.find("<style:style style:name=\"T2\" style:family=\"text\"><style:text-properties fo:font-weight=\"bold\"/>") !=
              std::string::npos);
    check("export: bold alone is Strong Emphasis, no automatic style",
          xml.find("<text:span text:style-name=\"Strong_20_Emphasis\">the Old Clock</text:span>") != std::string::npos);
    check("export: underline + small caps is Underline with small caps inside (T8); superscript its style",
          xml.find("<text:span text:style-name=\"Underline\"><text:span text:style-name=\"T8\">was</text:span></text:span>") != std::string::npos &&
          xml.find("<style:style style:name=\"T8\" style:family=\"text\"><style:text-properties fo:font-variant=\"small-caps\"/>") != std::string::npos &&
          xml.find("<text:span text:style-name=\"Superscript\">read.1</text:span>") != std::string::npos);
    check("export: no named style nests in another",
          xml.find("\"><text:span text:style-name=\"Strong_20_Emphasis\"") == std::string::npos &&
          xml.find("\"><text:span text:style-name=\"Small_20_Caps\"") == std::string::npos);
    check("export: plain text has the words only", core::plain(blocks[0].text) == "The Secret of the Old Clock was read.1");

    std::cout << "-- format: kept, undone\n";
    b.save();
    core::Book back;
    std::string why;
    check("book.json: every format survives save and open",
          core::Book::open(dir.string(), back) == core::Status::Ok && core::same_book(b, back, &why) &&
          ocr::fmt_of(back.pages()[0].areas[0].paras[0].lines[0].words[6]) == (ocr::kUnderline | ocr::kSmallCaps), why);
    while (j.position() > 1) j.undo(b);
    check("format: undo to before the formats is the book before", core::same_book(b, before, &why), why);
    fs::remove_all(dir, ec);
}
