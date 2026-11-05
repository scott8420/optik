// selftest_passes.cpp -- the reading passes after the engine (s25): headings,
// hot-lead quotes, small caps; the doubt flag; headings in export.
//
// The rules are checked on built areas (exact). The rendered chapter opener
// (tests/data/chapter.png) is checked only for what holds on any Tesseract:
// the centred title is a heading, and nothing the passes are for is left
// silently wrong -- a heading word is in capitals or wants a look, the opener
// is THE or wants a look, no quote is left as two marks.
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Export.hpp"
#include "core/Passes.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

ocr::Word w_at(const std::string& t, int x0, int x1, int y0, int y1, float conf = 95) {
    ocr::Word w; w.text = t; w.box = {x0, y0, x1, y1}; w.conf = conf; return w;
}

// A line of words laid out from x0, each word 20 px a letter, 10 px apart.
ocr::Line line_at(std::initializer_list<const char*> words, int x0, int y0, int h = 30) {
    ocr::Line l;
    int x = x0;
    for (const char* t : words) {
        const int w = int(std::string(t).size()) * 20;
        l.words.push_back(w_at(t, x, x + w, y0, y0 + h));
        x += w + 10;
    }
    l.box = {x0, y0, l.words.empty() ? x0 : l.words.back().box.x1, y0 + h};
    return l;
}

// A line centred on a 100..900 column (centre 500).
ocr::Line centred(std::initializer_list<const char*> words, int y0) {
    int w = -10;
    for (const char* t : words) w += int(std::string(t).size()) * 20 + 10;
    return line_at(words, 500 - w / 2, y0);
}

ocr::Area area_of(std::vector<ocr::Line> lines, ocr::Kind k = ocr::Kind::Text) {
    ocr::Area a; a.kind = k;
    ocr::Para p;
    for (auto& l : lines) { p.box = ocr::unite(p.box, l.box); p.lines.push_back(l); }
    a.box = p.box;
    a.paras.push_back(p);
    return a;
}

// A body of n full-width lines (100..900) at a 46 px pitch from y0.
std::vector<ocr::Line> body(int y0, int n) {
    std::vector<ocr::Line> ls;
    for (int i = 0; i < n; ++i) {
        ocr::Line l = line_at({"the", "body", "line"}, 100, y0 + i * 46);
        l.box.x1 = 900;
        l.words.back().box.x1 = 900;
        ls.push_back(l);
    }
    return ls;
}

int count_kind(const std::vector<ocr::Area>& as, ocr::Kind k) {
    int n = 0;
    for (const auto& a : as) n += a.kind == k;
    return n;
}

std::string area_text(const ocr::Area& a) {
    std::string s;
    for (const auto& p : a.paras) { if (!s.empty()) s += '\n'; s += ocr::para_text(p); }
    return s;
}

int word_count(const std::vector<ocr::Area>& as) {
    int n = 0;
    for (const auto& a : as) for (const auto& p : a.paras) for (const auto& l : p.lines) n += int(l.words.size());
    return n;
}

bool all_caps_ascii(const std::string& s) {
    for (unsigned char c : s) if (c >= 'a' && c <= 'z') return false;
    return true;
}

std::string q(const std::string& in, bool* doubt = nullptr) { return core::mend_quote_word(in, doubt); }

}  // namespace

