#include "core/ZipIntake.hpp"

#include <zip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace optik::core::zipin {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A name we will write: relative, no "..", no empty parts, not hidden, not a
// Mac resource-fork folder.
bool safe_and_visible(const std::string& name, const model::intake::Options& opt) {
    if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name.size() > 1 && name[1] == ':') return false;   // C:...
    for (const auto& part : fs::path(name)) {
        const std::string s = part.string();
        if (s == ".." || s == ".") return false;
        if (s == "__MACOSX") return false;
        if (opt.skip_hidden && !s.empty() && s[0] == '.') return false;
    }
    return true;
}

bool accepted(const std::string& name, const model::intake::Options& opt) {
    const std::string e = lower(fs::path(name).extension().string());
    return std::find(opt.extensions.begin(), opt.extensions.end(), e) != opt.extensions.end();
}

bool is_dir_entry(const std::string& name) { return !name.empty() && name.back() == '/'; }

bool wanted(const std::string& name, const model::intake::Options& opt) {
    if (!safe_and_visible(name, opt) || !accepted(name, opt)) return false;
    if (!opt.recursive && fs::path(name).has_parent_path() &&
        !fs::path(name).parent_path().empty()) return false;
    return true;
}

}  // namespace

bool is_zip(const std::string& path) {
    return lower(fs::path(path).extension().string()) == ".zip";
}

std::string Listing::explain() const {
    std::ostringstream o;
    if (!opened) return "That zip could not be read" + (error.empty() ? std::string(".") : ": " + error + ".");
    if (files_seen == 0) return "That zip is empty.";
    if (scans == 0) {
        o << "No scans in that zip. " << files_seen << " file" << (files_seen == 1 ? "" : "s")
          << " in it, none in the accepted set.";
        return o.str();
    }
    o << scans << " scan" << (scans == 1 ? "" : "s") << " in the zip";
    if (skipped) o << ", " << skipped << " other file(s) passed over";
    o << ".";
    return o.str();
}

Listing list(const std::string& zip, const model::intake::Options& opt) {
    Listing l;
    int err = 0;
    zip_t* z = zip_open(zip.c_str(), ZIP_RDONLY, &err);
    if (!z) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, err);
        l.error = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        return l;
    }
    l.opened = true;
    const zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        const char* nm = zip_get_name(z, zip_uint64_t(i), 0);
        if (!nm) continue;
        const std::string name = nm;
        if (is_dir_entry(name)) continue;
        ++l.files_seen;
        if (wanted(name, opt)) ++l.scans;
        else ++l.skipped;
    }
    zip_close(z);
    return l;
}

Unpacked unpack(const std::string& zip, const std::string& dest,
                const model::intake::Options& opt, const std::atomic<bool>* stop,
                Progress* progress) {
    Unpacked u;
    u.folder = dest;
    int err = 0;
    zip_t* z = zip_open(zip.c_str(), ZIP_RDONLY, &err);
    if (!z) { u.error = "the zip could not be read"; return u; }

    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) { zip_close(z); u.error = "could not make " + dest; return u; }

    std::vector<char> buf(1 << 16);
    const zip_int64_t n = zip_get_num_entries(z, 0);
    if (progress) {
        std::uint64_t want = 0;
        for (zip_int64_t i = 0; i < n; ++i) {
            const char* nm = zip_get_name(z, zip_uint64_t(i), 0);
            if (nm && !is_dir_entry(nm) && wanted(nm, opt)) ++want;
        }
        progress->done = 0;
        progress->total = want;
    }
    for (zip_int64_t i = 0; i < n; ++i) {
        if (stop && stop->load()) { u.cancelled = true; break; }
        const char* nm = zip_get_name(z, zip_uint64_t(i), 0);
        if (!nm) continue;
        const std::string name = nm;
        if (is_dir_entry(name) || !wanted(name, opt)) continue;

        const fs::path out = fs::path(dest) / fs::path(name).lexically_normal();
        fs::create_directories(out.parent_path(), ec);
        zip_file_t* f = zip_fopen_index(z, zip_uint64_t(i), 0);
        if (!f) { if (u.error.empty()) u.error = "could not read " + name; continue; }
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        bool good = bool(o);
        zip_int64_t got;
        while (good && (got = zip_fread(f, buf.data(), buf.size())) > 0) {
            o.write(buf.data(), std::streamsize(got));
            good = bool(o);
            if (stop && stop->load()) { u.cancelled = true; break; }
        }
        zip_fclose(f);
        o.close();
        if (u.cancelled) { fs::remove(out, ec); break; }
        if (!good) { if (u.error.empty()) u.error = "could not write " + out.string(); fs::remove(out, ec); continue; }
        ++u.written;
        if (progress) ++progress->done;
    }
    zip_close(z);
    return u;
}

std::string book_name(const std::string& zip) {
    const fs::path p = fs::path(zip).lexically_normal();
    const std::string stem = p.stem().string();
    static const char* generic[] = {"archive", "scans", "scan", "images", "pages", "tiff", "tiffs", "book"};
    const std::string ls = lower(stem);
    for (const char* g : generic)
        if (ls == g) {
            const std::string dir = p.parent_path().filename().string();
            if (!dir.empty()) return dir;
        }
    return stem.empty() ? std::string("Book") : stem;
}

std::string folder_name(const std::string& zip) {
    const fs::path p = fs::path(zip).lexically_normal();
    const std::string name = book_name(zip);
    if (name != p.stem().string()) {   // generic zip: named for its folder -- add the author's
        const std::string author = p.parent_path().parent_path().filename().string();
        if (!author.empty() && author != "/") return author + " - " + name;
    }
    return name;
}

std::string book_folder(const std::string& zip, const std::string& books_root) {
    std::string n;
    for (char c : folder_name(zip)) n += (c == '/' || c == '\\') ? '_' : c;
    return (fs::path(books_root) / (n + ".optik")).string();
}

std::string scratch_folder(const std::string& name) {
    fs::path base;
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) base = x;
    else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".cache";
    else base = fs::temp_directory_path();
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string safe;
    for (char c : name) safe += (c == '/' || c == '\\') ? '_' : c;
    return (base / "optik" / "intake" / (safe + "-" + std::to_string(t))).string();
}

}  // namespace optik::core::zipin
