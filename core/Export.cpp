#include "Export.hpp"

#include <zip.h>

#include <fstream>
#include <memory>

namespace optik::core {

namespace {

bool starts_lower(const std::string& s) {
    if (s.empty()) return false;
    const unsigned char c = static_cast<unsigned char>(s[0]);
    return c >= 'a' && c <= 'z';
}

bool ends_with(const std::string& s, std::size_t i, const char* tail) {
    const std::size_t n = std::char_traits<char>::length(tail);
    return i >= n && s.compare(i - n, n, tail) == 0;
}

// Does this paragraph end a sentence? Closing quotes and brackets are looked
// through to the mark before them; an em dash ("--" broken off) counts as an
// ending too, since a dialogue line cut off at the page turn is still a line.
bool ends_sentence(const std::string& s) {
    std::size_t i = s.size();
    for (bool again = true; again && i > 0;) {
        again = false;
        for (const char* t : {"\xE2\x80\x9D", "\xE2\x80\x99", "\"", "'", ")", "]", " "}) {
            if (ends_with(s, i, t)) { i -= std::char_traits<char>::length(t); again = true; break; }
        }
    }
    if (i == 0) return true;
    const char c = s[i - 1];
    return c == '.' || c == '!' || c == '?' || c == ':' || ends_with(s, i, "\xE2\x80\x94");
}

std::string xml_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

// '\n' inside a block is a line break inside its paragraph.
std::string with_breaks(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\n') o += "<text:line-break/>";
        else o += c;
    }
    return o;
}

}  // namespace

// A hyphen at a line end is mended only when the next line starts in lower case:
// "exam-" / "ple" is one word; "1914-" / "1918" and "well-" / "Known" are left.
// Ambiguous ones ("co-" / "operate") get mended; proofing is where that shows.
std::string join_lines(const std::vector<std::string>& lines, bool dehyphenate) {
    std::string out;
    for (const auto& raw : lines) {
        if (raw.empty()) continue;
        if (out.empty()) { out = raw; continue; }
        const bool hy = out.size() > 1 && out.back() == '-' && out[out.size() - 2] != '-';
        if (dehyphenate && hy && starts_lower(raw)) {
            out.pop_back();
            out += raw;
        } else {
            out += ' ';
            out += raw;
        }
    }
    return out;
}

std::vector<Block> flow(const Book& book) {
    const Settings& st = book.meta().settings;
    std::vector<Block> out;
    bool open_para = false;   // the last block may continue on the next page

    for (const Page& page : book.pages()) {
        bool first_on_page = true;
        bool last_heading  = false;   // the block before, on this page, was a heading
        for (const auto& area : page.areas) {
            if (!ocr::kind_is_text(area.kind)) continue;
            for (const auto& para : area.paras) {
                std::vector<std::string> lines;
                for (const auto& l : para.lines) {
                    std::string s;
                    for (const auto& w : l.words) {
                        if (w.text.empty()) continue;
                        if (!s.empty()) s += ' ';
                        s += w.text;
                    }
                    lines.push_back(std::move(s));
                }
                const bool heading = area.kind == ocr::Kind::Heading;
                // A heading keeps its lines ("CHAPTER II" / "AN IVORY CHARM"):
                // '\n' in a block is a line break inside it (ODT text:line-break).
                std::string text;
                if (heading) {
                    for (auto& l : lines)
                        if (!l.empty()) { if (!text.empty()) text += '\n'; text += l; }
                } else {
                    text = join_lines(lines, st.dehyphenate);
                }
                if (text.empty()) continue;

                // Joined to the paragraph before when that one did not end a
                // sentence and this one starts in lower case: either the page turn
                // broke it (if the book says to join there) or the engine split a
                // paragraph mid-sentence -- after an indented first line, or at a
                // hyphenated line end. A real paragraph does not start in lower case.
                const bool may_join = first_on_page ? st.join_pages : true;
                if (open_para && may_join && !heading && starts_lower(text) &&
                    !out.empty() && out.back().kind == Block::Paragraph) {
                    out.back().text = join_lines({out.back().text, text}, st.dehyphenate);
                } else if (heading && last_heading && !out.empty() &&
                           out.back().kind == Block::Heading && out.back().page_id == page.id) {
                    out.back().text += '\n' + text;   // one heading, read as two blocks
                } else {
                    out.push_back(Block{heading ? Block::Heading : Block::Paragraph, text, page.id});
                }
                last_heading = heading;
                first_on_page = false;
                open_para = !heading && !ends_sentence(out.back().text);
            }
        }
    }
    return out;
}

