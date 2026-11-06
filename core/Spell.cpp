#include "Spell.hpp"

#include <hunspell/hunspell.hxx>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace optik::core {

// -- the dictionary -------------------------------------------------------------

std::string dictionary_for(const std::string& language) {
    const std::string first = language.substr(0, language.find('+'));
    static const std::vector<std::pair<std::string, std::vector<std::string>>> names = {
        {"eng", {"en_US", "en_GB", "en_CA", "en_AU"}},
        {"fra", {"fr_FR", "fr"}},
        {"deu", {"de_DE", "de_DE_frami", "de"}},
        {"spa", {"es_ES", "es"}},
        {"ita", {"it_IT", "it"}},
        {"por", {"pt_PT", "pt_BR"}},
        {"nld", {"nl_NL", "nl"}},
    };
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("DICPATH")) {
        std::string s = env;
        for (std::size_t a = 0; a <= s.size();) {
            const std::size_t b = std::min(s.find(':', a), s.size());
            if (b > a) dirs.push_back(s.substr(a, b - a));
            a = b + 1;
        }
    }
    for (const char* d : {"/usr/share/hunspell", "/usr/share/myspell", "/usr/share/myspell/dicts"})
        dirs.push_back(d);
    for (const auto& [tess, dics] : names) {
        if (tess != first) continue;
        for (const auto& d : dirs)
            for (const auto& n : dics) {
                const fs::path base = fs::path(d) / n;
                std::error_code ec;
                if (fs::exists(base.string() + ".dic", ec) && fs::exists(base.string() + ".aff", ec))
                    return base.string();
            }
    }
    return {};
}

struct Speller::Impl {
    std::unique_ptr<Hunspell> h;
    std::string language;
};

Speller::Speller() : m(std::make_unique<Impl>()) {}
Speller::~Speller() = default;

bool Speller::open(const std::string& language, std::string* why) {
    m->h.reset();
    m->language = language;
    const std::string base = dictionary_for(language);
    if (base.empty()) {
        if (why) *why = "no spelling dictionary for '" + language + "' (install hunspell-en-US or your language's)";
        return false;
    }
    m->h = std::make_unique<Hunspell>((base + ".aff").c_str(), (base + ".dic").c_str());
    return true;
}

bool Speller::ready() const { return bool(m->h); }
const std::string& Speller::language() const { return m->language; }

bool Speller::known(const std::string& part) {
    return m->h && m->h->spell(part);
}

// -- what of a word is checked --------------------------------------------------

namespace {

// One code point from UTF-8 at s[i]; i moves past it. Bad bytes come back as
// themselves, one at a time.
char32_t next_cp(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + std::size_t(n) > s.size()) n = 1;
    char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3F);
    i += std::size_t(n);
    return cp;
}

bool is_letter(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;   // Latin-1 / Extended
    return c >= 0x370 && c < 0x2000;                              // Greek, Cyrillic, ...
}
bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }
bool is_apostrophe(char32_t c) { return c == '\'' || c == 0x2019; }

// A roman numeral in capitals, written the standard way (1..3999): read its
// value, write that value back, and it must come out the same. ("DID" does not.)
bool roman(const std::string& s) {
    if (s.empty() || s.size() > 15) return false;
    auto val = [](char c) {
        switch (c) { case 'I': return 1; case 'V': return 5; case 'X': return 10; case 'L': return 50;
                     case 'C': return 100; case 'D': return 500; case 'M': return 1000; default: return 0; }
    };
    int n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const int v = val(s[i]);
        if (!v) return false;
        n += (i + 1 < s.size() && val(s[i + 1]) > v) ? -v : v;
    }
    if (n <= 0 || n >= 4000) return false;
    static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* syms[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    std::string back;
    for (int k = 0; k < 13; ++k)
        while (n >= vals[k]) { back += syms[k]; n -= vals[k]; }
    return back == s;
}

std::string lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

}  // namespace

