#include "Languages.hpp"

#include "Spell.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace optik::core {

std::vector<std::string> tessdata_dirs() {
    std::vector<std::string> want;
    if (const char* p = std::getenv("TESSDATA_PREFIX"); p && *p) {
        want.push_back(p);
        want.push_back((fs::path(p) / "tessdata").string());
    }
    for (const char* d : {"/usr/share/tesseract/tessdata",          // Fedora
                          "/usr/share/tesseract-ocr/5/tessdata",    // Debian / Ubuntu, 5.x
                          "/usr/share/tesseract-ocr/4.00/tessdata", // ... 4.x
                          "/usr/share/tessdata",
                          "/usr/local/share/tessdata",
                          "/usr/local/share/tesseract-ocr/tessdata"})
        want.push_back(d);
    std::vector<std::string> out;
    for (const auto& d : want) {
        std::error_code ec;
        if (fs::is_directory(d, ec) && std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    }
    return out;
}

std::vector<std::string> installed_codes(const std::vector<std::string>& dirs) {
    std::set<std::string> codes;
    for (const auto& d : dirs) {
        std::error_code ec;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            if (p.extension() != ".traineddata") continue;
            const std::string code = p.stem().string();
            if (code.empty() || code == "osd" || code == "equ") continue;
            codes.insert(code);
        }
    }
    std::vector<std::string> v(codes.begin(), codes.end());
    std::stable_sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) {
        return language_name(a) < language_name(b);
    });
    return v;
}

std::vector<std::string> installed_codes() { return installed_codes(tessdata_dirs()); }

std::string language_name(const std::string& code) {
    static const std::map<std::string, std::string> names = {
        {"afr", "Afrikaans"}, {"ara", "Arabic"}, {"bul", "Bulgarian"}, {"cat", "Catalan"},
        {"ces", "Czech"}, {"chi_sim", "Chinese (Simplified)"}, {"chi_tra", "Chinese (Traditional)"},
        {"cym", "Welsh"}, {"dan", "Danish"}, {"deu", "German"}, {"deu_frak", "German (Fraktur)"},
        {"ell", "Greek"}, {"eng", "English"}, {"enm", "English (Middle)"}, {"epo", "Esperanto"},
        {"est", "Estonian"}, {"eus", "Basque"}, {"fin", "Finnish"}, {"fra", "French"},
        {"frk", "Fraktur"}, {"frm", "French (Middle)"}, {"gle", "Irish"}, {"glg", "Galician"},
        {"grc", "Greek (Ancient)"}, {"heb", "Hebrew"}, {"hin", "Hindi"}, {"hrv", "Croatian"},
        {"hun", "Hungarian"}, {"ind", "Indonesian"}, {"isl", "Icelandic"}, {"ita", "Italian"},
        {"ita_old", "Italian (Old)"}, {"jpn", "Japanese"}, {"jpn_vert", "Japanese (vertical)"},
        {"kor", "Korean"}, {"lat", "Latin"}, {"lav", "Latvian"}, {"lit", "Lithuanian"},
        {"nld", "Dutch"}, {"nor", "Norwegian"}, {"pol", "Polish"}, {"por", "Portuguese"},
        {"ron", "Romanian"}, {"rus", "Russian"}, {"slk", "Slovak"}, {"slv", "Slovenian"},
        {"spa", "Spanish"}, {"spa_old", "Spanish (Old)"}, {"sqi", "Albanian"}, {"srp", "Serbian"},
        {"swe", "Swedish"}, {"tha", "Thai"}, {"tur", "Turkish"}, {"ukr", "Ukrainian"},
        {"vie", "Vietnamese"}, {"yid", "Yiddish"},
    };
    const auto it = names.find(code);
    return it == names.end() ? code : it->second;
}

std::vector<std::string> split_languages(const std::string& setting) {
    std::vector<std::string> v;
    for (std::size_t a = 0; a <= setting.size();) {
        const std::size_t b = std::min(setting.find('+', a), setting.size());
        if (b > a) {
            const std::string c = setting.substr(a, b - a);
            if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
        }
        a = b + 1;
    }
    return v;
}

std::string join_languages(const std::vector<std::string>& codes) {
    std::string s;
    for (const auto& c : codes) {
        if (c.empty()) continue;
        if (!s.empty()) s += '+';
        s += c;
    }
    return s;
}

std::string with_language(const std::string& setting, const std::string& code, bool on) {
    std::vector<std::string> v = split_languages(setting);
    const auto it = std::find(v.begin(), v.end(), code);
    if (on) {
        if (it == v.end() && !code.empty()) v.push_back(code);
    } else if (it != v.end()) {
        if (v.size() == 1) return setting;   // a book reads in something
        v.erase(it);
    }
    return join_languages(v);
}

std::vector<Language> languages_for(const std::string& setting, const std::vector<std::string>& installed) {
    std::vector<Language> out;
    for (const auto& c : installed) out.push_back({c, language_name(c), true, dictionary_for(c)});
    std::vector<Language> missing;
    for (const auto& c : split_languages(setting))
        if (std::find(installed.begin(), installed.end(), c) == installed.end())
            missing.push_back({c, language_name(c), false, dictionary_for(c)});
    std::stable_sort(missing.begin(), missing.end(), [](const Language& a, const Language& b) { return a.name < b.name; });
    out.insert(out.end(), missing.begin(), missing.end());
    return out;
}

}  // namespace optik::core
