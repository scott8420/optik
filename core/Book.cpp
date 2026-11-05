#include "Book.hpp"

#include "model/Image.hpp"
#include "model/Spread.hpp"

#include <json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace optik::core {

namespace {

constexpr int kSchema = 1;
constexpr const char* kFile = "book.json";

// -- encode / decode: the pump. Both halves in this one place, so a field added
// to one is seen beside the other (CANON: pumps at conceptual seams).

json rect_out(const Rect& r) { return json::array({r.x0, r.y0, r.x1, r.y1}); }
Rect rect_in(const json& j) {
    Rect r;
    if (j.is_array() && j.size() == 4) {
        r.x0 = j[0].get<int>(); r.y0 = j[1].get<int>();
        r.x1 = j[2].get<int>(); r.y1 = j[3].get<int>();
    }
    return r;
}

// A word is an array, not an object: a book holds a hundred thousand of them,
// and the file stays readable either way. [text, box, conf, flags, original?]
json word_out(const ocr::Word& w) {
    const int flags = (w.checked ? 1 : 0) | (w.edited ? 2 : 0);
    json j = json::array({w.text, rect_out(w.box), std::round(w.conf * 10.0f) / 10.0f, flags});
    if (w.edited) j.push_back(w.original);
    return j;
}
ocr::Word word_in(const json& j) {
    ocr::Word w;
    w.text = j.at(0).get<std::string>();
    w.box  = rect_in(j.at(1));
    w.conf = j.at(2).get<float>();
    const int flags = j.at(3).get<int>();
    w.checked = flags & 1;
    w.edited  = flags & 2;
    if (j.size() > 4) w.original = j.at(4).get<std::string>();
    return w;
}

json area_out(const ocr::Area& a) {
    json paras = json::array();
    for (const auto& p : a.paras) {
        json lines = json::array();
        for (const auto& l : p.lines) {
            json words = json::array();
            for (const auto& w : l.words) words.push_back(word_out(w));
            lines.push_back({{"box", rect_out(l.box)}, {"words", words}});
        }
        paras.push_back({{"box", rect_out(p.box)}, {"lines", lines}});
    }
    json j = {{"kind", ocr::kind_name(a.kind)}, {"box", rect_out(a.box)}, {"paras", paras}};
    if (a.user) j["user"] = true;
    return j;
}
ocr::Area area_in(const json& j) {
    ocr::Area a;
    ocr::kind_from(j.value("kind", "text"), a.kind);
    a.box  = rect_in(j.at("box"));
    a.user = j.value("user", false);
    for (const auto& jp : j.at("paras")) {
        ocr::Para p;
        p.box = rect_in(jp.at("box"));
        for (const auto& jl : jp.at("lines")) {
            ocr::Line l;
            l.box = rect_in(jl.at("box"));
            for (const auto& jw : jl.at("words")) l.words.push_back(word_in(jw));
            p.lines.push_back(std::move(l));
        }
        a.paras.push_back(std::move(p));
    }
    return a;
}

const char* half_name(model::split::Half h) {
    using model::split::Half;
    return h == Half::Left ? "left" : h == Half::Right ? "right" : "whole";
}
model::split::Half half_in(const std::string& s) {
    using model::split::Half;
    return s == "left" ? Half::Left : s == "right" ? Half::Right : Half::Whole;
}

json settings_out(const Settings& s) {
    return {{"language", s.language}, {"uncertain_below", s.uncertain_below},
            {"split_spreads", s.split_spreads}, {"mark_running_heads", s.mark_running_heads},
            {"join_pages", s.join_pages}, {"dehyphenate", s.dehyphenate}};
}
Settings settings_in(const json& j) {
    Settings s;   // a key missing from an older file keeps its default
    s.language           = j.value("language", s.language);
    s.uncertain_below    = j.value("uncertain_below", s.uncertain_below);
    s.split_spreads      = j.value("split_spreads", s.split_spreads);
    s.mark_running_heads = j.value("mark_running_heads", s.mark_running_heads);
    s.join_pages         = j.value("join_pages", s.join_pages);
    s.dehyphenate        = j.value("dehyphenate", s.dehyphenate);
    return s;
}

}  // namespace

