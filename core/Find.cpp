#include "Find.hpp"

#include <regex.h>

#include <algorithm>
#include <memory>
#include <set>

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

namespace {

// A line as grep sees it (s30, Scott: "^ or $ of a line"): its words' texts
// joined by one space, and where each word lies in it. Empty words are not in it.
struct LineText {
    std::string text;
    std::vector<std::size_t> word, start, end;   // per word in the text
};
LineText line_text(const std::vector<ocr::Word>& ws) {
    LineText t;
    for (std::size_t w = 0; w < ws.size(); ++w) {
        if (ws[w].text.empty()) continue;
        if (!t.text.empty()) t.text += ' ';
        t.word.push_back(w);
        t.start.push_back(t.text.size());
        t.text += ws[w].text;
        t.end.push_back(t.text.size());
    }
    return t;
}
// The word (index into t.word) a match starting at `at` begins in: the one
// holding it, or -- at a space -- the word before the space.
std::size_t first_word(const LineText& t, std::size_t at) {
    std::size_t k = 0;
    while (k + 1 < t.start.size() && t.start[k + 1] <= at) ++k;
    return k;
}
// ...and the one it ends in (its last byte): holding it, or -- a space -- the
// word after.
std::size_t last_word(const LineText& t, std::size_t end) {
    const std::size_t b = end - 1;
    for (std::size_t k = 0; k < t.start.size(); ++k)
        if (b < t.end[k]) return k;
    return t.start.size() - 1;
}

}  // namespace

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
                    if (o.grep) {
                        // The line's text: a hit is the word each match begins in, once.
                        const LineText t = line_text(ws);
                        int last = -1;
                        for (const Span& s : spans(t.text, q, o, re.get())) {
                            const int w = int(t.word[first_word(t, s.at)]);
                            if (w != last) out.push_back(Hit{p.id, int(a), int(r), int(l), w});
                            last = w;
                        }
                        continue;
                    }
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

namespace {

void set_text(ocr::Word& w, const std::string& now) {
    if (w.text == now) return;
    if (!w.edited) w.original = w.text;
    w.text = now;
    w.edited = w.text != w.original;
    if (!w.edited) w.original.clear();
}

// Grep's replace on one line: the matches that begin in a hit word, each
// rewriting the stretch of words it touches -- the stretch's text with the
// match replaced, split at its spaces and dealt back to the same words in
// order (the last takes any extra; words left over are emptied). Matches that
// touch a shared word are one stretch. Returns the matches replaced.
int replace_line(std::vector<ocr::Word>& ws, const std::set<int>& at_words, const std::string& q,
                 const std::string& with, const FindOptions& o) {
    const LineText t = line_text(ws);
    std::vector<Span> picked;
    for (Span& s : spans(t.text, q, o))
        if (at_words.count(int(t.word[first_word(t, s.at)]))) picked.push_back(std::move(s));
    if (picked.empty()) return 0;
    // Stretches: consecutive matches whose words meet or overlap.
    struct Stretch { std::size_t i, j; std::vector<const Span*> m; };
    std::vector<Stretch> st;
    for (const Span& s : picked) {
        const std::size_t i = first_word(t, s.at), j = std::max(i, last_word(t, s.at + s.len));
        if (!st.empty() && i <= st.back().j) { st.back().j = std::max(st.back().j, j); st.back().m.push_back(&s); }
        else st.push_back({i, j, {&s}});
    }
    for (auto it = st.rbegin(); it != st.rend(); ++it) {
        const std::size_t from = t.start[it->i], to = t.end[it->j];
        std::string now;
        std::size_t pos = from;
        for (const Span* s : it->m) {
            now.append(t.text, pos, s->at - pos);
            now += filled(with, t.text, *s);
            pos = s->at + s->len;
        }
        now.append(t.text, pos, to - pos);
        std::vector<std::string> parts;
        for (std::size_t a = 0; a <= now.size();) {
            const std::size_t b = std::min(now.find(' ', a), now.size());
            if (b > a) parts.push_back(now.substr(a, b - a));
            a = b + 1;
        }
        const std::size_t n = it->j - it->i + 1;
        for (std::size_t k = 0; k < n; ++k) {
            std::string text;
            if (k + 1 < n) { if (k < parts.size()) text = parts[k]; }
            else for (std::size_t m = k; m < parts.size(); ++m) text += (m > k ? " " : "") + parts[m];
            set_text(ws[t.word[it->i + k]], text);
        }
    }
    return int(picked.size());
}

}  // namespace

int replace_hits(Edit& e, const std::vector<Hit>& hits, const std::string& q,
                 const std::string& with, const FindOptions& o) {
    int total = 0;
    if (o.grep) {
        // By line, each once, with the words its hits begin in.
        std::vector<std::pair<Hit, std::set<int>>> lines;
        for (const Hit& h : hits) {
            Hit key = h;
            key.word = -1;
            auto f = std::find_if(lines.begin(), lines.end(), [&](const auto& x) { return x.first == key; });
            if (f == lines.end()) lines.push_back({key, {h.word}});
            else f->second.insert(h.word);
        }
        for (const auto& [h, words] : lines) {
            const Page* look = e.book().page(h.page_id);
            if (!look || h.area < 0 || h.area >= int(look->areas.size())) continue;
            const auto& a = look->areas[std::size_t(h.area)];
            if (h.para < 0 || h.para >= int(a.paras.size())) continue;
            const auto& p = a.paras[std::size_t(h.para)];
            if (h.line < 0 || h.line >= int(p.lines.size())) continue;
            // Work on a copy; write only if something changed (a stale hit is no step).
            std::vector<ocr::Word> ws = p.lines[std::size_t(h.line)].words;
            const int n = replace_line(ws, words, q, with, o);
            if (n == 0 || ws == p.lines[std::size_t(h.line)].words) continue;
            e.page(h.page_id).areas[std::size_t(h.area)].paras[std::size_t(h.para)]
                .lines[std::size_t(h.line)].words = std::move(ws);
            total += n;
        }
        return total;
    }
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
        set_text(e.page(h.page_id).areas[std::size_t(h.area)].paras[std::size_t(h.para)]
                     .lines[std::size_t(h.line)].words[std::size_t(h.word)], now);
        total += n;
    }
    return total;
}

}  // namespace optik::core
