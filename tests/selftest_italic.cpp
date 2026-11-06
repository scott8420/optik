// selftest_italic.cpp -- s27: italic measured from the pixels, and spelling.
//
// The measure (ocr/Slant) is checked on strokes drawn here, where the lean is
// known exactly. The rendered page (tests/data/italic.png: Times roman with
// italic words and one italic line) is checked only for what holds on any
// Tesseract: no roman word is italic, and most italic words are. Spelling is
// checked with a dictionary the test defines (exact); the real Hunspell only
// where a dictionary is installed, on words any English dictionary agrees on.
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Passes.hpp"
#include "core/Reading.hpp"
#include "core/Spell.hpp"
#include "core/Undo.hpp"
#include "ocr/Slant.hpp"

#include <zip.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

// A white sheet with "words" of upright bars, each bar leaning by `lean`
// (x moves right by lean per pixel up).
struct Sheet {
    int w, h;
    std::vector<unsigned char> px;
    Sheet(int w_, int h_) : w(w_), h(h_), px(std::size_t(w_) * std::size_t(h_), 255) {}
    ocr::Grey grey() const { return ocr::Grey{px.data(), w, h, w}; }
    // `bars` strokes 3 px wide, 40 px tall, every 12 px, from (x0, y0). The box.
    ocr::Rect word(int x0, int y0, int bars, float lean) {
        const int tall = 40;
        for (int b = 0; b < bars; ++b)
            for (int y = 0; y < tall; ++y) {
                const int shift = int(std::lround(lean * float(tall / 2 - y)));
                for (int k = 0; k < 3; ++k) {
                    const int x = x0 + b * 12 + k + shift;
                    if (x >= 0 && x < w) px[std::size_t(y0 + y) * std::size_t(w) + std::size_t(x)] = 0;
                }
            }
        const int pad = int(std::ceil(std::fabs(lean) * tall / 2)) + 1;
        return ocr::Rect{x0 - pad, y0, x0 + (bars - 1) * 12 + 3 + pad, y0 + tall};
    }
};

ocr::Word word(const std::string& t, ocr::Rect box = {}) {
    ocr::Word w; w.text = t; w.box = box; w.conf = 95; return w;
}

ocr::Para para(std::vector<std::vector<std::string>> lines) {
    ocr::Para p;
    for (auto& l : lines) {
        ocr::Line ln;
        for (auto& t : l) ln.words.push_back(word(t));
        p.lines.push_back(ln);
    }
    return p;
}

std::vector<std::string> unknowns(const std::vector<ocr::Para>& ps) {
    std::vector<std::string> out;
    for (const auto& p : ps) for (const auto& l : p.lines) for (const auto& w : l.words)
        if (w.unknown) out.push_back(w.text);
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : "|") + x;
    return s;
}

std::string odt_content(const std::string& path) {
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

bool near(float a, float b, float tol) { return !std::isnan(a) && std::fabs(a - b) <= tol; }

}  // namespace