const char* status_text(Status s) {
    switch (s) {
        case Status::Ok:           return "ok";
        case Status::PathExists:   return "something is already there";
        case Status::NotABook:     return "that folder is not an Optik book (no book.json)";
        case Status::NotReadable:  return "could not read the book";
        case Status::BadJson:      return "book.json is damaged";
        case Status::SchemaTooNew: return "this book was saved by a newer Optik";
        case Status::WriteFailed:  return "could not write the book";
    }
    return "?";
}

Status Book::create(const std::string& folder, const std::string& title, Book& out) {
    std::error_code ec;
    if (fs::exists(folder, ec)) return Status::PathExists;
    if (!fs::create_directories(fs::path(folder) / "scans", ec) || ec) return Status::WriteFailed;
    out = Book{};
    out.m_folder = folder;
    out.m_meta.title = title.empty() ? fs::path(folder).stem().string() : title;
    return out.save();
}

Status Book::open(const std::string& folder, Book& out) {
    const fs::path file = fs::path(folder) / kFile;
    std::error_code ec;
    if (!fs::exists(file, ec)) return Status::NotABook;
    std::ifstream in(file);
    if (!in) return Status::NotReadable;
    json j;
    try { in >> j; } catch (...) { return Status::BadJson; }
    if (j.value("schema", 0) > kSchema) return Status::SchemaTooNew;

    Book b;
    b.m_folder = folder;
    try {
        b.m_meta.title    = j.value("title", "");
        b.m_meta.settings = settings_in(j.value("settings", json::object()));
        b.m_next_id       = j.value("next_id", 1);
        for (const auto& js : j.at("scans"))
            b.m_scans.push_back(Scan{js.at("id"), js.at("file"), js.value("source", "")});
        for (const auto& jp : j.at("pages")) {
            Page p;
            p.id       = jp.at("id");
            p.scan_id  = jp.at("scan");
            p.half     = half_in(jp.value("half", "whole"));
            p.crop     = rect_in(jp.at("crop"));
            p.concerns = jp.value("concerns", 0u);
            p.read     = jp.value("read", false);
            for (const auto& ja : jp.value("areas", json::array())) p.areas.push_back(area_in(ja));
            b.m_pages.push_back(std::move(p));
        }
    } catch (...) { return Status::BadJson; }
    out = std::move(b);
    return Status::Ok;
}

