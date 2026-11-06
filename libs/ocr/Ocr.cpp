#include "Ocr.hpp"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/publictypes.h>
#include <tesseract/resultiterator.h>

#include <memory>

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

// Load the scan and cut the page out of it. Tesseract reads the crop as its own
// image, so every box it reports is already in page space.
PixPtr load_crop(const std::string& path, const Rect& crop, int turns, std::string* why) {
    PixPtr full(pixRead(path.c_str()));
    if (!full) { if (why) *why = "could not read " + path; return nullptr; }
    PixPtr part;
    if (crop.empty()) {
        part = std::move(full);
    } else {
        Box* b = boxCreate(crop.x0, crop.y0, crop.width(), crop.height());
        part.reset(pixClipRectangle(full.get(), b, nullptr));
        boxDestroy(&b);
        if (!part) { if (why) *why = "the page's rectangle is outside its scan"; return nullptr; }
    }
    turns &= 3;
    if (turns == 0) return part;
    PixPtr turned(pixRotateOrth(part.get(), turns));
    if (!turned) { if (why) *why = "could not turn the page"; return nullptr; }
    return turned;
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
    out.clear();
    if (!m->ready) { if (why) *why = "the engine has no language loaded"; return false; }
    PixPtr pix = load_crop(path, crop, turns, why);
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
    return true;
}

bool Engine::read_area(const std::string& path, const Rect& crop, int turns, const Rect& area,
                       std::vector<Para>& out, std::string* why) {
    out.clear();
    if (!m->ready) { if (why) *why = "the engine has no language loaded"; return false; }
    if (area.empty()) { if (why) *why = "the area is empty"; return false; }
    PixPtr pix = load_crop(path, crop, turns, why);
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
    return true;
}

}  // namespace optik::ocr
