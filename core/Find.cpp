#include "Find.hpp"

#include <regex.h>

#include <memory>

namespace optik::core {

namespace {

char lo(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

// Is the character that starts at / ends just before byte i part of a word, for
// "whole words"? ASCII letters and digits, and letters past ASCII (an accent) --
// but not the typographic quotes, dashes and guillemets books are full of.
bool wordish_cp(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (c == 0xAB || c == 0xBB || (c >= 0x2000 && c <= 0x206F)) return false;
    return c >= 0xC0;
}
char32_t decode_at(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + std::size_t(n) > s.size()) n = 1;
    char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3F);
    return cp;
}
bool wordish_after(const std::string& s, std::size_t i) { return i < s.size() && wordish_cp(decode_at(s, i)); }
bool wordish_before(const std::string& s, std::size_t i) {
    if (i == 0) return false;
    std::size_t k = i - 1;
    while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;   // back to the lead byte
    return wordish_cp(decode_at(s, k));
}

}  // namespace

namespace {

struct Span { std::size_t at = 0, len = 0; std::vector<std::pair<long, long>> groups; };

bool whole_ok(const std::string& text, std::size_t at, std::size_t len) {
    // The match is not inside a longer run of letters: what is beside it on the
    // outside is not wordish where its own edge is.
    const std::string m = text.substr(at, len);
    const bool left_ok  = !wordish_before(text, at) || !wordish_after(m, 0);
    const bool right_ok = !wordish_after(text, at + len) || !wordish_before(m, m.size());
    return left_ok && right_ok;
}

// A compiled grep pattern; null with the reason when it does not compile.
struct Regex {
    regex_t re{};
    bool ok = false;
    ~Regex() { if (ok) regfree(&re); }
};
std::unique_ptr<Regex> compile(const std::string& q, const FindOptions& o, std::string* why) {
    auto r = std::make_unique<Regex>();
    const int err = regcomp(&r->re, q.c_str(), REG_EXTENDED | (o.match_case ? 0 : REG_ICASE));
    if (err != 0) {
        if (why) {
            char buf[256];
            regerror(err, &r->re, buf, sizeof buf);
            *why = buf;
        }
        return nullptr;
    }
    r->ok = true;
    return r;
}

std::vector<Span> spans(const std::string& text, const std::string& q, const FindOptions& o,
                        const Regex* given = nullptr) {
    std::vector<Span> out;
    if (q.empty() || text.empty()) return out;
    if (!o.grep) {
        if (q.size() > text.size()) return out;
        for (std::size_t i = 0; i + q.size() <= text.size();) {
            bool same = true;
            for (std::size_t k = 0; same && k < q.size(); ++k)
                same = o.match_case ? text[i + k] == q[k] : lo(text[i + k]) == lo(q[k]);
            if (same && o.whole_words) same = whole_ok(text, i, q.size());
            if (same) { out.push_back(Span{i, q.size(), {}}); i += q.size(); }
            else ++i;
        }
        return out;
    }
    std::unique_ptr<Regex> own;
    const Regex* re = given;
    if (!re) { own = compile(q, o, nullptr); re = own.get(); }
    if (!re) return out;
    constexpr std::size_t kGroups = 10;
    regmatch_t m[kGroups];
    for (std::size_t i = 0; i < text.size();) {
        // From i on, ^ only at the word's own start (REG_NOTBOL past it).
        const char* from = text.c_str() + i;
        if (regexec(&re->re, from, kGroups, m, i > 0 ? REG_NOTBOL : 0) != 0) break;
        const std::size_t at = i + std::size_t(m[0].rm_so), len = std::size_t(m[0].rm_eo - m[0].rm_so);
        if (len == 0) { i = at + 1; continue; }   // an empty match is not a hit
        if (!o.whole_words || whole_ok(text, at, len)) {
            Span s{at, len, {}};
            for (std::size_t g = 0; g < kGroups; ++g)
                s.groups.push_back(m[g].rm_so < 0 ? std::pair<long, long>{-1, -1}
                                                  : std::pair<long, long>{long(i) + m[g].rm_so, long(i) + m[g].rm_eo});
            out.push_back(std::move(s));
            i = at + len;
        } else {
            i = at + 1;
        }
    }
    return out;
}

// sed's replacement: \1..\9 a group, & the whole match, \\ and \& themselves.
std::string filled(const std::string& with, const std::string& text, const Span& s) {
    std::string out;
    auto group = [&](std::size_t g) {
        if (g < s.groups.size() && s.groups[g].first >= 0)
            out.append(text, std::size_t(s.groups[g].first), std::size_t(s.groups[g].second - s.groups[g].first));
    };
    for (std::size_t i = 0; i < with.size(); ++i) {
        const char c = with[i];
        if (c == '\\' && i + 1 < with.size()) {
            const char d = with[++i];
            if (d >= '0' && d <= '9') group(std::size_t(d - '0'));
            else out += d;
        } else if (c == '&') {
            group(0);
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

bool pattern_ok(const std::string& q, const FindOptions& o, std::string* why) {
    if (!o.grep || q.empty()) return true;
    return compile(q, o, why) != nullptr;
}

std::vector<std::size_t> find_in(const std::string& text, const std::string& q, const FindOptions& o) {
    std::vector<std::size_t> at;
    for (const Span& s : spans(text, q, o)) at.push_back(s.at);
    return at;
}

std::vector<Hit> find_all(const Book& book, const std::string& q, const FindOptions& o) {
    std::vector<Hit> out;
    if (q.empty()) return out;
    // A pattern is compiled once for the whole book (s30); one that does not
    // compile finds nothing (the bar says why: pattern_ok).
    std::unique_ptr<Regex> re;
    if (o.grep && !(re = compile(q, o, nullptr))) return out;
    for (const Page& p : book.pages())
        for (std::size_t a = 0; a < p.areas.size(); ++a)
            for (std::size_t r = 0; r < p.areas[a].paras.size(); ++r)
                for (std::size_t l = 0; l < p.areas[a].paras[r].lines.size(); ++l) {
                    const auto& ws = p.areas[a].paras[r].lines[l].words;
                    for (std::size_t w = 0; w < ws.size(); ++w)
                        if (!spans(ws[w].text, q, o, re.get()).empty())
                            out.push_back(Hit{p.id, int(a), int(r), int(l), int(w)});
                }
    return out;
}

std::string replace_in(const std::string& text, const std::string& q, const std::string& with,
                       const FindOptions& o, int* n) {
    const auto found = spans(text, q, o);
    if (n) *n = int(found.size());
    if (found.empty()) return text;
    std::string out;
    std::size_t from = 0;
    for (const Span& s : found) {
        out.append(text, from, s.at - from);
        out += o.grep ? filled(with, text, s) : with;
        from = s.at + s.len;
    }
    out.append(text, from, std::string::npos);
    return out;
}

int replace_hits(Edit& e, const std::vector<Hit>& hits, const std::string& q,
                 const std::string& with, const FindOptions& o) {
    int total = 0;
    for (const Hit& h : hits) {
        const Page* look = e.book().page(h.page_id);
        if (!look || h.area < 0 || h.area >= int(look->areas.size())) continue;
        const auto& a = look->areas[std::size_t(h.area)];
        if (h.para < 0 || h.para >= int(a.paras.size())) continue;
        const auto& p = a.paras[std::size_t(h.para)];
        if (h.line < 0 || h.line >= int(p.lines.size())) continue;
        const auto& ws = p.lines[std::size_t(h.line)].words;
        if (h.word < 0 || h.word >= int(ws.size())) continue;
        int n = 0;
        const std::string now = replace_in(ws[std::size_t(h.word)].text, q, with, o, &n);
        if (n == 0) continue;
        ocr::Word& w = e.page(h.page_id).areas[std::size_t(h.area)].paras[std::size_t(h.para)]
                           .lines[std::size_t(h.line)].words[std::size_t(h.word)];
        if (!w.edited) w.original = w.text;
        w.text = now;
        w.edited = w.text != w.original;
        if (!w.edited) w.original.clear();
        total += n;
    }
    return total;
}

}  // namespace optik::core
