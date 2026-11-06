#include "Picture.hpp"

#include <leptonica/allheaders.h>

#include <algorithm>

namespace optik::core {

bool load_picture(const std::string& path, const ocr::Rect& crop, int max_side, Picture& out) {
    return load_picture(path, ocr::Look{crop, 0}, max_side, out);
}

bool load_picture(const std::string& path, const ocr::Rect& crop, int turns, int max_side, Picture& out) {
    return load_picture(path, ocr::Look{crop, turns}, max_side, out);
}

bool load_picture(const std::string& path, const ocr::Look& look, int max_side, Picture& out) {
    out = Picture{};
    Pix* part = ocr::cut_page(path, look.crop, look.turns);
    if (!part) return false;
    const int w = int(pixGetWidth(part)), h = int(pixGetHeight(part));
    double s = 1.0;
    if (max_side > 0 && std::max(w, h) > max_side) s = double(max_side) / std::max(w, h);

    // Greyscale-aware scaling (area-averaged when reducing) reads better than
    // nearest-neighbour on type; then the look's tone and straightening (s29),
    // then to 32 bpp for the copy out.
    Pix* scaled = s < 1.0 ? pixScaleSmooth(part, float(s), float(s)) : pixClone(part);
    if (!scaled) scaled = pixScale(part, float(s), float(s));
    Pix* toned = scaled ? ocr::tone(scaled, look) : nullptr;
    Pix* straight = toned ? ocr::straighten(toned, look.skew) : nullptr;
    Pix* rgb = straight ? pixConvertTo32(straight) : nullptr;
    bool ok = false;
    if (rgb) {
        out.width  = int(pixGetWidth(rgb));
        out.height = int(pixGetHeight(rgb));
        out.scale  = w > 0 ? double(out.width) / w : 1.0;
        out.src_w  = w;
        out.src_h  = h;
        out.px.resize(std::size_t(out.width) * out.height);
        const l_uint32* data = pixGetData(rgb);
        const int wpl = pixGetWpl(rgb);
        for (int y = 0; y < out.height; ++y) {
            const l_uint32* line = data + std::size_t(y) * wpl;
            std::uint32_t* dst = out.px.data() + std::size_t(y) * out.width;
            for (int x = 0; x < out.width; ++x) {
                l_int32 r, g, b;
                extractRGBValues(line[x], &r, &g, &b);
                dst[x] = 0xFF000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
            }
        }
        ok = out.valid();
        pixDestroy(&rgb);
    }
    if (straight) pixDestroy(&straight);
    if (toned) pixDestroy(&toned);
    if (scaled) pixDestroy(&scaled);
    pixDestroy(&part);
    return ok;
}

}  // namespace optik::core