void italic_tests() {
    std::cout << "-- italic: the slant measure (s27)\n";
    {
        Sheet s(400, 300);
        const auto up    = s.word(40, 20, 5, 0.0f);
        const auto ital  = s.word(40, 90, 5, 0.25f);
        const auto back  = s.word(40, 160, 5, -0.15f);
        const auto one   = s.word(300, 20, 1, 0.25f);
        const float a = ocr::slant_of(s.grey(), up), b = ocr::slant_of(s.grey(), ital),
                    c = ocr::slant_of(s.grey(), back);
        check("slant: upright strokes measure upright", near(a, 0.0f, 0.03f), std::to_string(a));
        check("slant: strokes leaning 0.25 measure 0.25", near(b, 0.25f, 0.03f), std::to_string(b));
        check("slant: strokes leaning back measure back", near(c, -0.15f, 0.03f), std::to_string(c));
        check("slant: one stroke is too little to tell", std::isnan(ocr::slant_of(s.grey(), one)));
        check("slant: a blank box is too little to tell", std::isnan(ocr::slant_of(s.grey(), {200, 200, 300, 260})));
        check("slant: a box off the picture is too little to tell",
              std::isnan(ocr::slant_of(s.grey(), {500, 500, 600, 560})));
    }

    std::cout << "-- italic: words on a line\n";
    {
        // roman, italic, one stroke (cannot tell), italic, roman, one stroke, roman
        Sheet s(1200, 100);
        std::vector<ocr::Rect> boxes = {
            s.word(20, 20, 5, 0.0f),  s.word(140, 20, 5, 0.22f), s.word(260, 20, 1, 0.0f),
            s.word(320, 20, 5, 0.22f), s.word(440, 20, 5, 0.0f), s.word(560, 20, 1, 0.22f),
            s.word(620, 20, 5, 0.0f)};
        std::vector<ocr::Para> ps(1);
        ps[0].lines.resize(1);
        for (std::size_t i = 0; i < boxes.size(); ++i) ps[0].lines[0].words.push_back(word("w" + std::to_string(i), boxes[i]));
        ps[0].lines[0].words[6].checked = true;
        ps[0].lines[0].words[6].italic  = true;   // the author said so: kept
        ocr::measure_italic(s.grey(), ps);
        const auto& ws = ps[0].lines[0].words;
        check("italic: leaning words are italic, upright ones are not",
              ws[1].italic && ws[3].italic && !ws[0].italic && !ws[4].italic);
        check("italic: a word too small to tell, between italic words, is italic", ws[2].italic);
        check("italic: a word too small to tell, between roman words, is not", !ws[5].italic);
        check("italic: a checked word keeps the author's italic", ws[6].italic);
    }
    {
        // A scan leaning 0.05 everywhere: nothing is italic.
        Sheet s(800, 100);
        std::vector<ocr::Para> ps(1);
        ps[0].lines.resize(1);
        for (int i = 0; i < 5; ++i) ps[0].lines[0].words.push_back(word("w", s.word(20 + i * 120, 20, 5, 0.05f)));
        ocr::measure_italic(s.grey(), ps);
        bool any = false;
        for (const auto& w : ps[0].lines[0].words) any = any || w.italic;
        check("italic: a page leaning a little all over is not italic", !any);
    }
    {
        // A page all in italic does not cancel itself out.
        Sheet s(800, 100);
        std::vector<ocr::Para> ps(1);
        ps[0].lines.resize(1);
        for (int i = 0; i < 5; ++i) ps[0].lines[0].words.push_back(word("w", s.word(20 + i * 120, 20, 5, 0.22f)));
        ocr::measure_italic(s.grey(), ps);
        bool all = true;
        for (const auto& w : ps[0].lines[0].words) all = all && w.italic;
        check("italic: a page all in italic is all italic", all);
    }

    std::cout << "-- italic: the book's setting\n";
    {
        std::vector<ocr::Area> as(2);
        as[0].kind = ocr::Kind::Text;
        as[0].paras = {para({{"a", "b", "c"}})};
        as[0].paras[0].lines[0].words[0].italic = true;
        as[0].paras[0].lines[0].words[1].italic = true;
        as[0].paras[0].lines[0].words[1].checked = true;
        as[1].kind = ocr::Kind::Header;
        as[1].paras = {para({{"HEAD"}})};
        as[1].paras[0].lines[0].words[0].italic = true;
        auto on = as;
        core::mark_italic(on, true);
        const auto& w = on[0].paras[0].lines[0].words;
        check("italic on: an italic word is doubted", w[0].italic && w[0].doubt);
        check("italic on: a checked italic word is not doubted", w[1].italic && !w[1].doubt);
        check("italic on: a running head is not doubted", !on[1].paras[0].lines[0].words[0].doubt);
        auto off = as;
        core::mark_italic(off, false);
        check("italic off: the measure is dropped, nothing doubted",
              !off[0].paras[0].lines[0].words[0].italic && !off[0].paras[0].lines[0].words[0].doubt &&
              !off[1].paras[0].lines[0].words[0].italic);
        check("italic off: the author's checked italic stays", off[0].paras[0].lines[0].words[1].italic);
        core::Settings st;
        check("italic and spelling are on in a new book", st.mark_italic && st.check_spelling);
    }

    std::cout << "-- italic: export as emphasis\n";
    {
        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_it_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "page", [](core::Edit& e) {
            core::Page p;
            p.crop = {0, 0, 1000, 1400};
            ocr::Area a;
            a.paras = {para({{"She", "read", "The", "Hidden", "Staircase", "twice", "and", "an", "exam-"},
                             {"ple", "of", "it."}})};
            auto& l0 = a.paras[0].lines[0].words;
            l0[2].italic = l0[3].italic = l0[4].italic = true;
            l0[8].italic = true;
            a.paras[0].lines[1].words[0].italic = true;
            p.areas = {a};
            p.read = true;
            e.insert_page(0, p);
        });
        const auto blocks = core::flow(b);
        const std::string want = std::string("She read ") + core::fmt_open(ocr::kItalic) + "The Hidden Staircase" + core::kFmtOff +
                                 " twice and an " + core::fmt_open(ocr::kItalic) + "example" + core::kFmtOff + " of it.";
        check("export: an italic run is one span; a word mended across lines keeps one",
              blocks.size() == 1 && blocks[0].text == want,
              blocks.empty() ? "" : core::plain(blocks[0].text));
        const std::string odt = (dir / "out.odt").string(), txt = (dir / "out.txt").string();
        const bool wrote = core::write_odt(blocks, "t", odt);
        const std::string xml = odt_content(odt);
        check("export: the ODT sets italic in Writer's Emphasis style",
              wrote && xml.find("<text:span text:style-name=\"Emphasis\">The Hidden Staircase</text:span>") != std::string::npos &&
              xml.find("<text:span text:style-name=\"Emphasis\">example</text:span>") != std::string::npos);
        check("export: the ODT carries no control bytes",
              xml.find(core::kFmtOn) == std::string::npos && xml.find(core::kFmtOff) == std::string::npos);
        core::write_text(blocks, txt);
        std::string t;
        { std::ifstream f(txt); t.assign(std::istreambuf_iterator<char>(f), {}); }
        check("export: plain text drops the marks", t == "She read The Hidden Staircase twice and an example of it.\n", t);

        // Joins across pages look past a closing span.
        check("export: a paragraph ending in italic still ends a sentence",
              core::join_lines({std::string("It was ") + core::fmt_open(ocr::kItalic) + "over." + core::kFmtOff, "then"}, true)
                  .find(core::kFmtOff) != std::string::npos);

        // Save / open keep italic and unknown.
        j.run(b, "flags", [](core::Edit& e) {
            auto& w = e.page(e.book().pages()[0].id).areas[0].paras[0].lines[0].words;
            w[0].unknown = true;
            core::add_book_word(e.meta().words, "Zorbleck");
        });
        b.save();
        core::Book back;
        const auto st = core::Book::open(dir.string(), back);
        std::string why;
        check("book.json: italic, unknown and the book's words survive save and open",
              st == core::Status::Ok && core::same_book(b, back, &why) &&
              back.pages()[0].areas[0].paras[0].lines[0].words[2].italic &&
              back.pages()[0].areas[0].paras[0].lines[0].words[0].unknown &&
              back.meta().words == std::vector<std::string>{"Zorbleck"}, why);
        fs::remove_all(dir, ec);
    }

    std::cout << "-- italic: a rendered page (italic.png)\n";
    {
        ocr::Engine eng;
        std::string why;
        if (!eng.init("eng", &why)) { check("italic page: the English language data loads", false, why); return; }
        core::ReadRequest r;
        r.page_id   = "pg_x";
        r.scan_path = std::string(OPTIK_TEST_DATA) + "/italic.png";
        r.crop      = {0, 0, 1000, 1400};
        const core::Reading rd = core::read(eng, r);
        check("italic page: reads", rd.ok, rd.why);
        const std::set<std::string> roman = {"She", "had", "read", "twice", "once", "guessed", "would",
                                             "printed", "Then", "wrote", "note", "great", "hurry."};
        const std::set<std::string> ital  = {"Hidden", "Staircase", "never", "Morning", "Herald",
                                             "Every", "word", "this", "line", "italic."};
        std::string wrong;
        int ital_seen = 0, ital_marked = 0;
        bool doubted = true;
        for (const auto& a : rd.areas) for (const auto& p : a.paras) for (const auto& l : p.lines)
            for (const auto& w : l.words) {
                if (roman.count(w.text) && w.italic) wrong += w.text + " ";
                if (ital.count(w.text)) { ++ital_seen; if (w.italic) ++ital_marked; }
                if (w.italic && !w.doubt) doubted = false;
            }
        check("italic page: no roman word is marked italic", wrong.empty(), wrong);
        check("italic page: the italic words are marked (8 of 10 at least)",
              ital_marked >= 8, std::to_string(ital_marked) + " of " + std::to_string(ital_seen) + " read");
        check("italic page: every italic word wants a look", doubted);
    }
}

