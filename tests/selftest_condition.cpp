// selftest_condition.cpp -- phase 0, condition (s29, milestone 17): deskew,
// decolour, level. The skew measured on drawn tilts (both ways) and undone by
// its own angle; a straight page and a blank one measure nothing; level takes
// grey paper to white and leaves black and white alone; decolour makes grey.
// Through the book: a deskewed read keeps its angle on the page (book.json,
// undo, audit), reads flatter lines than the tilted read, and an area read
// asked before a re-straightening is dropped. Rules, not one machine's numbers.
#include "Check.hpp"

#include "core/Audit.hpp"
#include "core/Book.hpp"
#include "core/PageOps.hpp"
#include "core/Picture.hpp"
#include "core/Reading.hpp"
#include "core/Undo.hpp"
#include "ocr/Condition.hpp"
#include "ocr/Ocr.hpp"

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

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
    fs::create_directories(p, ec);
    return p;
}

std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }

// The page tilted by `deg` (Leptonica's sense), written as a PNG.
bool tilted(const std::string& from, double deg, const fs::path& to) {
    Pix* p = pixRead(from.c_str());
    if (!p) return false;
    Pix* r = pixRotate(p, float(deg * M_PI / 180.0), L_ROTATE_AREA_MAP, L_BRING_IN_WHITE, 0, 0);
    pixDestroy(&p);
    if (!r) return false;
    const bool ok = pixWrite(to.c_str(), r, IFF_PNG) == 0;
    pixDestroy(&r);
    return ok;
}

double skew_of(const std::string& path) {
    Pix* p = pixRead(path.c_str());
    if (!p) return 99;
    const double s = ocr::find_skew(p);
    pixDestroy(&p);
    return s;
}

// The page straightened by its own measure, measured again.
double after_straightening(const std::string& path) {
    Pix* p = pixRead(path.c_str());
    if (!p) return 99;
    Pix* s = ocr::straighten(p, ocr::find_skew(p));
    const double again = ocr::find_skew(s);
    pixDestroy(&s);
    pixDestroy(&p);
    return again;
}

// The middle grey of a page (8 bpp view of it).
int median_grey(Pix* p) {
    Pix* g = pixConvertTo8(p, 0);
    l_uint32 v = 0;
    if (g) pixGetRankValue(g, 1, 0.5f, &v);
    pixDestroy(&g);
    return std::min(int(v), 255);   // Leptonica's rank says 256 for an all-white page
}

double mean_line_height(const std::vector<ocr::Area>& areas) {
    double sum = 0;
    int n = 0;
    for (const auto& a : areas)
        for (const auto& p : a.paras)
            for (const auto& l : p.lines)
                if (l.words.size() >= 3) { sum += l.box.height(); ++n; }
    return n ? sum / n : 0;
}

void skew_measure_tests(const fs::path& dir) {
    const std::string straight = data("page_left.png");
    check("skew: a straight page measures none", skew_of(straight) == 0.0, num(skew_of(straight)));

    for (double deg : {2.0, -1.5, 3.5}) {
        const fs::path f = dir / ("tilt_" + num(deg) + ".png");
        if (!tilted(straight, deg, f)) { check("skew: tilt fixture " + num(deg), false); continue; }
        const double s = skew_of(f.string());
        // Whichever sign Leptonica measures in, it is the tilt's size, and the
        // tilt's own angle -- straighten() by it levels the page.
        check("skew: a page tilted " + num(deg) + " degrees measures it",
              std::fabs(std::fabs(s) - std::fabs(deg)) < 0.25, num(s));
        const double again = after_straightening(f.string());
        check("skew: straightened by its measure, a " + num(deg) + " tilt measures level",
              std::fabs(again) < 0.25, num(again));
    }
    // tests/data/tilted.png: turned by another library (PIL, scripts/make_fixtures.py).
    const double t = skew_of(data("tilted.png"));
    check("skew: tilted.png (2 degrees, turned elsewhere) measures 2", std::fabs(std::fabs(t) - 2.0) < 0.25, num(t));
    // Opposite tilts measure opposite.
    const double a = skew_of((dir / "tilt_2.00.png").string());
    const double b = skew_of((dir / "tilt_-1.50.png").string());
    check("skew: opposite tilts measure opposite", a * b < 0, num(a) + " " + num(b));

    Pix* blank = pixCreate(800, 1000, 8);
    pixSetAll(blank);
    check("skew: a blank page measures none", ocr::find_skew(blank) == 0.0);
    Pix* same = ocr::straighten(blank, 0.05);
    check("skew: under a tenth of a degree is not turned (same size, a clone)",
          same && pixGetWidth(same) == 800 && pixGetHeight(same) == 1000);
    pixDestroy(&same);
    Pix* turned = ocr::straighten(blank, 2.0);
    check("skew: straightening keeps the page's size",
          turned && pixGetWidth(turned) == 800 && pixGetHeight(turned) == 1000);
    check("skew: white is brought in at the corners", turned && median_grey(turned) == 255);
    pixDestroy(&turned);
    pixDestroy(&blank);
}

