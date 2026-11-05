#include "Recent.hpp"

#include "Book.hpp"

#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace optik::core {

namespace {

// One spelling per book: absolute, normal, no trailing slash.
std::string norm(const std::string& folder) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(folder), ec).lexically_normal();
    std::string s = p.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

}  // namespace

std::string Recent::default_file() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = (xdg && *xdg) ? fs::path(xdg)
                  : fs::path(home && *home ? home : ".") / ".config";
    return (base / "optik" / "recent.json").string();
}

Recent::Recent(std::string file) : m_file(std::move(file)) {}

void Recent::load() {
    m_books.clear();
    std::ifstream in(m_file);
    if (!in) return;
    try {
        json j;
        in >> j;
        for (const auto& b : j.value("books", json::array()))
            if (b.is_string() && m_books.size() < kMax) {
                const std::string s = norm(b.get<std::string>());
                if (std::find(m_books.begin(), m_books.end(), s) == m_books.end()) m_books.push_back(s);
            }
    } catch (...) {
        m_books.clear();
    }
}

bool Recent::save() const {
    std::error_code ec;
    fs::create_directories(fs::path(m_file).parent_path(), ec);
    const std::string tmp = m_file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << json{{"books", m_books}}.dump(1) << '\n';
        if (!out) return false;
    }
    fs::rename(tmp, m_file, ec);
    return !ec;
}

void Recent::touch(const std::string& book_folder) {
    const std::string s = norm(book_folder);
    forget(s);
    m_books.insert(m_books.begin(), s);
    if (m_books.size() > kMax) m_books.resize(kMax);
}

void Recent::forget(const std::string& book_folder) {
    const std::string s = norm(book_folder);
    m_books.erase(std::remove(m_books.begin(), m_books.end(), s), m_books.end());
}

std::vector<std::string> Recent::existing() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& b : m_books)
        if (fs::exists(fs::path(b) / "book.json", ec)) out.push_back(b);
    return out;
}

Status copy_book(const std::string& from, const std::string& to, std::string* why) {
    std::error_code ec;
    if (!fs::exists(fs::path(from) / "book.json", ec)) {
        if (why) *why = "there is no book at " + from;
        return Status::NotABook;
    }
    if (fs::exists(to, ec)) {
        if (why) *why = "something is already at " + to;
        return Status::PathExists;
    }
    const fs::path a = fs::weakly_canonical(from, ec), b = fs::weakly_canonical(to, ec);
    if (!ec && std::mismatch(a.begin(), a.end(), b.begin(), b.end()).first == a.end()) {
        if (why) *why = "a book cannot be saved inside itself";
        return Status::PathExists;
    }
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        if (why) *why = "could not copy the book: " + ec.message();
        std::error_code ec2;
        fs::remove_all(to, ec2);   // half a book is no book
        return Status::WriteFailed;
    }
    return Status::Ok;
}

}  // namespace optik::core