void passes_tests() {
    std::cout << "-- passes: headings\n";
    {
        // CHAPTER II / AN IVORY CHARM, each its own block, space, then the body.
        std::vector<ocr::Area> as{area_of({centred({"CHAPTER", "II"}, 260)}),
                                  area_of({centred({"AN", "IVORY", "CHARM"}, 320)}),
                                  area_of(body(470, 5))};
        core::mark_headings(as);
        check("headings: a short centred line with space below is a heading",
              as.size() == 3 && as[1].kind == ocr::Kind::Heading);
        check("headings: ...and so is the centred line above it in the run",
              as[0].kind == ocr::Kind::Heading);
        check("headings: the body stays text", as[2].kind == ocr::Kind::Text);

        // The engine lumped the title in with the body: split off.
        std::vector<ocr::Line> ls{centred({"CHAPTER", "II"}, 260)};
        for (auto& l : body(400, 4)) ls.push_back(l);
        std::vector<ocr::Area> lump{area_of(ls)};
        const int before = word_count(lump);
        core::mark_headings(lump);
        check("headings: a heading inside a text block is split off into its own area",
              lump.size() == 2 && lump[0].kind == ocr::Kind::Heading &&
              area_text(lump[0]) == "CHAPTER II" && lump[1].kind == ocr::Kind::Text &&
              lump[0].box.y1 <= lump[1].box.y0,
              lump.empty() ? "" : area_text(lump[0]));
        check("headings: the split keeps every word", word_count(lump) == before);

        // Centred but tight on the body below: not a heading.
        std::vector<ocr::Area> tight{area_of({centred({"Short", "line"}, 400)}), area_of(body(446, 5))};
        core::mark_headings(tight);
        check("headings: a centred line with no space below stays text",
              count_kind(tight, ocr::Kind::Heading) == 0);

        // A scene break: centred, spaced, no letters.
        std::vector<ocr::Area> brk{area_of(body(100, 4)), area_of({centred({"*", "*", "*"}, 330)}),
                                   area_of(body(470, 4))};
        core::mark_headings(brk);
        check("headings: a scene break (* * *) is not a heading", count_kind(brk, ocr::Kind::Heading) == 0);

        // A paragraph's short last line, left-aligned, then a blank line.
        std::vector<ocr::Line> pl = body(100, 3);
        pl.push_back(line_at({"the", "end."}, 100, 238));
        std::vector<ocr::Area> last{area_of(pl), area_of(body(380, 4))};
        core::mark_headings(last);
        check("headings: a paragraph's short last line is not a heading",
              count_kind(last, ocr::Kind::Heading) == 0);

        // A centred poem: five centred lines in a row.
        std::vector<ocr::Line> poem;
        for (int i = 0; i < 5; ++i) poem.push_back(centred({"a", "line", "of", "verse"}, 100 + i * 46));
        std::vector<ocr::Area> verse{area_of(poem), area_of(body(500, 4))};
        core::mark_headings(verse);
        check("headings: five centred lines in a row are verse, not a heading",
              count_kind(verse, ocr::Kind::Heading) == 0);

        // The author's area is never re-marked.
        std::vector<ocr::Area> mine{area_of({centred({"CHAPTER", "II"}, 260)}), area_of(body(470, 5))};
        mine[0].user = true;
        core::mark_headings(mine);
        check("headings: an area the author set is never re-marked", mine[0].kind == ocr::Kind::Text);
    }

    std::cout << "-- passes: hot-lead quotes\n";
    {
        bool d = true;
        check("quotes: ``Stop opens with one double quote", q("``Stop", &d) == "“Stop" && !d, q("``Stop"));
        check("quotes: ''Stop and ‘‘Stop open with one double quote",
              q("''Stop") == "“Stop" && q("‘‘Stop") == "“Stop");
        check("quotes: a double read twice (““Stop) is one", q("““Stop") == "“Stop");
        check("quotes: car!'' closes with one double quote after the mark",
              q("car!''") == "car!”" && q("car''.") == "car”.", q("car!''"));
        check("quotes: straight \"Hello,\" becomes typographic",
              q("\"Hello,\"") == "“Hello,”", q("\"Hello,\""));
        check("quotes: a single quote stays single", q("'Stop") == "‘Stop" && q("stop'") == "stop’");
        check("quotes: an apostrophe between letters", q("don't") == "don’t" && q("Nancy`s") == "Nancy’s");
        check("quotes: an elision opens with an apostrophe", q("'tis") == "’tis" && q("'em,") == "’em,");
        check("quotes: nested quotes are kept", q("\"'Stop") == "“‘Stop" && q("car!'\"") == "car!’”");
        check("quotes: other punctuation keeps its place", q("(``Stop") == "(“Stop", q("(``Stop"));
        q("car'!””", &d);
        check("quotes: marks split by other punctuation are doubted", d);
        q("```Stop", &d);
        check("quotes: a run too heavy for one quote is doubted", d);
        check("quotes: a word with no marks is unchanged", q("Nancy.") == "Nancy." && q("--") == "--");

        // Lone marks join their word; the word keeps its doubt and confidence.
        ocr::Line l;
        l.words = {w_at("``", 100, 120, 0, 30, 40), w_at("Stop", 125, 200, 0, 30, 96),
                   w_at("the", 210, 260, 0, 30), w_at("car!", 270, 340, 0, 30), w_at("''", 342, 360, 0, 30, 50)};
        std::vector<ocr::Area> as{area_of({l})};
        core::mend_quotes(as);
        const auto& ws = as[0].paras[0].lines[0].words;
        check("quotes: a lone opening mark joins the word after it",
              ws.size() == 3 && ws[0].text == "“Stop" && ws[0].box.x0 == 100 && ws[0].box.x1 == 200,
              ws.empty() ? "" : ws[0].text);
        check("quotes: a lone closing mark joins the word before it", ws.size() == 3 && ws[2].text == "car!”");
        check("quotes: a joined word keeps the lower confidence (it still wants a look)",
              ws.size() == 3 && ws[0].conf == 40 && ws[2].conf == 50);

        ocr::Line l2;
        l2.words = {w_at("``Stop", 0, 10, 0, 10, 30)};
        l2.words[0].checked = true;
        std::vector<ocr::Area> as2{area_of({l2})};
        core::mend_quotes(as2);
        check("quotes: a word the author checked is left as it is",
              as2[0].paras[0].lines[0].words[0].text == "``Stop");
    }

    std::cout << "-- passes: small caps\n";
    {
        std::vector<ocr::Area> as{area_of({centred({"CHAPTER", "II"}, 260)}, ocr::Kind::Heading),
                                  area_of({centred({"Aw", "Ivory", "CHarM"}, 320)}, ocr::Kind::Heading),
                                  area_of({centred({"The", "Secret", "Room"}, 380)}, ocr::Kind::Heading),
                                  area_of(body(600, 3))};
        as[3].paras[0].lines[0] = line_at({"Tue", "old", "car"}, 100, 600);
        core::mark_small_caps(as);
        const auto& h1 = as[1].paras[0].lines[0].words;
        check("caps: a heading in capitals is upper-cased", area_text(as[1]) == "AW IVORY CHARM", area_text(as[1]));
        check("caps: ...and every word it changed wants a look",
              h1[0].doubt && h1[1].doubt && h1[2].doubt);
        check("caps: a heading read in capitals is left alone",
              area_text(as[0]) == "CHAPTER II" && !as[0].paras[0].lines[0].words[0].doubt);
        check("caps: a title-case heading is not a small-caps one",
              area_text(as[2]) == "The Secret Room" && !as[2].paras[0].lines[0].words[0].doubt);

        // The opener sits under the LAST heading of the run; here The Secret Room.
        const auto& op = as[3].paras[0].lines[0].words;
        check("caps: a chapter's opener wants a look however confident (Tue at 95%)",
              op[0].text == "Tue" && op[0].doubt);
        check("caps: ...but not the ordinary words after it", !op[1].doubt && !op[2].doubt);

        std::vector<ocr::Area> rough{area_of({centred({"AN", "Ivory", "Crrsrst"}, 320)}, ocr::Kind::Heading),
                                     area_of({centred({"Chapter", "IV"}, 380)}, ocr::Kind::Heading)};
        core::mark_small_caps(rough);
        check("caps: one word of capitals makes a heading caps (AN Ivory Crrsrst)",
              area_text(rough[0]) == "AN IVORY CRRSRST" && rough[0].paras[0].lines[0].words[2].doubt,
              area_text(rough[0]));
        check("caps: ...but a roman numeral does not (Chapter IV)", area_text(rough[1]) == "Chapter IV");

        std::vector<ocr::Area> b{area_of({centred({"CHAPTER", "I"}, 260)}, ocr::Kind::Heading),
                                 area_of({line_at({"THE", "OLD", "car", "came"}, 100, 400)})};
        core::mark_small_caps(b);
        const auto& o2 = b[1].paras[0].lines[0].words;
        check("caps: an opener run in capitals all wants a look, to the first small letter",
              o2[0].doubt && o2[1].doubt && !o2[2].doubt);

        std::vector<ocr::Area> c{area_of(body(100, 3)), area_of(body(300, 3))};
        core::mark_small_caps(c);
        bool none = true;
        for (const auto& a : c) for (const auto& p : a.paras) for (const auto& l : p.lines)
            for (const auto& w : l.words) none = none && !w.doubt;
        check("caps: no heading, no opener: nothing doubted", none);
    }

    std::cout << "-- passes: doubt in the book\n";
    {
        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_doubt_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "page", [](core::Edit& e) {
            core::Page p;
            p.crop = {0, 0, 1000, 1400};
            ocr::Line l = line_at({"Tue", "old"}, 100, 400);
            l.words[0].conf = 99;
            l.words[0].doubt = true;
            p.areas.push_back(area_of({l}));
            p.read = true;
            e.insert_page(0, p);
        });
        const auto& w0 = b.pages()[0].areas[0].paras[0].lines[0].words[0];
        check("doubt: a doubted word wants a look at 99% confidence", b.uncertain(w0) && b.uncertain_count(b.pages()[0]) == 1);
        core::Book back;
        const bool saved = b.save() == core::Status::Ok && core::Book::open(dir.string(), back) == core::Status::Ok;
        check("doubt: survives book.json", saved && back.pages()[0].areas[0].paras[0].lines[0].words[0].doubt);
        j.run(b, "check", [](core::Edit& e) {
            core::Page& p = e.page(e.book().pages()[0].id);
            p.areas[0].paras[0].lines[0].words[0].checked = true;
        });
        check("doubt: checking the word settles it", b.uncertain_count(b.pages()[0]) == 0);
        core::Settings s;
        check("doubt: the three passes are on in a new book", s.mark_headings && s.mend_quotes && s.small_caps);
        fs::remove_all(dir, ec);
    }

    std::cout << "-- passes: headings in export\n";
    {
        const fs::path dir = fs::temp_directory_path() / ("optik_selftest_hx_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        core::Book b;
        core::Book::create(dir.string(), "", b);
        core::Journal j;
        j.run(b, "page", [](core::Edit& e) {
            core::Page p;
            p.crop = {0, 0, 1000, 1400};
            p.areas = {area_of({centred({"CHAPTER", "II"}, 260)}, ocr::Kind::Heading),
                       area_of({centred({"AN", "IVORY", "CHARM"}, 320)}, ocr::Kind::Heading),
                       area_of({line_at({"THE", "old", "car."}, 100, 470)})};
            p.read = true;
            e.insert_page(0, p);
            core::Page p2 = p;
            p2.areas = {area_of({centred({"CHAPTER", "III"}, 260), centred({"THE", "LETTER"}, 320)},
                                ocr::Kind::Heading)};
            e.insert_page(1, p2);
        });
        const auto blocks = core::flow(b);
        check("export: a heading read as two blocks is one heading, its lines kept",
              blocks.size() == 3 && blocks[0].kind == core::Block::Heading &&
              blocks[0].text == "CHAPTER II\nAN IVORY CHARM",
              blocks.empty() ? "" : blocks[0].text);
        check("export: a two-line heading keeps its line break",
              blocks.size() == 3 && blocks[2].text == "CHAPTER III\nTHE LETTER");
        const std::string odt = (dir / "out.odt").string();
        check("export: the ODT with a broken heading writes", core::write_odt(blocks, "t", odt) && fs::file_size(odt) > 0);
        fs::remove_all(dir, ec);
    }

    std::cout << "-- passes: a rendered chapter opener\n";
    {
        ocr::Engine eng;
        std::string why;
        if (!eng.init("eng", &why)) {
            check("opener: the English language data loads", false, why);
            return;
        }
        core::ReadRequest r;
        r.page_id   = "pg_x";
        r.scan_path = std::string(OPTIK_TEST_DATA) + "/chapter.png";
        r.crop      = {0, 0, 1000, 1400};
        const core::Reading rd = core::read(eng, r);
        check("opener: the chapter page reads", rd.ok, rd.why);
        if (!rd.ok) return;

        std::string heads, text;
        bool caps_or_doubt = true, opener_ok = false, opener_seen = false, two_marks = false;
        int heading_bottom = -1, text_top = 1 << 30;
        for (const auto& a : rd.areas) {
            if (a.kind == ocr::Kind::Heading) {
                heads += area_text(a) + "\n";
                heading_bottom = std::max(heading_bottom, a.box.y1);
                for (const auto& p : a.paras) for (const auto& l : p.lines) for (const auto& w : l.words)
                    if (!all_caps_ascii(w.text) && !w.doubt) caps_or_doubt = false;
            } else if (a.kind == ocr::Kind::Text) {
                text += area_text(a) + "\n";
                text_top = std::min(text_top, a.box.y0);
                for (const auto& p : a.paras) for (const auto& l : p.lines) {
                    if (!opener_seen && !l.words.empty() && l.box.y0 >= 440 && l.box.y0 < 520) {
                        opener_seen = true;
                        opener_ok = l.words[0].text == "THE" || l.words[0].doubt;
                    }
                    for (const auto& w : l.words) {
                        const std::string& t = w.text;
                        for (const char* m : {"``", "''", "‘‘", "’’", "““", "””"})
                            if (t.find(m) != std::string::npos) two_marks = true;
                    }
                }
            }
        }
        check("opener: CHAPTER is a heading", heads.find("CHAPTER") != std::string::npos, heads);
        check("opener: the headings sit above the text", heading_bottom >= 0 && heading_bottom <= text_top);
        check("opener: every heading word is in capitals or wants a look", caps_or_doubt, heads);
        check("opener: the opening word reads THE or wants a look", opener_seen && opener_ok);
        check("opener: no quote is left as two marks", !two_marks, text);
        check("opener: the body is not a heading", text.find("strange charm") != std::string::npos);
    }
}