void spelling_tests() {
    std::cout << "-- spelling: what of a word is checked (s27)\n";
    {
        auto is = [](const std::string& w, std::vector<std::string> want) {
            const auto got = core::spell_parts(w);
            check("spell parts: '" + w + "'", got == want, join(got));
        };
        is("\xE2\x80\x9CStop,", {"Stop"});
        is("don\xE2\x80\x99t", {"don't"});
        is("Nancy's", {"Nancy's"});
        is("well-known", {"well", "known"});
        is("Stop\xE2\x80\x94please", {"Stop", "please"});
        is("1936", {});
        is("2nd", {});
        is("II", {});
        is("XIV.", {});
        is("MCMXXXVI", {});
        is("DID", {"DID"});
        is("MIX", {});
        is("a", {});
        is("U.S.", {});
        is("--", {});
        is("'tis", {"tis"});
        is("caf\xC3\xA9", {"caf\xC3\xA9"});
    }

    std::cout << "-- spelling: the pass\n";
    {
        const std::set<std::string> dict = {"the", "cat", "sat", "on", "example", "well", "known", "a", "mat"};
        const core::KnownFn known = [&](const std::string& w) {
            std::string l = w;
            for (char& c : l) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return dict.count(l) > 0;
        };
        std::vector<ocr::Para> ps = {para({{"The", "cat", "sat", "on", "teh", "mat."}})};
        core::mark_spelling(ps, known, {});
        check("spelling: an unknown word is marked, known ones are not", unknowns(ps) == std::vector<std::string>{"teh"},
              join(unknowns(ps)));

        ps = {para({{"an", "exam-"}, {"ple", "of"}})};
        core::mark_spelling(ps, known, {"an", "of"});
        check("spelling: a word hyphenated at the line end is checked whole", unknowns(ps).empty(), join(unknowns(ps)));
        ps = {para({{"a", "well-"}, {"known", "cat"}})};
        core::mark_spelling(ps, known, {});
        check("spelling: a hyphenated compound split at the line end is fine", unknowns(ps).empty(), join(unknowns(ps)));
        ps = {para({{"a", "xyzzy-"}, {"plugh", "cat"}})};
        core::mark_spelling(ps, known, {});
        check("spelling: a bad word split at the line end marks both halves",
              unknowns(ps) == std::vector<std::string>{"xyzzy-", "plugh"}, join(unknowns(ps)));

        ps = {para({{"Zorbleck", "sat", "on", "ZORBLECK", "cat"}})};
        core::mark_spelling(ps, known, {"Zorbleck"});
        check("spelling: a book word is known, whatever its case", unknowns(ps).empty(), join(unknowns(ps)));

        ps = {para({{"teh", "teh", "1936", "II"}})};
        ps[0].lines[0].words[0].checked = true;
        ps[0].lines[0].words[1].edited  = true;
        core::mark_spelling(ps, known, {});
        check("spelling: checked and edited words, numbers and numerals are left alone", unknowns(ps).empty(),
              join(unknowns(ps)));

        std::vector<ocr::Area> as(3);
        as[0].kind = ocr::Kind::Heading; as[0].paras = {para({{"CHAPTR"}})};
        as[1].kind = ocr::Kind::Header;  as[1].paras = {para({{"Zzq"}})};
        as[2].kind = ocr::Kind::Text;    as[2].paras = {para({{"qqq"}})};
        core::mark_spelling(as, known, {});
        check("spelling: headings and text are checked, running heads are not",
              as[0].paras[0].lines[0].words[0].unknown && !as[1].paras[0].lines[0].words[0].unknown &&
              as[2].paras[0].lines[0].words[0].unknown);
    }

    std::cout << "-- spelling: the book's words\n";
    {
        std::vector<std::string> bw;
        check("book words: added", core::add_book_word(bw, "Zorbleck"));
        check("book words: added again (any case) is not added twice", !core::add_book_word(bw, "zorbleck"));
        core::add_book_word(bw, "Bess");
        core::add_book_word(bw, "Gwen");
        check("book words: kept sorted", bw == std::vector<std::string>{"Bess", "Gwen", "Zorbleck"}, join(bw));

        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_sp_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "pages", [](core::Edit& e) {
            for (int i = 0; i < 2; ++i) {
                core::Page p;
                p.crop = {0, 0, 1000, 1400};
                ocr::Area a;
                a.paras = {para({{"Gwen", "met", "Gwen-like", "Zorbleck"}})};
                for (auto& w : a.paras[0].lines[0].words) w.unknown = w.text != "met";
                p.areas = {a};
                e.insert_page(std::size_t(i), p);
            }
        });
        const auto& w0 = b.pages()[0].areas[0].paras[0].lines[0].words;
        check("book: an unknown word wants a look, however confident", b.uncertain(w0[0]) && !b.uncertain(w0[1]));
        const core::Book before = b;
        j.run(b, "add Gwen", [](core::Edit& e) {
            core::add_book_word(e.meta().words, "Gwen");
            for (const auto& pg : e.book().pages()) {
                auto areas = pg.areas;
                if (core::clear_book_words(areas, e.meta().words) > 0) e.page(pg.id).areas = areas;
            }
        });
        int left = 0;
        for (const auto& p : b.pages()) for (const auto& w : p.areas[0].paras[0].lines[0].words) left += w.unknown;
        check("book: adding a word clears it on every page, not words with other unknown parts",
              left == 4 && !b.pages()[1].areas[0].paras[0].lines[0].words[0].unknown &&
              b.pages()[1].areas[0].paras[0].lines[0].words[2].unknown, std::to_string(left));
        check("book: the audit is clean with book words", core::audit(b).size() ==
              core::audit(before).size());
        j.undo(b);
        std::string why;
        check("book: one undo takes the word and its clearing back", core::same_book(b, before, &why), why);

        core::Book bad = b;
        core::Journal jb;
        jb.run(bad, "bad words", [](core::Edit& e) { e.meta().words = {"b", "a", "A", ""}; });
        const auto said = core::audit(bad);
        bool sorted = false, twice = false, empty = false;
        for (const auto& s : said) {
            sorted = sorted || s.find("not sorted") != std::string::npos;
            twice  = twice  || s.find("twice") != std::string::npos;
            empty  = empty  || s.find("empty book word") != std::string::npos;
        }
        check("audit: book words out of order, twice, or empty are named", sorted && twice && empty);
        fs::remove_all(dir, ec);
    }

    std::cout << "-- spelling: the installed dictionary\n";
    {
        const std::string dic = core::dictionary_for("eng");
        if (dic.empty()) {
            std::cout << "   (no English Hunspell dictionary here: install hunspell-en-US; the real-dictionary checks are skipped)\n";
            return;
        }
        core::Speller sp;
        check("dictionary: opens " + dic, sp.open("eng"));
        check("dictionary: knows house, the", sp.known("house") && sp.known("the"));
        check("dictionary: does not know hosue or teh", !sp.known("hosue") && !sp.known("teh"));
        check("dictionary: none for a language without one", core::dictionary_for("xxx").empty());

        ocr::Engine eng;
        std::string why;
        if (!eng.init("eng", &why)) return;
        core::ReadRequest r;
        r.page_id   = "pg_x";
        r.scan_path = std::string(OPTIK_TEST_DATA) + "/italic.png";
        r.crop      = {0, 0, 1000, 1400};
        const core::Reading rd = core::read(eng, r);
        const std::set<std::string> plain = {"She", "had", "read", "twice", "once", "would", "printed",
                                             "wrote", "note", "great", "hurry.", "the", "in"};
        std::string wrong;
        int unknown = 0;
        for (const auto& a : rd.areas) for (const auto& p : a.paras) for (const auto& l : p.lines)
            for (const auto& w : l.words) {
                if (plain.count(w.text) && w.unknown) wrong += w.text + " ";
                unknown += w.unknown;
            }
        check("spelling page: plain words are not marked", wrong.empty(), wrong);
        check("spelling page: the made-up name and the misspelling are", unknown >= 1, std::to_string(unknown));
    }
}
