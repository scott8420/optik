// selftest_find.cpp -- search and replace across the book (s27, milestone 15).
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/Find.hpp"
#include "core/Undo.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

ocr::Area area_of(std::vector<std::vector<std::string>> lines, ocr::Kind k = ocr::Kind::Text) {
    ocr::Area a;
    a.kind = k;
    ocr::Para p;
    for (auto& l : lines) {
        ocr::Line ln;
        for (auto& t : l) { ocr::Word w; w.text = t; w.conf = 60; ln.words.push_back(w); }
        p.lines.push_back(ln);
    }
    a.paras.push_back(p);
    return a;
}

std::string at(const core::Book& b, const core::Hit& h) {
    return b.page(h.page_id)->areas[std::size_t(h.area)].paras[std::size_t(h.para)]
        .lines[std::size_t(h.line)].words[std::size_t(h.word)].text;
}

}  // namespace

void find_tests() {
    std::cout << "-- find: in a word (s27)\n";
    {
        core::FindOptions any, cased, whole;
        cased.match_case = true;
        whole.whole_words = true;
        check("find: case aside by default", core::find_in("Tbe", "tbe", any).size() == 1);
        check("find: match case", core::find_in("Tbe", "tbe", cased).empty());
        check("find: every occurrence, not overlapping", core::find_in("aaaa", "aa", any) == std::vector<std::size_t>{0, 2});
        check("find: whole words skips a part of a word", core::find_in("other", "the", whole).empty() &&
              core::find_in("\xE2\x80\x9Cthe,", "the", whole).size() == 1 &&
              core::find_in("caf\xC3\xA9", "caf", whole).empty());
        check("find: an empty query finds nothing", core::find_in("abc", "", any).empty());
        int n = 0;
        check("replace: every occurrence in the word",
              core::replace_in("rnay rnore", "rn", "m", any, &n) == "may more" && n == 2);
        check("replace: case aside keeps the rest", core::replace_in("TBE tbe", "tbe", "the", any, &n) == "the the" && n == 2);
    }

    std::cout << "-- find: across the book\n";
    {
        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_find_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "pages", [](core::Edit& e) {
            core::Page p1;
            p1.crop = {0, 0, 100, 100};
            p1.areas = {area_of({{"THE", "MYSTERY"}}, ocr::Kind::Header), area_of({{"tbe", "cat"}, {"and", "tbe", "dog."}})};
            core::Page p2;
            p2.crop = {0, 0, 100, 100};
            p2.areas = {area_of({{"Tbe", "end", "other"}})};
            e.insert_page(0, p1);
            e.insert_page(1, p2);
        });
        core::FindOptions o;
        const auto hits = core::find_all(b, "tbe", o);
        check("find all: in book order, every page", hits.size() == 3 && hits[0].page_id == b.pages()[0].id &&
              hits[0].area == 1 && hits[1].line == 1 && hits[2].page_id == b.pages()[1].id,
              std::to_string(hits.size()));
        check("find all: running heads are searched too", core::find_all(b, "MYSTERY", o).size() == 1);

        const core::Book before = b;
        int n = 0;
        j.run(b, "replace all", [&](core::Edit& e) { n = core::replace_hits(e, hits, "tbe", "the", o); });
        const auto& w = b.pages()[0].areas[1].paras[0].lines[0].words[0];
        check("replace all: every match, one step", n == 3 && j.size() == 2 && at(b, hits[2]) == "the");
        check("replace all: a replaced word is the author's edit (original kept, no longer to check)",
              w.edited && w.original == "tbe" && !b.uncertain(w));
        check("replace all: the audit is as before", core::audit(b).size() == core::audit(before).size());
        j.undo(b);
        std::string why;
        check("replace all: one undo puts every page back", core::same_book(b, before, &why), why);
        j.redo(b);

        // Replacing back to the engine's reading is no longer an edit.
        core::FindOptions cased;
        cased.match_case = true;
        const auto back = core::find_all(b, "the", cased);
        j.run(b, "back", [&](core::Edit& e) { core::replace_hits(e, {back[0]}, "the", "tbe", cased); });
        const auto& w2 = b.pages()[0].areas[1].paras[0].lines[0].words[0];
        check("replace: back to the engine's reading is not an edit", w2.text == "tbe" && !w2.edited && w2.original.empty());

        // A hit gone stale (its page deleted) is skipped, not a crash.
        core::Hit stale = hits[2];
        stale.word = 99;
        bool changed = j.run(b, "stale", [&](core::Edit& e) { core::replace_hits(e, {stale}, "tbe", "x", o); });
        check("replace: a stale hit is skipped", !changed);
        fs::remove_all(dir, ec);
    }

    std::cout << "-- find: grep (s30, Scott)\n";
    {
        core::FindOptions g;
        g.grep = true;
        core::FindOptions gc = g;
        gc.match_case = true;
        core::FindOptions gw = g;
        gw.whole_words = true;
        std::string why;
        check("grep: a class and a repeat", core::find_in("x1y22z333", "[0-9]+", g) == std::vector<std::size_t>{1, 3, 6});
        check("grep: alternation", core::find_in("tbe", "t(b|h)e", g).size() == 1 && core::find_in("the", "t(b|h)e", g).size() == 1);
        check("grep: ^ and $ are the word's ends", core::find_in("rnore", "^rn", g).size() == 1 &&
              core::find_in("barn", "^rn", g).empty() && core::find_in("barn", "rn$", g).size() == 1);
        check("grep: ^ is not found again later in the word", core::find_in("aaa", "^a", g).size() == 1);
        check("grep: case aside by default, kept on Match case",
              core::find_in("Tbe", "tbe", g).size() == 1 && core::find_in("Tbe", "tbe", gc).empty());
        check("grep: an empty match is not a hit, and does not hang", core::find_in("abc", "x*", g).empty() &&
              core::find_in("abc", "b*", g) == std::vector<std::size_t>{1});
        check("grep: whole words holds for a pattern too",
              core::find_in("other", "th[ae]", gw).empty() && core::find_in("\xE2\x80\x9Cthe,", "th[ae]", gw).size() == 1);
        check("grep: a dot is any byte, a backslash makes it a dot",
              core::find_in("a.b", "a\\.b", g).size() == 1 && core::find_in("axb", "a\\.b", g).empty());
        check("grep: a bad pattern says why", !core::pattern_ok("t(b", g, &why) && !why.empty(), why);
        check("grep: a bad pattern finds nothing (no throw)", core::find_in("tbe", "t(b", g).empty());
        check("grep: off, the same query is plain text", core::find_in("t(b", "t(b", core::FindOptions{}).size() == 1 &&
              core::pattern_ok("t(b", core::FindOptions{}));
        int n = 0;
        check("replace grep: the match as written", core::replace_in("rnay", "^rn", "m", g, &n) == "may" && n == 1);
        check("replace grep: \\1 is a group", core::replace_in("Nancv", "(Nanc)v", "\\1y", g) == "Nancy");
        check("replace grep: & is the whole match, \\& an ampersand",
              core::replace_in("12", "[0-9]+", "<&>", g) == "<12>" && core::replace_in("a", "a", "\\&", g) == "&");
        check("replace grep: every match in the word", core::replace_in("1a2b3", "[0-9]", "#", g, &n) == "#a#b#" && n == 3);
        check("replace plain: & and \\1 are themselves", core::replace_in("tbe", "tbe", "&\\1", core::FindOptions{}) == "&\\1");

        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_grep_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "pages", [](core::Edit& e) {
            core::Page p;
            p.scan_id = e.add_scan(core::Scan{"", "x.png", "x.png"}).id;
            p.crop = {0, 0, 100, 100};
            p.read = true;
            p.areas.push_back(area_of({{"rnore", "tbe", "barn"}, {"Nancv", "12", "tbe."}}));
            e.insert_page(0, p);
            p.areas = {area_of({{"tbe", "rnay"}})};
            e.insert_page(1, p);
        });
        const auto hits = core::find_all(b, "^(tb|rn)", g);
        check("grep all: every word, in book order", hits.size() == 5 && at(b, hits[0]) == "rnore" &&
              at(b, hits[4]) == "rnay", std::to_string(hits.size()));
        int done = 0;
        j.run(b, "Replace All", [&](core::Edit& e) { done = core::replace_hits(e, hits, "^tb", "th", g); });
        const auto after = core::find_all(b, "^th", g);
        check("grep all: replaced where the pattern holds, one step", done == 3 && after.size() == 3 && j.size() == 2,
              std::to_string(done));
        check("grep all: the engine's reading kept as the original",
              b.pages()[0].areas[0].paras[0].lines[0].words[1].original == "tbe" &&
              b.pages()[0].areas[0].paras[0].lines[0].words[1].edited);
        j.undo(b);
        check("grep all: undone whole", core::find_all(b, "^tb", g).size() == 3);
        check("grep all: a bad pattern finds nothing in the book", core::find_all(b, "(", g).empty());
        fs::remove_all(dir, ec);
    }
}
