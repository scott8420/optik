#include "Condition.hpp"

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cmath>

namespace optik::ocr {

Pix* cut_page(const std::string& path, const Rect& crop, int turns, std::string* why) {
    Pix* full = pixRead(path.c_str());
    if (!full) { if (why) *why = "could not read " + path; return nullptr; }
    Pix* part = full;
    if (!crop.empty()) {
        Box* b = boxCreate(crop.x0, crop.y0, crop.width(), crop.height());
        part = pixClipRectangle(full, b, nullptr);
        boxDestroy(&b);
        pixDestroy(&full);
        if (!part) { if (why) *why = "the page's rectangle is outside its scan"; return nullptr; }
    }
    turns &= 3;
    if (turns == 0) return part;
    Pix* turned = pixRotateOrth(part, turns);
    pixDestroy(&part);
    if (!turned && why) *why = "could not turn the page";
    return turned;
}

namespace {

// A colormapped page is made plain first: the passes work on 8 or 32 bpp.
Pix* plain(Pix* p) {
    if (pixGetColormap(p)) return pixRemoveColormap(p, REMOVE_CMAP_BASED_ON_SRC);
    return pixClone(p);
}

// Level: the paper's background evened out (tiles, so a gutter's shadow or a
// pale corner comes up with the rest) to Leptonica's 200, then a linear stretch
// that takes 200 to white and the darkest ink to black. 8 or 32 bpp; anything
// else (1 bpp is already black and white) is left alone.
Pix* level_of(Pix* p) {
    const int d = int(pixGetDepth(p));
    if (d != 8 && d != 32) return pixClone(p);
    if (pixGetWidth(p) < 40 || pixGetHeight(p) < 40) return pixClone(p);
    Pix* norm = pixBackgroundNormSimple(p, nullptr, nullptr);
    if (!norm) return pixClone(p);
    // The ink: the darkest 1% of the page (on the luminance), never past 100 --
    // a page with no ink keeps its greys.
    int black = 0;
    if (Pix* g = d == 32 ? pixConvertRGBToLuminance(norm) : pixClone(norm)) {
        l_uint32 v = 0;
        if (pixGetRankValue(g, 2, 0.01f, &v) == 0)
            black = std::clamp(int(v), 0, 100);
        pixDestroy(&g);
    }
    Pix* out = pixGammaTRC(nullptr, norm, 1.0f, black, 200);
    pixDestroy(&norm);
    return out ? out : pixClone(p);
}

}  // namespace

Pix* tone(Pix* page, const Look& look) {
    Pix* p = plain(page);
    if (!p) return pixClone(page);
    if (look.decolour && pixGetDepth(p) > 8) {
        if (Pix* g = pixConvertTo8(p, 0)) { pixDestroy(&p); p = g; }
    }
    if (look.level) {
        Pix* l = level_of(p);
        pixDestroy(&p);
        p = l;
    }
    return p;
}

Pix* straighten(Pix* page, double degrees) {
    if (std::fabs(degrees) < kMinSkew) return pixClone(page);
    const float rad = float(degrees * M_PI / 180.0);
    Pix* r = pixRotate(page, rad, L_ROTATE_AREA_MAP, L_BRING_IN_WHITE, 0, 0);
    return r ? r : pixClone(page);
}

double find_skew(Pix* page) {
    // Black and white for the measure: grey, the background evened, Otsu's
    // threshold over tiles (a shadowed gutter does not turn to solid ink).
    Pix* g = pixConvertTo8(page, 0);
    if (!g) return 0.0;
    Pix* bw = nullptr;
    if (pixGetWidth(g) >= 64 && pixGetHeight(g) >= 64)
        bw = pixOtsuThreshOnBackgroundNorm(g, nullptr, 10, 15, 100, 50, 255, 2, 2, 0.1f, nullptr);
    if (!bw) bw = pixConvertTo1(g, 128);
    pixDestroy(&g);
    if (!bw) return 0.0;
    l_float32 angle = 0, conf = 0;
    const l_int32 bad = pixFindSkewSweepAndSearch(bw, &angle, &conf, 4, 2, float(kMaxSkew), 1.0f, 0.01f);
    pixDestroy(&bw);
    // Leptonica's own floor for trusting the measure (pixDeskew): 3.0.
    if (bad || conf < 3.0f || std::fabs(angle) < kMinSkew || std::fabs(angle) > kMaxSkew) return 0.0;
    return std::round(double(angle) * 100.0) / 100.0;
}

Pix* load_page(const std::string& path, const Look& look, std::string* why) {
    Pix* cut = cut_page(path, look.crop, look.turns, why);
    if (!cut) return nullptr;
    Pix* toned = tone(cut, look);
    pixDestroy(&cut);
    Pix* out = straighten(toned, look.skew);
    pixDestroy(&toned);
    return out;
}

}  // namespace optik::ocr
