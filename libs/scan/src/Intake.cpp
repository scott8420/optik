#include "model/Intake.hpp"
#include "model/Split.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace optik::model::intake {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool accepted(const std::string& ext, const Options& opt) {
    const std::string e = lower(ext);
    return std::find(opt.extensions.begin(), opt.extensions.end(), e) != opt.extensions.end();
}

bool hidden_name(const std::string& name) {
    return !name.empty() && name[0] == '.';
}

}  // namespace

std::string Report::explain() const {
    std::ostringstream o;

    if (!folder_exists)   return "That folder does not exist.";
    if (!folder_readable) return "That folder could not be read.";

    if (!scans.empty()) {
        o << scans.size() << " scan" << (scans.size() == 1 ? "" : "s") << " found";
        if (!skipped.empty()) o << ", " << skipped.size() << " other file(s) passed over";
        o << ".";
        return o.str();
    }

    // The case this whole file exists for. Say what was looked at and what the
    // commonest reason for losing was, because "no scans" on its own sends the
    // author looking for a bug in the wrong place.
    if (files_seen == 0) {
        return "That folder is empty.";
    }

    std::map<std::string, std::size_t> by_ext;
    for (const auto& s : skipped)
        if (s.reason == Skip::Extension)
            by_ext[s.extension.empty() ? "(no extension)" : lower(s.extension)]++;

    o << "No scans here. " << files_seen << " file"
      << (files_seen == 1 ? "" : "s") << " looked at";

    if (!by_ext.empty()) {
        auto top = std::max_element(by_ext.begin(), by_ext.end(),
                                    [](const auto& a, const auto& b) { return a.second < b.second; });
        o << ", " << top->second << " of them " << top->first;
        if (by_ext.size() > 1) o << " and " << (by_ext.size() - 1) << " other type(s)";
        o << ", none in the accepted set";
    }
    o << ".";
    return o.str();
}

Report discover(const std::string& folder, const Options& opt) {
    Report rep;

    std::error_code ec;
    const fs::path root(folder);

    if (!fs::exists(root, ec) || ec) return rep;
    rep.folder_exists = true;
    if (!fs::is_directory(root, ec) || ec) return rep;

    auto consider = [&](const fs::path& p, bool inside_hidden) {
        std::error_code fec;
        if (!fs::is_regular_file(p, fec) || fec) {
            rep.skipped.push_back({p.string(), Skip::NotAFile, ""});
            return;
        }
        ++rep.files_seen;

        const std::string name = p.filename().string();
        if (opt.skip_hidden && (inside_hidden || hidden_name(name))) {
            rep.skipped.push_back({p.string(), Skip::Hidden, p.extension().string()});
            return;
        }
        const std::string ext = p.extension().string();
        if (!accepted(ext, opt)) {
            rep.skipped.push_back({p.string(), Skip::Extension, ext});
            return;
        }
        rep.scans.push_back({p.string(), name});
    };

    if (opt.recursive) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) return rep;
        rep.folder_readable = true;
        for (const auto& entry : it) {
            // A dot-directory's whole subtree is sidecar clutter, not scans.
            bool inside_hidden = false;
            for (const auto& part : fs::relative(entry.path(), root, ec)) {
                const std::string s = part.string();
                if (s.size() > 1 && s[0] == '.') { inside_hidden = true; break; }
            }
            if (entry.is_directory(ec)) continue;
            consider(entry.path(), inside_hidden);
        }
    } else {
        fs::directory_iterator it(root, ec);
        if (ec) return rep;
        rep.folder_readable = true;
        for (const auto& entry : it) {
            if (entry.is_directory(ec)) continue;
            consider(entry.path(), false);
        }
    }

    // Same natural order the split pass sorts by, and for the same reason: page
    // order is the one thing an unpadded filename set gets silently wrong.
    // Sorting on the full path rather than the name keeps sibling subfolders
    // from interleaving with each other.
    std::sort(rep.scans.begin(), rep.scans.end(),
              [](const Found& a, const Found& b) { return split::natural_less(a.path, b.path); });

    return rep;
}

}  // namespace optik::model::intake
