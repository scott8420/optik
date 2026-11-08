#include "Export.hpp"

#include <zip.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>

namespace optik::core {

namespace {

bool starts_lower(const std::string& s) {
    const std::size_t i = !s.empty() && s[0] == kFmtOn ? 2 : 0;
    if (s.size() <= i) return false;
    const unsigned char c = static_cast<unsigned char>(s[i]);
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
        for (const char* t : {"\xE2\x80\x9D", "\xE2\x80\x99", "\"", "'", ")", "]", " ", "\x02"}) {
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

// One ODT character style per format bit (s28).
const char* fmt_style(unsigned bit) {
    switch (bit) {
        case ocr::kItalic:      return "Emphasis";
        case ocr::kBold:        return "Strong_20_Emphasis";
        case ocr::kUnderline:   return "Underline";
        case ocr::kSmallCaps:   return "Small_20_Caps";
        case ocr::kSuperscript: return "Superscript";
    }
    return "Emphasis";
}

// A format bit's look, as ODF text properties.
const char* fmt_props(unsigned bit) {
    switch (bit) {
        case ocr::kItalic:      return " fo:font-style=\"italic\"";
        case ocr::kBold:        return " fo:font-weight=\"bold\"";
        case ocr::kUnderline:   return " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"";
        case ocr::kSmallCaps:   return " fo:font-variant=\"small-caps\"";
        case ocr::kSuperscript: return " style:text-position=\"super 58%\"";
    }
    return "";
}

unsigned low_bit(unsigned mask) { return mask & (~mask + 1u); }

// '\n' inside a block is a line break inside its paragraph (s27, s28).
// A format run is ONE named character style -- its first format's -- and, when
// it has more than one (bold italic), an automatic style inside it for the rest
// (s31): Composix keeps one character style a run, so nested named styles lost
// all but the innermost there; Writer reads it as the style plus direct formatting.
// `extra` collects the automatic styles' masks (named "T" + the mask).
std::string with_breaks(const std::string& s, std::vector<unsigned>* extra = nullptr) {
    std::string o;
    int open = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\n') o += "<text:line-break/>";
        else if (c == kFmtOn && i + 1 < s.size()) {
            const unsigned mask = unsigned(static_cast<unsigned char>(s[++i]) - '@') & ocr::kFmtAll;
            if (!mask) continue;
            const unsigned first = low_bit(mask), rest = mask & ~first;
            o += "<text:span text:style-name=\""; o += fmt_style(first); o += "\">"; ++open;
            if (rest) {
                o += "<text:span text:style-name=\"T" + std::to_string(rest) + "\">"; ++open;
                if (extra && std::find(extra->begin(), extra->end(), rest) == extra->end()) extra->push_back(rest);
            }
        } else if (c == kFmtOff) {
            for (; open > 0; --open) o += "</text:span>";
        } else o += c;
    }
    for (; open > 0; --open) o += "</text:span>";
    return o;
}

// Runs of the same format that meet become one: "a\x02 \x01Mb" -> "a b" when
// the run before was M too; "a\x02\x01Mb" -> "ab".
void fuse(std::string& s) {
    std::string o;
    o.reserve(s.size());
    char mask = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == kFmtOn && i + 1 < s.size()) { mask = s[i + 1]; o += c; o += s[++i]; continue; }
        if (c == kFmtOff) {
            std::size_t j = i + 1;
            const bool space = j < s.size() && s[j] == ' ';
            if (space) ++j;
            if (j + 1 < s.size() && s[j] == kFmtOn && s[j + 1] == mask) {
                if (space) o += ' ';
                i = j + 1;   // skip the close, the space (kept above) and the reopen
                continue;
            }
        }
        o += c;
    }
    s = std::move(o);
}

}  // namespace

std::string fmt_open(unsigned mask) {
    return std::string(1, kFmtOn) + char('@' + (mask & ocr::kFmtAll));
}

std::string plain(const std::string& marked) {
    std::string o;
    o.reserve(marked.size());
    for (std::size_t i = 0; i < marked.size(); ++i) {
        if (marked[i] == kFmtOn) { ++i; continue; }
        if (marked[i] != kFmtOff) o += marked[i];
    }
    return o;
}

