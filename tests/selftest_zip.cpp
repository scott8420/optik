// selftest_zip.cpp -- s33: scans that arrive as a zip (Librioteca's Archive.zip).
#include "Check.hpp"

#include "core/Book.hpp"
#include "core/Undo.hpp"
#include "core/ZipIntake.hpp"

#include <zip.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace optik;
using optik::test::check;

namespace {
fs::path tmp(const char* tag) {
    const fs::path p = fs::temp_directory_path() /
        ("optik_selftest_zip_" + std::string(tag) + "_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Build a zip from (entry name, file on disk or literal bytes).
struct Entry { std::string name; std::string bytes; };
bool make_zip(const fs::path& out, const std::vector<Entry>& entries) {
    int err = 0;
    zip_t* z = zip_open(out.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) return false;
    for (const auto& e : entries) {
        if (!e.name.empty() && e.name.back() == '/') { zip_dir_add(z, e.name.c_str(), ZIP_FL_ENC_UTF_8); continue; }
        // libzip reads the buffer at close: keep a copy alive until then.
        char* buf = static_cast<char*>(std::malloc(e.bytes.size() ? e.bytes.size() : 1));
        std::copy(e.bytes.begin(), e.bytes.end(), buf);
        zip_source_t* src = zip_source_buffer(z, buf, e.bytes.size(), 1);   // 1: libzip frees it
        if (!src || zip_file_add(z, e.name.c_str(), src, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
            if (src) zip_source_free(src);
            zip_discard(z);
            return false;
        }
    }
    return zip_close(z) == 0;
}
}  // namespace

void zip_tests() {
    std::cout << "-- core: scans in a zip (s33)\n";
    namespace zi = core::zipin;

    check("zip: .zip and .ZIP are zips; a .tif is not",
          zi::is_zip("/a/Archive.zip") && zi::is_zip("b.ZIP") && !zi::is_zip("c.tif") && !zi::is_zip("zip"));

    check("zip: a generic Archive.zip takes its folder's name",
          zi::book_name("/lib/Fiction/Laumer, Keith/Retief's War/Archive.zip") == "Retief's War");
    check("zip: any other zip keeps its own name", zi::book_name("/x/Ivory Charm.zip") == "Ivory Charm");
    check("zip: a library zip's book is \"Author - Title\", made locally (s33b)",
          zi::book_folder("/run/user/1000/gvfs/smb-share:server=raspi.local,share=ssd1/EBOOKS/Fiction/"
                          "Laumer, Keith/Retief's War/Archive.zip", "/home/s/Documents/Optik")
              == "/home/s/Documents/Optik/Laumer, Keith - Retief's War.optik");
    check("zip: any other zip's book keeps the zip's name",
          zi::book_folder("/x/Ivory Charm.zip", "/b") == "/b/Ivory Charm.optik");
    {
        const char* old = std::getenv("XDG_CACHE_HOME");
        const std::string keep = old ? old : "";
        ::setenv("XDG_CACHE_HOME", "/tmp/cachetest", 1);
        const std::string s = zi::scratch_folder("Retief's War");
        check("zip: the scratch folder is under the cache, never the library",
              s.rfind("/tmp/cachetest/optik/intake/Retief's War-", 0) == 0, s);
        if (old) ::setenv("XDG_CACHE_HOME", keep.c_str(), 1); else ::unsetenv("XDG_CACHE_HOME");
    }

    const fs::path root = tmp("main");
    const fs::path book = root / "Retief's War";
    fs::create_directories(book);
    const fs::path zipf = book / "Archive.zip";
    const std::string page = slurp(std::string(OPTIK_TEST_DATA) + "/page_left.png");
    const std::string other = slurp(std::string(OPTIK_TEST_DATA) + "/chapter.png");
    const bool built = make_zip(zipf, {
        {"scans/", ""},
        {"scans/10.png", other},
        {"scans/2.png", page},
        {"cover.PNG", page},
        {"notes.txt", "hello"},
        {"__MACOSX/scans/._2.png", "junk"},
        {".hidden.png", page},
        {"../evil.png", page},
    });
    check("zip: the fixture zip was built", built);
    const auto before_size = fs::file_size(zipf);
    const auto before_time = fs::last_write_time(zipf);

    const auto l = zi::list(zipf.string());
    check("zip: list counts the scans and passes over the rest",
          l.opened && l.scans == 3 && l.skipped == 4 && l.files_seen == 7, l.explain());
    check("zip: list's line says how many scans", l.explain().rfind("3 scans in the zip", 0) == 0, l.explain());

    {
        const auto bad = zi::list((root / "nope.zip").string());
        check("zip: a missing zip is not ok and says so", !bad.opened && !bad.ok() && !bad.explain().empty(), bad.explain());
        std::ofstream(root / "fake.zip") << "not a zip at all";
        const auto fake = zi::list((root / "fake.zip").string());
        check("zip: a file that is not a zip is not ok", !fake.opened && !fake.ok(), fake.explain());
        make_zip(root / "text.zip", {{"readme.txt", "x"}});
        const auto txt = zi::list((root / "text.zip").string());
        check("zip: a zip with no scans is readable but not ok, and says why",
              txt.opened && !txt.ok() && txt.explain().find("No scans") == 0, txt.explain());
    }

    const fs::path scratch = root / "scratch";
    zi::Progress pr;
    const auto up = zi::unpack(zipf.string(), scratch.string(), {}, nullptr, &pr);
    check("zip: progress counts the scans unpacked (s33b)", pr.total == 3 && pr.done == 3);
    check("zip: unpack writes the three scans", up.written == 3 && !up.cancelled && up.error.empty(), up.error);
    check("zip: subfolders are kept, bytes are exact",
          slurp(scratch / "scans" / "2.png") == page && slurp(scratch / "scans" / "10.png") == other &&
          slurp(scratch / "cover.PNG") == page);
    check("zip: nothing is written outside the scratch folder (../evil.png)",
          !fs::exists(root / "evil.png") && !fs::exists(scratch.parent_path() / "evil.png"));
    check("zip: no __MACOSX, no hidden file, no notes",
          !fs::exists(scratch / "__MACOSX") && !fs::exists(scratch / ".hidden.png") && !fs::exists(scratch / "notes.txt"));
    check("zip: the zip itself is untouched",
          fs::file_size(zipf) == before_size && fs::last_write_time(zipf) == before_time);

    const auto found = model::intake::discover(scratch.string());
    check("zip: intake orders the unpacked scans naturally (2 before 10)",
          found.scans.size() == 3 &&
          fs::path(found.scans[1].path).filename() == "2.png" &&
          fs::path(found.scans[2].path).filename() == "10.png");

    {
        std::atomic<bool> stop{true};
        const fs::path s2 = root / "stopped";
        const auto c = zi::unpack(zipf.string(), s2.string(), {}, &stop);
        check("zip: a stopped unpack says cancelled and writes nothing", c.cancelled && c.written == 0);
    }

    {   // the whole path the window takes, headless: unpack -> plan -> copy -> one undo step
        std::vector<std::string> paths;
        for (const auto& f : found.scans) paths.push_back(f.path);
        const auto plan = core::plan_import(paths, false);
        core::Book b;
        const std::string dir = zi::book_folder(zipf.string(), (root / "books").string());
        const bool made = core::Book::create(dir, zi::book_name(zipf.string()), b) == core::Status::Ok;
        const auto copied = core::copy_scans(b.folder(), plan.paths);
        fs::remove_all(scratch);
        core::Journal j;
        j.run(b, "Import", [&](core::Edit& e) {
            std::vector<core::Scan> scans;
            for (const auto& s : copied) scans.push_back(e.add_scan(s));
            for (const auto& p : core::pages_for(plan, scans)) e.insert_page(e.book().pages().size(), p);
        });
        check("zip: the book is made in the books folder, titled for the zip's folder",
              made && fs::path(b.folder()).parent_path() == root / "books" && b.meta().title == "Retief's War",
              b.folder());
        check("zip: the scans are the book's own once the scratch folder is gone",
              b.pages().size() == 3 && copied.size() == 3 && fs::exists(b.scan_path(b.pages()[0].scan_id)));
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}
