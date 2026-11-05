// selftest_recent.cpp -- the app's memory of its books, and Save As's copy (s25).
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Recent.hpp"
#include "core/Undo.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {
fs::path tmp(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}
}  // namespace

void recent_tests() {
    std::cout << "-- core: recent books\n";
    {
        const fs::path dir = tmp("recent");
        const std::string file = (dir / "cfg" / "optik" / "recent.json").string();
        core::Book a, b;
        core::Book::create((dir / "a.optik").string(), "", a);
        core::Book::create((dir / "b.optik").string(), "", b);

        core::Recent r(file);
        r.load();
        check("recent: no file is an empty list", r.all().empty());
        r.touch((dir / "a.optik").string());
        r.touch((dir / "b.optik").string());
        r.touch((dir / "a.optik/").string());
        check("recent: newest first, each book once (a trailing slash is the same book)",
              r.all().size() == 2 && fs::path(r.all()[0]).filename() == "a.optik");
        check("recent: saves, making its folder", r.save() && fs::exists(file));

        core::Recent back(file);
        back.load();
        check("recent: loads what it saved", back.all() == r.all());

        fs::remove_all(dir / "b.optik");
        check("recent: a book that has gone is not offered",
              back.existing().size() == 1 && fs::path(back.existing()[0]).filename() == "a.optik");
        back.forget((dir / "b.optik").string());
        check("recent: forget takes it off the list", back.all().size() == 1);

        for (int i = 0; i < 15; ++i) back.touch((dir / ("x" + std::to_string(i))).string());
        check("recent: the list is capped", back.all().size() == core::Recent::kMax);

        { std::ofstream f(file, std::ios::trunc); f << "{ not json"; }
        core::Recent bad(file);
        bad.load();
        check("recent: a damaged file is an empty list, not an error", bad.all().empty());

        check("recent: the default file is in the config folder",
              core::Recent::default_file().find("optik/recent.json") != std::string::npos);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::cout << "-- core: save as (copy the book)\n";
    {
        const fs::path dir = tmp("saveas");
        const std::string from = (dir / "ivory.optik").string();
        core::Book b;
        core::Book::create(from, "Ivory", b);
        core::Journal j;
        j.run(b, "page", [](core::Edit& e) {
            core::Page p; p.crop = {0, 0, 10, 10}; p.read = true;
            e.insert_page(0, p);
        });
        b.save();
        { std::ofstream s(dir / "ivory.optik" / "scans" / "p1.png"); s << "scan bytes"; }

        const std::string to = (dir / "copy.optik").string();
        std::string why;
        check("saveas: the book folder is copied", core::copy_book(from, to, &why) == core::Status::Ok, why);
        core::Book c;
        check("saveas: the copy opens as the same book",
              core::Book::open(to, c) == core::Status::Ok && c.pages().size() == 1 &&
              c.meta().title == "Ivory");
        check("saveas: the scans come too", fs::exists(dir / "copy.optik" / "scans" / "p1.png"));
        check("saveas: never over something already there",
              core::copy_book(from, to, &why) == core::Status::PathExists);
        check("saveas: never inside itself",
              core::copy_book(from, from + "/inner.optik", &why) != core::Status::Ok &&
              !fs::exists(from + "/inner.optik"));
        check("saveas: no book, no copy",
              core::copy_book((dir / "nothing").string(), (dir / "n2").string(), &why) == core::Status::NotABook);
        core::Book o;
        check("saveas: the original is untouched",
              core::Book::open(from, o) == core::Status::Ok && o.pages().size() == 1);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