bool write_text(const std::vector<Block>& blocks, const std::string& path, std::string* why) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (why) *why = "could not write " + path; return false; }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i) f << "\n\n";
        f << blocks[i].text;
    }
    f << '\n';
    return bool(f);
}

// An ODT is a zip whose FIRST entry is "mimetype", stored uncompressed -- that
// is how a reader recognises it without unpacking. Everything else is ordinary.
bool write_odt(const std::vector<Block>& blocks, const std::string& title,
               const std::string& path, std::string* why) {
    const std::string mimetype = "application/vnd.oasis.opendocument.text";

    const std::string manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">\n"
        " <manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
        " <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>\n"
        "</manifest:manifest>\n";

    const std::string ns =
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
        " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
        " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
        " office:version=\"1.3\"";

    // Two handles for Writer, nothing more: the body and Heading 1. Writer's own
    // default styles carry the rest.
    const std::string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-styles" + ns + ">\n"
        " <office:styles>\n"
        "  <style:style style:name=\"Standard\" style:family=\"paragraph\" style:class=\"text\"/>\n"
        "  <style:style style:name=\"Text_20_body\" style:display-name=\"Text body\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:class=\"text\">\n"
        "   <style:paragraph-properties fo:margin-top=\"0in\" fo:margin-bottom=\"0.0835in\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Heading_20_1\" style:display-name=\"Heading 1\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:next-style-name=\"Text_20_body\" style:default-outline-level=\"1\" style:class=\"text\">\n"
        "   <style:text-properties fo:font-size=\"130%\" fo:font-weight=\"bold\"/>\n"
        "  </style:style>\n"
        " </office:styles>\n"
        "</office:document-styles>\n";

    const std::string meta =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta" + ns + ">\n"
        " <office:meta><dc:title>" + xml_escape(title) + "</dc:title>"
        "<meta:generator>Optik " OPTIK_BUILD "</meta:generator></office:meta>\n"
        "</office:document-meta>\n";

    std::string content =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content" + ns + ">\n"
        " <office:body>\n  <office:text>\n";
    for (const auto& b : blocks) {
        if (b.kind == Block::Heading)
            content += "   <text:h text:style-name=\"Heading_20_1\" text:outline-level=\"1\">" +
                       with_breaks(xml_escape(b.text)) + "</text:h>\n";
        else
            content += "   <text:p text:style-name=\"Text_20_body\">" + with_breaks(xml_escape(b.text)) + "</text:p>\n";
    }
    content += "  </office:text>\n </office:body>\n</office:document-content>\n";

    int err = 0;
    zip_t* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) { if (why) *why = "could not create " + path; return false; }

    // The buffers above outlive zip_close (they are this frame's locals), which
    // is what libzip needs: it reads them at close, not at add.
    auto add = [&](const char* name, const std::string& data, bool store) {
        zip_source_t* src = zip_source_buffer(za, data.data(), data.size(), 0);
        if (!src) return false;
        const zip_int64_t idx = zip_file_add(za, name, src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (idx < 0) { zip_source_free(src); return false; }
        if (store) zip_set_file_compression(za, zip_uint64_t(idx), ZIP_CM_STORE, 0);
        return true;
    };
    const bool ok = add("mimetype", mimetype, true) &&
                    add("META-INF/manifest.xml", manifest, false) &&
                    add("styles.xml", styles, false) &&
                    add("meta.xml", meta, false) &&
                    add("content.xml", content, false);
    if (!ok) {
        zip_discard(za);
        if (why) *why = "could not write " + path;
        return false;
    }
    if (zip_close(za) != 0) {
        if (why) *why = std::string("could not finish ") + path + ": " + zip_strerror(za);
        zip_discard(za);
        return false;
    }
    return true;
}

}  // namespace optik::core