// A hyphen at a line end is mended only when the next line starts in lower case:
// "exam-" / "ple" is one word; "1914-" / "1918" and "well-" / "Known" are left.
// Ambiguous ones ("co-" / "operate") get mended; proofing is where that shows.
std::string join_lines(const std::vector<std::string>& lines, bool dehyphenate) {
    std::string out;
    for (const auto& raw : lines) {
        if (raw.empty()) continue;
        if (out.empty()) { out = raw; continue; }
        // A formatted word at the line end closes its run after the hyphen.
        const bool shut = out.back() == kFmtOff;
        const std::size_t e = out.size() - (shut ? 1 : 0);
        const bool hy = e > 1 && out[e - 1] == '-' && out[e - 2] != '-';
        if (dehyphenate && hy && starts_lower(raw)) {
            out.erase(e - 1, 1);
            out += raw;
        } else {
            out += ' ';
            out += raw;
        }
        fuse(out);
    }
    return out;
}

std::vector<Block> flow(const Book& book) { return flow(book, ExportOptions{}); }

PageSpan span_of(const Book& book, const ExportOptions& opts) {
    const int n = int(book.pages().size());
    PageSpan s;
    s.from = std::max(opts.first, 1) - 1;
    s.to   = (opts.last <= 0 ? n : std::min(opts.last, n)) - 1;
    return s;
}

std::vector<Block> flow(const Book& book, const ExportOptions& opts) {
    const Settings& st = book.meta().settings;
    const PageSpan span = span_of(book, opts);
    const bool by_page = opts.breaks == ExportOptions::AtPages;
    std::vector<Block> out;
    bool open_para = false;      // the last paragraph may continue on the next page
    std::size_t open_at = 0;     // ...which block it is (heads kept may follow it)

    for (int pi = span.from; pi <= span.to; ++pi) {
        const Page& page = book.pages()[std::size_t(pi)];
        bool first_on_page = true;
        bool last_heading  = false;   // the block before, on this page, was a heading
        bool page_started  = false;   // a block of this page is out (AtPages breaks)
        auto push = [&](Block b) {
            if (!page_started && by_page && !out.empty()) b.break_before = true;
            if (b.kind == Block::Heading && opts.breaks == ExportOptions::AtHeadings && !out.empty())
                b.break_before = true;
            page_started = true;
            out.push_back(std::move(b));
        };
        for (const auto& area : page.areas) {
            // s31: a picture where it stands. It changes nothing about the
            // paragraphs around it: one broken by it (or by the page turn
            // after it) still joins, and the picture follows the whole of it.
            if (area.kind == ocr::Kind::Picture) {
                if (opts.pictures && !area.box.empty()) {
                    Block b{Block::Picture, "", page.id};
                    b.rect = area.box;
                    push(std::move(b));
                }
                continue;
            }
            if (!ocr::kind_is_text(area.kind)) {
                // s29: running heads and folios, kept where they stand.
                if (!opts.keep_heads || (area.kind != ocr::Kind::Header && area.kind != ocr::Kind::Footer))
                    continue;
                std::string text;
                for (const auto& para : area.paras) {
                    const std::string t = ocr::para_text(para);
                    if (t.empty()) continue;
                    if (!text.empty()) text += ' ';
                    for (char c : t) text += c == '\n' ? ' ' : c;
                }
                if (!text.empty()) push(Block{Block::RunningHead, text, page.id});
                continue;
            }
            for (const auto& para : area.paras) {
                std::vector<std::string> lines;
                for (const auto& l : para.lines) {
                    std::string s;
                    for (const auto& w : l.words) {
                        if (w.text.empty()) continue;
                        if (!s.empty()) s += ' ';
                        if (const unsigned f = ocr::fmt_of(w)) { s += fmt_open(f); s += w.text; s += kFmtOff; }
                        else s += w.text;
                    }
                    fuse(s);
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
                // (s29) Pages kept as pages (a break at each) do not join over the turn.
                const bool may_join = first_on_page ? (st.join_pages && !by_page) : true;
                if (open_para && may_join && !heading && starts_lower(text) &&
                    open_at < out.size() && out[open_at].kind == Block::Paragraph) {
                    out[open_at].text = join_lines({out[open_at].text, text}, st.dehyphenate);
                } else if (heading && last_heading && !out.empty() &&
                           out.back().kind == Block::Heading && out.back().page_id == page.id) {
                    out.back().text += '\n' + text;   // one heading, read as two blocks
                } else {
                    Block nb{heading ? Block::Heading : Block::Paragraph, text, page.id};
                    if (!heading && para.cap_letters > 0 && st.rebuild_dropcaps) {   // s32
                        nb.cap_letters = para.cap_letters;
                        nb.cap_lines   = std::clamp(para.cap_lines, 1, 9);
                    }
                    push(std::move(nb));
                    open_at = out.size() - 1;
                }
                last_heading = heading;
                first_on_page = false;
                open_para = !heading && !ends_sentence(out[open_at].text);
            }
        }
    }
    return out;
}

namespace {

std::string pt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2fpt", v);
    return b;
}

std::vector<std::string> split_nl(const std::string& s) {
    std::vector<std::string> out;
    std::size_t a = 0;
    for (;;) {
        const auto e = s.find('\n', a);
        out.push_back(s.substr(a, e == std::string::npos ? std::string::npos : e - a));
        if (e == std::string::npos) break;
        a = e + 1;
    }
    return out;
}

std::string join_sp(const std::vector<std::string>& v, std::size_t from) {
    std::string o;
    for (std::size_t i = from; i < v.size(); ++i) {
        if (v[i].empty()) continue;
        if (!o.empty()) o += ' ';
        o += v[i];
    }
    return o;
}

}  // namespace