std::vector<std::string> spell_parts(const std::string& word) {
    // Pieces: runs of letters and digits, with an apostrophe kept when a letter
    // stands either side of it ("don't", "Nancy's").
    struct Cp { char32_t c; std::size_t a, b; };
    std::vector<Cp> cps;
    for (std::size_t i = 0; i < word.size();) {
        const std::size_t a = i;
        const char32_t c = next_cp(word, i);
        cps.push_back({c, a, i});
    }
    std::vector<std::string> out;
    std::string cur;
    int letters = 0;
    bool digit = false;
    auto flush = [&] {
        if (!cur.empty() && letters >= 2 && !digit && !roman(cur)) out.push_back(cur);
        cur.clear(); letters = 0; digit = false;
    };
    for (std::size_t k = 0; k < cps.size(); ++k) {
        const char32_t c = cps[k].c;
        if (is_letter(c)) { cur += word.substr(cps[k].a, cps[k].b - cps[k].a); ++letters; continue; }
        if (is_digit(c))  { cur += char(c); digit = true; continue; }
        if (is_apostrophe(c) && !cur.empty() && k + 1 < cps.size() && is_letter(cps[k + 1].c) &&
            is_letter(cps[k - 1].c)) {
            cur += '\'';
            continue;
        }
        flush();
    }
    flush();
    return out;
}

bool is_book_word(const std::vector<std::string>& words, const std::string& part) {
    const std::string l = lower_ascii(part);
    for (const auto& w : words)
        if (lower_ascii(w) == l) return true;
    return false;
}

bool add_book_word(std::vector<std::string>& words, const std::string& part) {
    if (part.empty() || is_book_word(words, part)) return false;
    words.insert(std::upper_bound(words.begin(), words.end(), part), part);
    return true;
}

// -- the pass -------------------------------------------------------------------

namespace {

bool all_known(const std::string& text, const KnownFn& known, const std::vector<std::string>& book) {
    for (const auto& p : spell_parts(text))
        if (!is_book_word(book, p) && !known(p)) return false;
    return true;
}

bool hyphen_at_end(const std::string& t) {
    return t.size() > 1 && t.back() == '-' && t[t.size() - 2] != '-';
}

}  // namespace

void mark_spelling(std::vector<ocr::Para>& paras, const KnownFn& known,
                   const std::vector<std::string>& book) {
    for (ocr::Para& p : paras) {
        std::vector<ocr::Word*> seq;
        std::vector<bool> line_end;
        for (ocr::Line& l : p.lines)
            for (std::size_t i = 0; i < l.words.size(); ++i) {
                seq.push_back(&l.words[i]);
                line_end.push_back(i + 1 == l.words.size());
            }
        auto set = [](ocr::Word* w, bool unknown) {
            if (!w->checked && !w->edited) w->unknown = unknown;
        };
        for (std::size_t i = 0; i < seq.size(); ++i) {
            ocr::Word* w = seq[i];
            if (line_end[i] && i + 1 < seq.size() && hyphen_at_end(w->text)) {
                ocr::Word* n = seq[i + 1];
                const std::string joined = w->text.substr(0, w->text.size() - 1) + n->text;
                const bool ok = all_known(joined, known, book) ||
                                (all_known(w->text, known, book) && all_known(n->text, known, book));
                set(w, !ok);
                set(n, !ok);
                ++i;
                continue;
            }
            set(w, !all_known(w->text, known, book));
        }
    }
}

void mark_spelling(std::vector<ocr::Area>& areas, const KnownFn& known,
                   const std::vector<std::string>& book) {
    for (ocr::Area& a : areas)
        if (ocr::kind_is_text(a.kind)) mark_spelling(a.paras, known, book);
}

int clear_book_words(std::vector<ocr::Area>& areas, const std::vector<std::string>& words) {
    int n = 0;
    for (ocr::Area& a : areas)
        for (ocr::Para& p : a.paras)
            for (ocr::Line& l : p.lines)
                for (ocr::Word& w : l.words) {
                    if (!w.unknown) continue;
                    const auto parts = spell_parts(w.text);
                    if (parts.empty()) continue;
                    bool all = true;
                    for (const auto& pt : parts) all = all && is_book_word(words, pt);
                    if (all) { w.unknown = false; ++n; }
                }
    return n;
}

}  // namespace optik::core
