#include "Ocr.hpp"
#include "Slant.hpp"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/publictypes.h>
#include <tesseract/resultiterator.h>

#include <memory>
#include <vector>

namespace optik::ocr {

// -- Layout.hpp's small functions live here: one library, one TU of plumbing --

Rect unite(const Rect& a, const Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return Rect{std::min(a.x0, b.x0), std::min(a.y0, b.y0),
                std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Text:    return "text";
        case Kind::Heading: return "heading";
        case Kind::Picture: return "picture";
        case Kind::Header:  return "header";
        case Kind::Footer:  return "footer";
        case Kind::Ignore:  return "ignore";
    }
    return "text";
}

bool kind_from(const std::string& s, Kind& out) {
    for (Kind k : {Kind::Text, Kind::Heading, Kind::Picture, Kind::Header,
                   Kind::Footer, Kind::Ignore})
        if (s == kind_name(k)) { out = k; return true; }
    return false;
}

bool kind_is_text(Kind k) { return k == Kind::Text || k == Kind::Heading; }

unsigned fmt_of(const Word& w) {
    return (w.italic ? kItalic : 0u) | (w.bold ? kBold : 0u) | (w.underline ? kUnderline : 0u) |
           (w.small_caps ? kSmallCaps : 0u) | (w.superscript ? kSuperscript : 0u);
}

void set_fmt(Word& w, unsigned bits, bool on) {
    if (bits & kItalic)      w.italic = on;
    if (bits & kBold)        w.bold = on;
    if (bits & kUnderline)   w.underline = on;
    if (bits & kSmallCaps)   w.small_caps = on;
    if (bits & kSuperscript) w.superscript = on;
}

std::string para_text(const Para& p) {
    std::string s;
    for (std::size_t l = 0; l < p.lines.size(); ++l) {
        if (l) s += '\n';
        bool first = true;
        for (const Word& w : p.lines[l].words) {
            if (w.text.empty()) continue;
            if (!first) s += ' ';
            s += w.text;
            first = false;
        }
    }
    return s;
}

// -- The engine ---------------------------------------------------------------

namespace {

struct PixDel { void operator()(Pix* p) const { pixDestroy(&p); } };
using PixPtr = std::unique_ptr<Pix, PixDel>;

// The page as the look says (Condition.hpp). Tesseract reads it as its own
// image, so every box it reports is already in page space.
PixPtr load_look(const std::string& path, const Look& look, std::string* why, double* measured) {
    if (!measured) return PixPtr(load_page(path, look, why));
    PixPtr cut(cut_page(path, look.crop, look.turns, why));
    if (!cut) return nullptr;
    PixPtr toned(tone(cut.get(), look));
    *measured = find_skew(toned.get());
    return PixPtr(straighten(toned.get(), *measured));
}

// The page as 8-bit grey bytes, for the italic measure (Slant.hpp): the same
// pixels the engine read, so every word box lands on its own ink.
struct GreyPage {
    std::vector<unsigned char> bytes;
    Grey view;
};
bool grey_of(Pix* pix, GreyPage& out) {
    PixPtr g8(pixConvertTo8(pix, 0));
    if (!g8 || pixGetDepth(g8.get()) != 8) return false;
    const int w = int(pixGetWidth(g8.get())), h = int(pixGetHeight(g8.get()));
    const int wpl = pixGetWpl(g8.get());
    l_uint32* data = pixGetData(g8.get());
    out.bytes.resize(std::size_t(w) * std::size_t(h));
    for (int y = 0; y < h; ++y) {
        l_uint32* line = data + std::size_t(y) * std::size_t(wpl);
        unsigned char* row = out.bytes.data() + std::size_t(y) * std::size_t(w);
        for (int x = 0; x < w; ++x) row[x] = static_cast<unsigned char>(GET_DATA_BYTE(line, x));
    }
    out.view = Grey{out.bytes.data(), w, h, w};
    return true;
}

Kind kind_of(tesseract::PolyBlockType t) {
    using namespace tesseract;
    switch (t) {
        case PT_HEADING_TEXT:  return Kind::Heading;
        case PT_FLOWING_IMAGE:
        case PT_HEADING_IMAGE:
        case PT_PULLOUT_IMAGE: return Kind::Picture;
        case PT_HORZ_LINE:
        case PT_VERT_LINE:
        case PT_NOISE:         return Kind::Ignore;
        default:               return Kind::Text;
    }
}

Rect box_at(const tesseract::PageIterator* it, tesseract::PageIteratorLevel lvl) {
    int l = 0, t = 0, r = 0, b = 0;
    if (!it->BoundingBox(lvl, &l, &t, &r, &b)) return {};
    return Rect{l, t, r, b};
}

std::string text_at(tesseract::ResultIterator* it, tesseract::PageIteratorLevel lvl) {
    std::unique_ptr<char[]> s(it->GetUTF8Text(lvl));
    return s ? std::string(s.get()) : std::string();
}

// Walk the recognised words into paras / lines / words. `on_block` is told of
// each new block so the caller can decide where its paras go.
template <class OnBlock>
void walk_words(tesseract::ResultIterator* ri, std::vector<Para>* sink, OnBlock on_block) {
    using namespace tesseract;
    if (!ri) return;
    ri->Begin();
    do {
        if (ri->Empty(RIL_WORD)) continue;
        if (ri->IsAtBeginningOf(RIL_BLOCK)) sink = on_block(box_at(ri, RIL_BLOCK));
        if (!sink) continue;
        if (ri->IsAtBeginningOf(RIL_PARA) || sink->empty()) {
            sink->push_back(Para{});
            sink->back().box = box_at(ri, RIL_PARA);
        }
        Para& p = sink->back();
        if (ri->IsAtBeginningOf(RIL_TEXTLINE) || p.lines.empty()) {
            p.lines.push_back(Line{});
            p.lines.back().box = box_at(ri, RIL_TEXTLINE);
        }
        Word w;
        w.text = text_at(ri, RIL_WORD);
        w.box  = box_at(ri, RIL_WORD);
        w.conf = ri->Confidence(RIL_WORD);
        if (!w.text.empty()) p.lines.back().words.push_back(std::move(w));
    } while (ri->Next(RIL_WORD));
}

}  // namespace

struct Engine::Impl {
    tesseract::TessBaseAPI api;
    std::string            language;
    bool                   ready = false;
};

Engine::Engine() : m(std::make_unique<Impl>()) {}
Engine::~Engine() { if (m->ready) m->api.End(); }

bool Engine::ready() const { return m->ready; }
const std::string& Engine::language() const { return m->language; }

std::string Engine::version() { return tesseract::TessBaseAPI::Version(); }

bool Engine::init(const std::string& language, std::string* why) {
    if (m->ready) { m->api.End(); m->ready = false; }
    if (m->api.Init(nullptr, language.c_str(), tesseract::OEM_LSTM_ONLY) != 0) {
        if (why) *why = "Tesseract has no '" + language +
                        "' language data (install tesseract's language pack, or set TESSDATA_PREFIX)";
        return false;
    }
    // Scans often carry no resolution; without one Tesseract guesses and warns
    // on every page. Book scans are 300 dpi or near it.
    m->api.SetVariable("user_defined_dpi", "300");
    m->language = language;
    m->ready = true;
    return true;
}

bool Engine::read_page(const std::string& path, const Rect& crop,
                       std::vector<Area>& out, std::string* why) {
    return read_page(path, crop, 0, out, why);
}

bool Engine::read_area(const std::string& path, const Rect& crop, const Rect& area,
                       std::vector<Para>& out, std::string* why) {
    return read_area(path, crop, 0, area, out, why);
}

bool Engine::read_page(const std::string& path, const Rect& crop, int turns,
                       std::vector<Area>& out, std::string* why) {
    return read_page(path, Look{crop, turns}, out, why);
}

bool Engine::read_area(const std::string& path, const Rect& crop, int turns, const Rect& area,
                       std::vector<Para>& out, std::string* why) {
    return read_area(path, Look{crop, turns}, area, out, why);
}

bool Engine::read_page(const std::string& path, const Look& look, std::vector<Area>& out,
                       std::string* why, double* measured) {
    out.clear();
    if (!m->ready) { if (why) *why = "the engine has no language loaded"; return false; }
    PixPtr pix = load_look(path, look, why, measured);
    if (!pix) return false;

    auto& api = m->api;
    api.SetPageSegMode(tesseract::PSM_AUTO);
    api.SetImage(pix.get());
    if (api.Recognize(nullptr) != 0) {
        if (why) *why = "Tesseract could not read the page";
        api.Clear();
        return false;
    }

    // Pass 1: every block, text or not, in the engine's reading order.
    std::unique_ptr<tesseract::ResultIterator> ri(api.GetIterator());
    if (ri) {
        ri->Begin();
        do {
            Area a;
            a.kind = kind_of(ri->BlockType());
            a.box  = box_at(ri.get(), tesseract::RIL_BLOCK);
            if (!a.box.empty()) out.push_back(std::move(a));
        } while (ri->Next(tesseract::RIL_BLOCK));

        // Pass 2: the words, into the block they began in (matched by its box,
        // which is the block's identity across the two walks).
        walk_words(ri.get(), nullptr, [&](const Rect& b) -> std::vector<Para>* {
            for (Area& a : out)
                if (a.box == b) return &a.paras;
            return nullptr;
        });
    }
    api.Clear();

    // A text block the engine found no words in is an empty claim; a picture
    // block keeps its rect (it is what the area IS).
    std::vector<Area> kept;
    for (Area& a : out)
        if (!kind_is_text(a.kind) || !a.paras.empty()) kept.push_back(std::move(a));
    out = std::move(kept);

    // Italic, from the pixels (s27): the engine does not say.
    GreyPage g;
    if (grey_of(pix.get(), g)) measure_italic(g.view, out);
    return true;
}

bool Engine::read_area(const std::string& path, const Look& look, const Rect& area,
                       std::vector<Para>& out, std::string* why) {
    out.clear();
    if (!m->ready) { if (why) *why = "the engine has no language loaded"; return false; }
    if (area.empty()) { if (why) *why = "the area is empty"; return false; }
    PixPtr pix = load_look(path, look, why, nullptr);
    if (!pix) return false;

    auto& api = m->api;
    api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    api.SetImage(pix.get());
    api.SetRectangle(area.x0, area.y0, area.width(), area.height());
    if (api.Recognize(nullptr) != 0) {
        if (why) *why = "Tesseract could not read the area";
        api.Clear();
        return false;
    }
    std::unique_ptr<tesseract::ResultIterator> ri(api.GetIterator());
    walk_words(ri.get(), &out, [&](const Rect&) { return &out; });
    api.Clear();

    GreyPage g;
    if (grey_of(pix.get(), g)) measure_italic(g.view, out);
    return true;
}

}  // namespace optik::ocr