Status Book::save() const {
    json scans = json::array();
    for (const auto& s : m_scans)
        scans.push_back({{"id", s.id}, {"file", s.file}, {"source", s.source_name}});
    json pages = json::array();
    for (const auto& p : m_pages) {
        json areas = json::array();
        for (const auto& a : p.areas) areas.push_back(area_out(a));
        pages.push_back({{"id", p.id}, {"scan", p.scan_id}, {"half", half_name(p.half)},
                         {"crop", rect_out(p.crop)}, {"concerns", p.concerns},
                         {"read", p.read}, {"areas", areas}});
    }
    const json j = {{"schema", kSchema}, {"title", m_meta.title},
                    {"settings", settings_out(m_meta.settings)}, {"next_id", m_next_id},
                    {"scans", scans}, {"pages", pages}};

    // Write beside, then rename over: a crash mid-write leaves the old book.
    const fs::path file = fs::path(m_folder) / kFile;
    const fs::path tmp  = fs::path(m_folder) / (std::string(kFile) + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Status::WriteFailed;
        out << j.dump(1) << '\n';
        if (!out) return Status::WriteFailed;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    return ec ? Status::WriteFailed : Status::Ok;
}

std::string Book::mint(const char* prefix) {
    return std::string(prefix) + std::to_string(m_next_id++);
}

const Page* Book::page(const std::string& id) const {
    for (const auto& p : m_pages) if (p.id == id) return &p;
    return nullptr;
}

int Book::index_of(const std::string& id) const {
    for (std::size_t i = 0; i < m_pages.size(); ++i) if (m_pages[i].id == id) return int(i);
    return -1;
}

const Scan* Book::scan(const std::string& id) const {
    for (const auto& s : m_scans) if (s.id == id) return &s;
    return nullptr;
}

std::string Book::scan_path(const std::string& scan_id) const {
    const Scan* s = scan(scan_id);
    return s ? (fs::path(m_folder) / "scans" / s->file).string() : std::string();
}

bool Book::uncertain(const ocr::Word& w) const {
    return !w.checked && !w.edited && w.conf < float(m_meta.settings.uncertain_below);
}

int Book::uncertain_count(const Page& p) const {
    int n = 0;
    for (const auto& a : p.areas)
        if (ocr::kind_is_text(a.kind))
            for (const auto& para : a.paras)
                for (const auto& l : para.lines)
                    for (const auto& w : l.words) n += uncertain(w) ? 1 : 0;
    return n;
}

std::vector<Scan> copy_scans(const std::string& book_folder, const std::vector<std::string>& paths) {
    std::vector<Scan> out;
    const fs::path dir = fs::path(book_folder) / "scans";
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& src : paths) {
        Scan s;
        s.source_name = fs::path(src).filename().string();
        // Two folders may each hold a "001.tif": number the copy until it is new.
        s.file = s.source_name;
        for (int n = 2; fs::exists(dir / s.file, ec); ++n)
            s.file = fs::path(s.source_name).stem().string() + "_" + std::to_string(n) +
                     fs::path(s.source_name).extension().string();
        fs::copy_file(src, dir / s.file, ec);
        if (ec) continue;
        out.push_back(std::move(s));
    }
    return out;
}

// -- Import planning --------------------------------------------------------

ImportPlan plan_import(const std::vector<std::string>& paths, bool split_spreads) {
    namespace spread = model::spread;
    namespace split  = model::split;
    ImportPlan out;
    std::vector<split::Scan> scans;
    std::map<std::string, std::pair<int, int>> size_of;
    for (const auto& path : paths) {
        model::image::GreyImage img;
        if (!model::image::load_grey(path, img) || !img.valid()) {
            out.unreadable.push_back(path);
            continue;
        }
        split::Scan s;
        s.name = fs::path(path).filename().string();
        if (split_spreads) {
            s.result = spread::classify(img.view());
        } else {
            s.result = spread::Result{};
            s.result.width = img.width;
            s.result.height = img.height;
        }
        size_of[s.name] = {img.width, img.height};
        out.paths.push_back(path);
        scans.push_back(std::move(s));
    }
    out.pages = split::plan(std::move(scans)).pages;
    return out;
}

std::vector<Page> pages_for(const ImportPlan& plan, const std::vector<Scan>& scans) {
    std::map<std::string, std::string> id_of;
    for (const auto& s : scans) id_of[s.source_name] = s.id;
    std::vector<Page> out;
    for (const auto& ps : plan.pages) {
        auto it = id_of.find(ps.source_name);
        if (it == id_of.end()) continue;
        Page p;
        p.scan_id  = it->second;
        p.half     = ps.half;
        p.crop     = Rect{ps.x0, ps.y0, ps.x1, ps.y1};
        p.concerns = ps.concerns;
        out.push_back(std::move(p));
    }
    return out;
}

// -- Running heads -------------------------------------------------------------

void mark_running_heads(std::vector<ocr::Area>& areas, int page_height) {
    if (page_height <= 0) return;
    const int band = page_height / 10;   // the top and bottom tenth of the page
    for (auto& a : areas) {
        if (a.user || !ocr::kind_is_text(a.kind)) continue;
        int lines = 0;
        std::size_t chars = 0;
        for (const auto& p : a.paras) {
            lines += int(p.lines.size());
            chars += ocr::para_text(p).size();
        }
        if (lines != 1 || chars > 80) continue;
        if (a.box.y1 <= band)                    a.kind = ocr::Kind::Header;
        else if (a.box.y0 >= page_height - band) a.kind = ocr::Kind::Footer;
    }
}

}  // namespace optik::core
