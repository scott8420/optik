#include "Audit.hpp"

#include <cmath>
#include <set>

namespace optik::core {

Size2i page_size(const Page& p) {
    if (p.turns & 1) return {p.crop.height(), p.crop.width()};
    return {p.crop.width(), p.crop.height()};
}

namespace {

// "pg_12" -> 12; -1 when the id is not prefix + digits.
long id_number(const std::string& id, const std::string& prefix) {
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) return -1;
    long n = 0;
    for (std::size_t i = prefix.size(); i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return -1;
        n = n * 10 + (id[i] - '0');
    }
    return n;
}

// Inside the page, with the slop an engine's box may have at the page's edge.
bool on_page(const Rect& r, const Size2i& s) {
    constexpr int slop = 2;
    return r.x0 >= -slop && r.y0 >= -slop && r.x1 <= s.w + slop && r.y1 <= s.h + slop;
}

std::string rs(const Rect& r) {
    return "[" + std::to_string(r.x0) + "," + std::to_string(r.y0) + " " +
           std::to_string(r.x1) + "," + std::to_string(r.y1) + "]";
}

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

}  // namespace

std::vector<std::string> audit(const Book& book) {
    std::vector<std::string> bad;
    auto say = [&](const std::string& s) { bad.push_back(s); };

    std::set<std::string> scan_ids;
    for (const auto& s : book.scans()) {
        if (!scan_ids.insert(s.id).second) say("scan id used twice: " + s.id);
        const long n = id_number(s.id, "scn_");
        if (n < 0) say("scan id not minted: '" + s.id + "'");
        else if (std::uint64_t(n) >= book.next_id()) say("scan id " + s.id + " not below next_id");
        if (s.file.empty() || s.file.find('/') != std::string::npos)
            say("scan " + s.id + " file is not a bare name: '" + s.file + "'");
    }

    std::set<std::string> page_ids;
    for (std::size_t i = 0; i < book.pages().size(); ++i) {
        const Page& p = book.pages()[i];
        const std::string at = "page " + std::to_string(i + 1) + " (" + p.id + ")";
        if (!page_ids.insert(p.id).second) say("page id used twice: " + p.id);
        const long n = id_number(p.id, "pg_");
        if (n < 0) say(at + ": id not minted");
        else if (std::uint64_t(n) >= book.next_id()) say(at + ": id not below next_id");
        if (!scan_ids.count(p.scan_id)) say(at + ": no scan " + p.scan_id);
        if (p.crop.empty()) say(at + ": empty crop " + rs(p.crop));
        if (p.crop.x0 < 0 || p.crop.y0 < 0) say(at + ": crop off the scan " + rs(p.crop));
        if (p.turns < 0 || p.turns > 3) say(at + ": turns " + std::to_string(p.turns) + " is not 0..3");
        if (!p.read && !p.areas.empty()) {
            // An unread page has no engine areas; only ones the author drew.
            for (const auto& a : p.areas)
                if (!a.user) { say(at + ": not read, yet holds an engine area"); break; }
        }
        const Size2i sz = page_size(p);
        for (std::size_t ai = 0; ai < p.areas.size(); ++ai) {
            const ocr::Area& a = p.areas[ai];
            const std::string aat = at + " area " + std::to_string(ai);
            if (a.box.empty()) say(aat + ": empty box " + rs(a.box));
            if (!on_page(a.box, sz)) say(aat + ": off the page " + rs(a.box));
            for (const auto& para : a.paras)
                for (const auto& line : para.lines)
                    for (const auto& w : line.words) {
                        if (w.conf < 0.0f || w.conf > 100.0f || std::isnan(w.conf))
                            say(aat + ": word '" + w.text + "' confidence " + std::to_string(w.conf));
                        if (!w.edited && !w.original.empty())
                            say(aat + ": word '" + w.text + "' keeps an original but is not edited");
                    }
        }
    }
    // s27: the book's own words are sorted, one each (case aside), none empty.
    const auto& bw = book.meta().words;
    for (std::size_t i = 0; i < bw.size(); ++i) {
        if (bw[i].empty()) say("an empty book word");
        if (i && !(bw[i - 1] < bw[i])) say("book words not sorted: '" + bw[i - 1] + "' then '" + bw[i] + "'");
        for (std::size_t j = 0; j < i; ++j)
            if (lower(bw[j]) == lower(bw[i])) say("book word twice: '" + bw[i] + "'");
    }
    if (book.meta().settings.uncertain_below < 0 || book.meta().settings.uncertain_below > 100)
        say("uncertain_below out of 0..100");
    return bad;
}

namespace {

bool near_words(const ocr::Word& a, const ocr::Word& b) {
    return a.text == b.text && a.box == b.box && std::fabs(a.conf - b.conf) <= 0.06f &&
           a.checked == b.checked && a.edited == b.edited && a.doubt == b.doubt &&
           a.italic == b.italic && a.unknown == b.unknown && ocr::fmt_of(a) == ocr::fmt_of(b) &&
           a.original == b.original;
}

bool near_areas(const ocr::Area& a, const ocr::Area& b) {
    if (a.kind != b.kind || a.box != b.box || a.user != b.user || a.paras.size() != b.paras.size())
        return false;
    for (std::size_t i = 0; i < a.paras.size(); ++i) {
        const auto& pa = a.paras[i];
        const auto& pb = b.paras[i];
        if (pa.box != pb.box || pa.lines.size() != pb.lines.size()) return false;
        for (std::size_t j = 0; j < pa.lines.size(); ++j) {
            const auto& la = pa.lines[j];
            const auto& lb = pb.lines[j];
            if (la.box != lb.box || la.words.size() != lb.words.size()) return false;
            for (std::size_t k = 0; k < la.words.size(); ++k)
                if (!near_words(la.words[k], lb.words[k])) return false;
        }
    }
    return true;
}

}  // namespace

bool same_book(const Book& a, const Book& b, std::string* why) {
    auto no = [&](const std::string& s) { if (why) *why = s; return false; };
    if (!(a.meta() == b.meta())) return no("meta differs");
    if (!(a.scans() == b.scans())) return no("scans differ");
    if (a.pages().size() != b.pages().size())
        return no("page count " + std::to_string(a.pages().size()) + " vs " + std::to_string(b.pages().size()));
    for (std::size_t i = 0; i < a.pages().size(); ++i) {
        const Page& x = a.pages()[i];
        const Page& y = b.pages()[i];
        const std::string at = "page " + std::to_string(i + 1) + " ";
        if (x.id != y.id) return no(at + "id " + x.id + " vs " + y.id);
        if (x.scan_id != y.scan_id || x.half != y.half || !(x.crop == y.crop) ||
            x.concerns != y.concerns || x.read != y.read || x.turns != y.turns)
            return no(at + "differs (" + x.id + ")");
        if (x.areas.size() != y.areas.size()) return no(at + "area count differs (" + x.id + ")");
        for (std::size_t k = 0; k < x.areas.size(); ++k)
            if (!near_areas(x.areas[k], y.areas[k]))
                return no(at + "area " + std::to_string(k) + " differs (" + x.id + ")");
    }
    return true;
}

}  // namespace optik::core
