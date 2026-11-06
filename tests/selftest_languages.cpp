// selftest_languages.cpp -- the languages a book reads in (s30, milestone 19):
// what is installed (a folder the test makes, and this machine's eng), the
// names, the setting's codes kept in order and never emptied, the popover's
// list with what the book names but this machine lacks.
#include "Check.hpp"

#include "core/Languages.hpp"
#include "ocr/Ocr.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& c : v) s += (s.empty() ? "" : ",") + c;
    return s;
}

bool has(const std::vector<std::string>& v, const std::string& c) {
    return std::find(v.begin(), v.end(), c) != v.end();
}

}  // namespace

void languages_tests() {
    std::cout << "-- languages (s30)\n";
    const fs::path dir = fs::temp_directory_path() / ("optik_selftest_langs_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "a", ec);
    fs::create_directories(dir / "b", ec);
    for (const char* f : {"a/eng.traineddata", "a/fra.traineddata", "a/osd.traineddata",
                          "a/equ.traineddata", "a/readme.txt", "b/eng.traineddata", "b/deu.traineddata"})
        std::ofstream(dir / f) << "x";

    const auto found = core::installed_codes({(dir / "a").string(), (dir / "b").string(),
                                              (dir / "nowhere").string()});
    check("installed: every .traineddata, one each, by name (English, French, German)",
          joined(found) == "eng,fra,deu", joined(found));
    check("installed: osd and equ are not languages to read in", !has(found, "osd") && !has(found, "equ"));
    check("installed: a folder that is not there is nothing, not a failure",
          core::installed_codes({(dir / "nowhere").string()}).empty());

    const auto here = core::installed_codes();
    check("installed: this machine reads English (the selftest's own pages need it)", has(here, "eng"),
          joined(here));
    if (has(here, "fra")) {
        ocr::Engine e;
        std::string why;
        check("installed: a second listed language loads with the first (eng+fra)", e.init("eng+fra", &why), why);
    } else {
        check("installed: a second language -- none here besides eng, not tried", true, "not installed here");
    }

    check("names: deu is German", core::language_name("deu") == "German");
    check("names: chi_sim is Chinese (Simplified)", core::language_name("chi_sim") == "Chinese (Simplified)");
    check("names: an unknown code is itself", core::language_name("tlh") == "tlh");

    check("setting: eng+fra is eng, fra", joined(core::split_languages("eng+fra")) == "eng,fra");
    check("setting: empty parts and repeats dropped",
          joined(core::split_languages("+eng++fra+eng+")) == "eng,fra");
    check("setting: nothing is nothing", core::split_languages("").empty());
    check("setting: back to eng+fra", core::join_languages({"eng", "", "fra"}) == "eng+fra");

    check("tick: a language is added after the book's own", core::with_language("eng", "fra", true) == "eng+fra");
    check("tick: ticked twice is once", core::with_language("eng+fra", "fra", true) == "eng+fra");
    check("untick: taken out, the order kept", core::with_language("eng+fra+deu", "fra", false) == "eng+deu");
    check("untick: the first taken out, the next is the book's own",
          core::with_language("eng+fra", "eng", false) == "fra");
    check("untick: the last stays -- a book reads in something", core::with_language("eng", "eng", false) == "eng");
    check("untick: one it does not read in changes nothing", core::with_language("eng", "fra", false) == "eng");

    const auto list = core::languages_for("eng+tlh", {"eng", "fra"});
    check("list: the installed, then what the book names but this machine lacks",
          list.size() == 3 && list[0].code == "eng" && list[1].code == "fra" && list[2].code == "tlh" &&
          list[0].installed && list[1].installed && !list[2].installed && list[0].name == "English");
    check("list: a language Optik knows no dictionary for has none", list[2].dictionary.empty());

    fs::remove_all(dir, ec);
}