void tone_tests() {
    // Grey paper (190) with black-ish ink (70): a pale, yellowed scan in grey.
    Pix* page = pixRead(data("page_left.png").c_str());
    Pix* g8 = page ? pixConvertTo8(page, 0) : nullptr;
    Pix* dull = g8 ? pixGammaTRC(nullptr, g8, 1.0f, -100, 330) : nullptr;   // squeeze 0..255 into ~70..190
    const int was = dull ? median_grey(dull) : 0;
    ocr::Look look;
    look.level = true;
    Pix* lev = dull ? ocr::tone(dull, look) : nullptr;
    const int now = lev ? median_grey(lev) : 0;
    check("level: grey paper comes up to white", was < 220 && now >= 245,
          std::to_string(was) + " -> " + std::to_string(now));
    l_uint32 dark_was = 255, dark_now = 255;
    if (dull) pixGetRankValue(dull, 1, 0.005f, &dark_was);
    if (lev)  pixGetRankValue(lev, 1, 0.005f, &dark_now);
    check("level: the ink goes darker", dark_now < dark_was,
          std::to_string(dark_was) + " -> " + std::to_string(dark_now));
    check("level: the page keeps its size",
          lev && dull && pixGetWidth(lev) == pixGetWidth(dull) && pixGetHeight(lev) == pixGetHeight(dull));
    pixDestroy(&lev);

    look.level = false;
    Pix* none = dull ? ocr::tone(dull, look) : nullptr;
    check("tone: nothing asked is the page as it was", none && median_grey(none) == was);
    pixDestroy(&none);

    // Black and white is already levelled: left alone, not failed.
    Pix* bw = g8 ? pixConvertTo1(g8, 128) : nullptr;
    look.level = true;
    Pix* bw2 = bw ? ocr::tone(bw, look) : nullptr;
    check("level: a 1-bit page is left as it is", bw2 && pixGetDepth(bw2) == 1);
    pixDestroy(&bw2);
    pixDestroy(&bw);

    // Decolour: a colour page in grey; off, it keeps its colour.
    Pix* colour = g8 ? pixConvertTo32(g8) : nullptr;
    ocr::Look dc;
    dc.decolour = true;
    Pix* grey = colour ? ocr::tone(colour, dc) : nullptr;
    check("decolour: a colour page comes out grey", grey && pixGetDepth(grey) == 8);
    pixDestroy(&grey);
    dc.decolour = false;
    Pix* kept = colour ? ocr::tone(colour, dc) : nullptr;
    check("decolour: off, the colour is kept", kept && pixGetDepth(kept) == 32);
    pixDestroy(&kept);
    dc.level = true;
    Pix* lc = colour ? ocr::tone(colour, dc) : nullptr;
    check("level: a colour page levels and stays colour", lc && pixGetDepth(lc) == 32);
    pixDestroy(&lc);

    Pix* tiny = pixCreate(20, 20, 8);
    look.level = true;
    Pix* t2 = ocr::tone(tiny, look);
    check("level: a page too small to level is left alone", t2 && pixGetWidth(t2) == 20);
    pixDestroy(&t2);
    pixDestroy(&tiny);
    pixDestroy(&colour);
    pixDestroy(&dull);
    pixDestroy(&g8);
    pixDestroy(&page);
}