bool is_chapter_label(const std::string& line) {
    std::string s = plain(line);
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == ':')) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && s[a] == ' ') ++a;
    s.erase(0, a);
    if (s.empty()) return false;
    // The line alone a number: "12", "XIV".
    const bool digits = s.size() <= 3 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    const bool roman = s.size() <= 8 && std::all_of(s.begin(), s.end(), [](char c) {
        return c == 'I' || c == 'V' || c == 'X' || c == 'L' || c == 'C' || c == 'D' || c == 'M';
    });
    if (digits || roman) return true;
    // Or a label word first, the line short: "CHAPTER II", "Chapter the Fourth", "BOOK THREE".
    std::vector<std::string> words;
    std::string w;
    for (char c : s) {
        if (c == ' ') { if (!w.empty()) words.push_back(w); w.clear(); }
        else w += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    if (!w.empty()) words.push_back(w);
    if (words.empty() || words.size() > 4) return false;
    for (const char* k : {"chapter", "book", "part", "volume", "section", "canto",
                          "prologue", "epilogue", "interlude", "appendix"})
        if (words[0] == k) return true;
    return false;
}

PageSize page_size(const std::vector<Measured>& pages, double ppi) {
    PageSize out;
    // Under 100 the number is a default, not a scanner's (a JPEG says 72 or 96
    // when nobody set it); over 2400 nobody scans a book.
    if (ppi < 100 || ppi > 2400) return out;
    std::vector<double> w, h, top, bottom, inner, outer;
    for (const auto& m : pages) {
        if (m.w <= 0 || m.h <= 0) continue;
        w.push_back(m.w);
        h.push_back(m.h);
        if (m.text.empty()) continue;
        const double l = std::max(0, m.text.x0), r = std::max(0, m.w - m.text.x1);
        top.push_back(std::max(0, m.text.y0));
        bottom.push_back(std::max(0, m.h - m.text.y1));
        // A verso's inner margin is on its right; a recto's (and a page not
        // split from a spread) on its left.
        inner.push_back(m.verso ? r : l);
        outer.push_back(m.verso ? l : r);
    }
    if (w.empty()) return out;
    const auto median = [](std::vector<double> v) {
        if (v.empty()) return 0.0;
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    const double k = 72.0 / ppi;
    out.w = median(w) * k;
    out.h = median(h) * k;
    // A page from two to twenty inches a side is a book's; anything else is a
    // wrong resolution, and the export keeps Writer's page instead.
    if (out.w < 144 || out.h < 144 || out.w > 1440 || out.h > 1440) return PageSize{};
    out.top = median(top) * k;
    out.bottom = median(bottom) * k;
    out.inner = median(inner) * k;
    out.outer = median(outer) * k;
    // The text must keep room: margins no more than a third of the page each.
    out.top = std::min(out.top, out.h / 3);
    out.bottom = std::min(out.bottom, out.h / 3);
    out.inner = std::min(out.inner, out.w / 3);
    out.outer = std::min(out.outer, out.w / 3);
    return out;
}

bool write_odt(const std::vector<Block>& blocks, const std::string& title,
               const std::string& path, std::string* why) {
    OdtBook b;
    b.title = title;
    return write_odt(blocks, b, path, why);
}

// An ODT is a zip whose FIRST entry is "mimetype", stored uncompressed -- that
// is how a reader recognises it without unpacking. Everything else is ordinary.
bool write_odt(const std::vector<Block>& blocks, const OdtBook& book,
               const std::string& path, std::string* why) {
    const std::string mimetype = "application/vnd.oasis.opendocument.text";

    const std::string ns =
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
        " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
        " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
        " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
        " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
        " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
        " office:version=\"1.3\"";

    // s31: the book's page -- its trim and margins, facing pages (inner /
    // outer). Composix's import takes its pages and master from this; Writer
    // opens the book at its own size. Unknown: Writer's default page.
    const PageSize& pg = book.page;
    std::string page_styles;
    if (pg.valid()) {
        page_styles =
            " <office:automatic-styles>\n"
            "  <style:page-layout style:name=\"pm1\" style:page-usage=\"mirrored\">\n"
            "   <style:page-layout-properties fo:page-width=\"" + pt(pg.w) + "\" fo:page-height=\"" + pt(pg.h) +
            "\" style:print-orientation=\"portrait\" fo:margin-top=\"" + pt(pg.top) + "\" fo:margin-bottom=\"" + pt(pg.bottom) +
            "\" fo:margin-left=\"" + pt(pg.inner) + "\" fo:margin-right=\"" + pt(pg.outer) + "\"/>\n"
            "  </style:page-layout>\n"
            " </office:automatic-styles>\n"
            " <office:master-styles>\n"
            "  <style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\"/>\n"
            " </office:master-styles>\n";
    }

    // Handles for Writer, nothing more: the body, Heading 1, (s31) Subtitle --
    // a chapter's title under its label, as Composix sets Folio's chapters --
    // and (s27) the character styles formats are set in. Writer's own
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
        "  <style:style style:name=\"Subtitle\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:next-style-name=\"Text_20_body\" style:class=\"chapter\">\n"
        "   <style:paragraph-properties fo:text-align=\"center\" fo:margin-bottom=\"0.17in\"/>\n"
        "   <style:text-properties fo:font-size=\"115%\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Running_20_Head\" style:display-name=\"Running Head\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:class=\"extra\">\n"
        "   <style:text-properties fo:font-size=\"85%\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Emphasis\" style:family=\"text\">\n"
        "   <style:text-properties fo:font-style=\"italic\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Strong_20_Emphasis\" style:display-name=\"Strong Emphasis\" style:family=\"text\">\n"
        "   <style:text-properties fo:font-weight=\"bold\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Underline\" style:family=\"text\">\n"
        "   <style:text-properties style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Small_20_Caps\" style:display-name=\"Small Caps\" style:family=\"text\">\n"
        "   <style:text-properties fo:font-variant=\"small-caps\"/>\n"
        "  </style:style>\n"
        "  <style:style style:name=\"Superscript\" style:family=\"text\">\n"
        "   <style:text-properties style:text-position=\"super 58%\"/>\n"
        "  </style:style>\n"
        " </office:styles>\n" + page_styles +
        "</office:document-styles>\n";

    const std::string meta =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta" + ns + ">\n"
        " <office:meta><dc:title>" + xml_escape(book.title) + "</dc:title>"
        "<meta:generator>Optik " OPTIK_BUILD "</meta:generator></office:meta>\n"
        "</office:document-meta>\n";

    // The body first: it says which automatic styles it needs.
    std::vector<unsigned> extra;
    std::vector<std::pair<std::string, const Art*>> pictures;   // zip name, its art
    std::string body;
    auto para = [&](const char* style, const std::string& text) {
        body += std::string("   <text:p text:style-name=\"") + style + "\">" + with_breaks(xml_escape(text), &extra) + "</text:p>\n";
    };
    std::vector<std::string> caps;   // s32: the drop-cap styles the body uses
    std::string cap_styles;
    std::size_t art_i = 0;
    bool carried = false;   // a picture left out passes its page break on
    for (const auto& blk : blocks) {
        Block b = blk;
        if (carried && b.kind != Block::Picture) { b.break_before = true; carried = false; }
        if (b.kind == Block::Picture) {
            const Art* art = art_i < book.art.size() ? &book.art[art_i] : nullptr;
            ++art_i;
            if (!art || art->png.empty() || art->w <= 0 || art->h <= 0) {
                carried = carried || b.break_before;
                continue;
            }
            if (carried) { b.break_before = true; carried = false; }
            // As big as it was on the page, and never wider or taller than the
            // text area (when the page is known).
            double w = art->w, h = art->h;
            if (pg.valid()) {
                const double tw = pg.w - pg.inner - pg.outer, th = pg.h - pg.top - pg.bottom;
                const double k = std::min({1.0, tw > 0 ? tw / w : 1.0, th > 0 ? th / h : 1.0});
                w *= k;
                h *= k;
            }
            const std::string n = std::to_string(pictures.size() + 1);
            const std::string zip_name = "Pictures/picture" + n + ".png";
            pictures.push_back({zip_name, art});
            // In its own centred paragraph, as a character (as-char): it moves
            // with the text in Writer, and Composix places it in the flow.
            body += std::string("   <text:p text:style-name=\"") + (b.break_before ? "P_pic_break" : "P_pic") + "\">"
                    "<draw:frame draw:style-name=\"fr_pic\" draw:name=\"Picture " + n + "\" text:anchor-type=\"as-char\""
                    " svg:width=\"" + pt(w) + "\" svg:height=\"" + pt(h) + "\" draw:z-index=\"0\">"
                    "<draw:image xlink:href=\"" + zip_name + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\""
                    " draw:mime-type=\"image/png\"/></draw:frame></text:p>\n";
            continue;
        }
        if (b.kind == Block::Heading) {
            // s31: a heading's lines. A chapter's label over its title ("CHAPTER
            // II" / "AN IVORY CHARM") is Heading 1 over a Subtitle -- one heading
            // in the outline, the title in the paragraph after. Other lines are
            // the scanned page's wrap, run on with a space. (A line break inside
            // the heading made two paragraphs in Composix, each starting a page.)
            const auto lines = split_nl(b.text);
            const bool labelled = lines.size() >= 2 && is_chapter_label(lines[0]) && !join_sp(lines, 1).empty();
            const std::string head = labelled ? lines[0] : join_sp(lines, 0);
            body += std::string("   <text:h text:style-name=\"") + (b.break_before ? "H_break" : "Heading_20_1") +
                    "\" text:outline-level=\"1\">" + with_breaks(xml_escape(head), &extra) + "</text:h>\n";
            if (labelled) para("Subtitle", join_sp(lines, 1));
        } else if (b.kind == Block::RunningHead) {
            para(b.break_before ? "R_break" : "Running_20_Head", b.text);
        } else if (b.cap_letters > 0) {
            // s32: a dropcap -- the body style with a drop cap of the letters
            // and lines the scan had (Writer draws it; Strip never gets here).
            const std::string name = "P_dc" + std::to_string(b.cap_lines) + "_" + std::to_string(b.cap_letters) +
                                     (b.break_before ? "_break" : "");
            if (std::find(caps.begin(), caps.end(), name) == caps.end()) {
                caps.push_back(name);
                cap_styles += "  <style:style style:name=\"" + name + "\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\">"
                              "<style:paragraph-properties" + std::string(b.break_before ? " fo:break-before=\"page\"" : "") + ">"
                              "<style:drop-cap style:lines=\"" + std::to_string(b.cap_lines) + "\" style:length=\"" +
                              std::to_string(b.cap_letters) + "\" style:distance=\"0.04in\"/></style:paragraph-properties></style:style>\n";
            }
            para(name.c_str(), b.text);
        } else {
            para(b.break_before ? "P_break" : "Text_20_body", b.text);
        }
    }

    std::string content =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content" + ns + ">\n"
        // s29: a page break is the paragraph's style with a break before it --
        // an automatic style on the named one, so Writer still sees Text body,
        // Heading 1, Running Head.
        " <office:automatic-styles>\n"
        "  <style:style style:name=\"P_break\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\"><style:paragraph-properties fo:break-before=\"page\"/></style:style>\n"
        "  <style:style style:name=\"H_break\" style:family=\"paragraph\" style:parent-style-name=\"Heading_20_1\"><style:paragraph-properties fo:break-before=\"page\"/></style:style>\n"
        "  <style:style style:name=\"R_break\" style:family=\"paragraph\" style:parent-style-name=\"Running_20_Head\"><style:paragraph-properties fo:break-before=\"page\"/></style:style>\n";
    if (!pictures.empty())
        content +=
            "  <style:style style:name=\"P_pic\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:paragraph-properties fo:text-align=\"center\" fo:margin-top=\"0.0835in\" fo:margin-bottom=\"0.0835in\"/></style:style>\n"
            "  <style:style style:name=\"P_pic_break\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:paragraph-properties fo:text-align=\"center\" fo:margin-top=\"0.0835in\" fo:margin-bottom=\"0.0835in\" fo:break-before=\"page\"/></style:style>\n"
            "  <style:style style:name=\"fr_pic\" style:family=\"graphic\"><style:graphic-properties style:vertical-pos=\"top\" style:vertical-rel=\"baseline\"/></style:style>\n";
    content += cap_styles;
    // s31: a run's formats after its first (T + the mask: T2 bold, T3 ...).
    std::sort(extra.begin(), extra.end());
    for (unsigned m : extra) {
        content += "  <style:style style:name=\"T" + std::to_string(m) + "\" style:family=\"text\"><style:text-properties";
        for (unsigned bit = 1; bit <= ocr::kSuperscript; bit <<= 1)
            if (m & bit) content += fmt_props(bit);
        content += "/></style:style>\n";
    }
    content += " </office:automatic-styles>\n <office:body>\n  <office:text>\n" + body +
               "  </office:text>\n </office:body>\n</office:document-content>\n";

    std::string manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">\n"
        " <manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
        " <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>\n";
    for (const auto& [name, art] : pictures)
        manifest += " <manifest:file-entry manifest:full-path=\"" + name + "\" manifest:media-type=\"image/png\"/>\n";
    manifest += "</manifest:manifest>\n";

    int err = 0;
    zip_t* za = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) { if (why) *why = "could not create " + path; return false; }

    // The buffers above outlive zip_close (this frame's locals, and the art the
    // caller holds), which is what libzip needs: it reads them at close.
    auto add = [&](const char* name, const std::string& data, bool store) {
        zip_source_t* src = zip_source_buffer(za, data.data(), data.size(), 0);
        if (!src) return false;
        const zip_int64_t idx = zip_file_add(za, name, src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (idx < 0) { zip_source_free(src); return false; }
        if (store) zip_set_file_compression(za, zip_uint64_t(idx), ZIP_CM_STORE, 0);
        return true;
    };
    bool ok = add("mimetype", mimetype, true) &&
              add("META-INF/manifest.xml", manifest, false) &&
              add("styles.xml", styles, false) &&
              add("meta.xml", meta, false) &&
              add("content.xml", content, false);
    for (const auto& [name, art] : pictures)   // a PNG is compressed already
        ok = ok && add(name.c_str(), art->png, true);
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