// A one-page book from the tilted page.
bool one_page(const fs::path& dir, const fs::path& scan, core::Book& b, core::Journal& j) {
    if (core::Book::create((dir / "Tilt.optik").string(), "Tilt", b) != core::Status::Ok) return false;
    const core::ImportPlan plan = core::plan_import({scan.string()}, true);
    std::vector<core::Scan> scans = core::copy_scans(b.folder(), plan.paths);
    return j.run(b, "Import", [&](core::Edit& e) {
        std::vector<core::Scan> with_ids;
        for (auto& s : scans) with_ids.push_back(e.add_scan(s));
        std::size_t at = 0;
        for (auto& p : core::pages_for(plan, with_ids)) e.insert_page(at++, p);
    }) && b.pages().size() == 1;
}

void book_tests(const fs::path& dir) {
    const fs::path scan = dir / "tilt_2.00.png";
    core::Book b;
    core::Journal j;
    if (!one_page(dir, scan, b, j)) { check("condition: a one-page book from the tilted scan", false); return; }
    const std::string P = b.pages()[0].id;

    const core::Settings fresh;
    check("settings: deskew and level on, decolour off, for a new book",
          fresh.deskew && fresh.level && !fresh.decolour);

    ocr::Engine eng;
    std::string why;
    if (!eng.init("eng", &why)) { check("condition: the engine", false, why); return; }

    // Deskew off: read as cut.
    j.run(b, "Deskew off", [&](core::Edit& e) { e.meta().settings.deskew = false; });
    const core::Reading flat = core::read(eng, core::request_for(b, *b.page(P)));
    check("read: deskew off reads the page as cut (skew 0)", flat.ok && flat.skew == 0.0, flat.why);

    j.run(b, "Deskew on", [&](core::Edit& e) { e.meta().settings.deskew = true; });
    const core::Reading straight = core::read(eng, core::request_for(b, *b.page(P)));
    check("read: deskew on measures the tilt", straight.ok && std::fabs(std::fabs(straight.skew) - 2.0) < 0.25,
          num(straight.skew));
    const double h_tilt = mean_line_height(flat.areas), h_straight = mean_line_height(straight.areas);
    check("read: straightened lines are flatter than tilted ones",
          h_straight > 0 && h_tilt > 0 && h_straight < h_tilt * 0.8,
          num(h_tilt) + " -> " + num(h_straight));

    bool landed = false;
    check("land: the page keeps its angle",
          j.run(b, "Read", [&](core::Edit& e) { landed = core::land(e, straight); }) && landed &&
          b.page(P)->skew == straight.skew && b.page(P)->read, num(b.page(P)->skew));
    check("audit: a straightened page is sound", core::audit(b).empty(),
          core::audit(b).empty() ? "" : core::audit(b)[0]);

    // An area read in the straightened page...
    const ocr::Rect box = b.page(P)->areas.empty() ? ocr::Rect{} : b.page(P)->areas[0].box;
    const core::Reading area = core::read(eng, core::request_for_area(b, *b.page(P), 0, box));
    check("read area: read in the straightened page", area.ok && area.skew == b.page(P)->skew &&
          !area.paras.empty(), area.why);
    // ...dropped if the page was read straight again before it landed.
    core::Reading moved = area;
    moved.skew = 0.0;
    landed = true;
    j.run(b, "Land", [&](core::Edit& e) { landed = core::land(e, moved); });
    check("land: an area read in another straightening is dropped", !landed);
    landed = false;
    j.run(b, "Land", [&](core::Edit& e) { landed = core::land(e, area); });
    check("land: in the same straightening it lands", landed);

    // book.json keeps it; settings round-trip; the open agrees.
    j.run(b, "Tone", [&](core::Edit& e) { e.meta().settings.decolour = true; e.meta().settings.level = false; });
    check("save: a straightened, toned book saves", b.save() == core::Status::Ok);
    core::Book again;
    std::string diff;
    check("open: the skew and the condition settings come back",
          core::Book::open(b.folder(), again) == core::Status::Ok && core::same_book(b, again, &diff) &&
          again.page(P)->skew == b.page(P)->skew && again.meta().settings.decolour &&
          !again.meta().settings.level && again.meta().settings.deskew, diff);

    // An older book.json without the keys gets the defaults.
    {
        std::ifstream in(fs::path(b.folder()) / "book.json");
        std::string s((std::istreambuf_iterator<char>(in)), {});
        // book.json is one key a line: drop the four lines, then any comma
        // left before a closing bracket.
        std::vector<std::string> lines;
        for (std::size_t at = 0; at < s.size();) {
            const std::size_t nl = s.find('\n', at);
            const std::size_t end = nl == std::string::npos ? s.size() : nl;
            const std::string line = s.substr(at, end - at);
            at = end + 1;
            bool drop = false;
            for (const char* k : {"\"deskew\":", "\"decolour\":", "\"level\":", "\"skew\":"})
                drop = drop || line.find(k) != std::string::npos;
            if (drop) continue;
            const std::size_t first = line.find_first_not_of(' ');
            if (first != std::string::npos && (line[first] == '}' || line[first] == ']') &&
                !lines.empty() && !lines.back().empty() && lines.back().back() == ',')
                lines.back().pop_back();
            lines.push_back(line);
        }
        s.clear();
        for (const auto& l : lines) s += l + "\n";
        std::ofstream(fs::path(b.folder()) / "book.json", std::ios::trunc) << s;
        core::Book old;
        check("open: a book from before s29 is upright and conditioned by default",
              core::Book::open(b.folder(), old) == core::Status::Ok && old.page(P) &&
              old.page(P)->skew == 0.0 && old.meta().settings.deskew && old.meta().settings.level &&
              !old.meta().settings.decolour);
    }

    // The look: skew and tone change the key; a turn clears the angle.
    const std::string k1 = core::look_key(b, *b.page(P));
    j.run(b, "Level", [&](core::Edit& e) { e.meta().settings.level = true; });
    const std::string k2 = core::look_key(b, *b.page(P));
    check("look: the tone is in the picture's key", k1 != k2);
    const ocr::Look l = core::look_of(b, *b.page(P));
    check("look: the page's skew and the book's tone", l.skew == b.page(P)->skew && l.level && l.decolour);

    core::Picture pic;
    check("picture: a straightened page keeps its size",
          core::load_picture(b.scan_path(b.page(P)->scan_id), l, 0, pic) && pic.src_w == b.page(P)->crop.width() &&
          pic.width == b.page(P)->crop.width() && pic.height == b.page(P)->crop.height());

    j.run(b, "Turn", [&](core::Edit& e) { core::rotate_page(e, P, 1); });
    check("turn: a turned page's angle goes with its reading",
          b.page(P)->skew == 0.0 && !b.page(P)->read && core::audit(b).empty());
    j.undo(b);
    check("undo: the turn undone, the angle is back", b.page(P)->skew == straight.skew);
    while (j.position() > 1) j.undo(b);   // back to just after the import
    check("undo: to the import, upright and unread", b.page(P) && b.page(P)->skew == 0.0 && !b.page(P)->read);
}

void audit_tests() {
    core::Book b;
    core::Journal j;
    const fs::path dir = temp_dir("cond_audit");
    if (!one_page(dir, data("page_left.png"), b, j)) { check("audit fixture", false); return; }
    const std::string P = b.pages()[0].id;
    j.run(b, "Bad", [&](core::Edit& e) { e.page(P).skew = 1.5; });
    check("audit: an unread page with an angle is named", !core::audit(b).empty());
    j.run(b, "Bad", [&](core::Edit& e) { e.page(P).read = true; e.page(P).skew = 30.0; });
    check("audit: an angle past the measure is named", !core::audit(b).empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace

void condition_tests() {
    const fs::path dir = temp_dir("condition");
    skew_measure_tests(dir);
    tone_tests();
    book_tests(dir);
    audit_tests();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
