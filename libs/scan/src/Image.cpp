#include "model/Image.hpp"

#include <leptonica/allheaders.h>

namespace optik::model::image {

bool load_grey(const std::string& path, GreyImage& out) {
    out = GreyImage{};

    PIX* src = pixRead(path.c_str());
    if (!src) return false;

    // cmapflag 0: remove any colormap rather than carrying one into the plane.
    PIX* grey = pixConvertTo8(src, 0);
    pixDestroy(&src);
    if (!grey) return false;

    const int w   = pixGetWidth(grey);
    const int h   = pixGetHeight(grey);
    const int wpl = pixGetWpl(grey);
    l_uint32* data = pixGetData(grey);

    if (w <= 0 || h <= 0 || !data) {
        pixDestroy(&grey);
        return false;
    }

    out.width  = w;
    out.height = h;
    out.px.resize(std::size_t(w) * std::size_t(h));

    // Copied out a byte at a time through the accessor macro. See the header:
    // handing over Leptonica's raster directly would read word-aligned padding
    // and shuffled byte order as image data, and the result looks like a bad
    // scan rather than like a bug.
    for (int y = 0; y < h; ++y) {
        l_uint32* line = data + std::size_t(y) * wpl;
        std::uint8_t* dst = out.px.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x)
            dst[x] = static_cast<std::uint8_t>(GET_DATA_BYTE(line, x));
    }

    pixDestroy(&grey);
    return true;
}

bool write_crop(const std::string& src, int x0, int y0, int x1, int y1,
                const std::string& out) {
    if (x1 <= x0 || y1 <= y0) return false;

    PIX* pix = pixRead(src.c_str());
    if (!pix) return false;

    BOX* box = boxCreate(x0, y0, x1 - x0, y1 - y0);
    if (!box) {
        pixDestroy(&pix);
        return false;
    }

    PIX* crop = pixClipRectangle(pix, box, nullptr);
    boxDestroy(&box);
    pixDestroy(&pix);
    if (!crop) return false;

    const l_int32 rc = pixWrite(out.c_str(), crop, IFF_PNG);
    pixDestroy(&crop);
    return rc == 0;
}

RgbImage RgbImage::from(const spread::Grey& g) {
    RgbImage out;
    if (!g.valid()) return out;
    out.width  = g.width;
    out.height = g.height;
    out.px.resize(std::size_t(g.width) * g.height * 3);
    for (int y = 0; y < g.height; ++y) {
        std::uint8_t* dst = out.px.data() + std::size_t(y) * g.width * 3;
        for (int x = 0; x < g.width; ++x) {
            const std::uint8_t v = g.at(x, y);
            dst[x * 3 + 0] = v; dst[x * 3 + 1] = v; dst[x * 3 + 2] = v;
        }
    }
    return out;
}

bool write_rgb(const RgbImage& img, const std::string& out) {
    if (!img.valid()) return false;

    PIX* pix = pixCreate(img.width, img.height, 32);
    if (!pix) return false;

    const int wpl = pixGetWpl(pix);
    l_uint32* data = pixGetData(pix);
    for (int y = 0; y < img.height; ++y) {
        l_uint32* line = data + std::size_t(y) * wpl;
        const std::uint8_t* src = img.px.data() + std::size_t(y) * img.width * 3;
        for (int x = 0; x < img.width; ++x)
            composeRGBPixel(src[x * 3], src[x * 3 + 1], src[x * 3 + 2], line + x);
    }

    const l_int32 rc = pixWrite(out.c_str(), pix, IFF_PNG);
    pixDestroy(&pix);
    return rc == 0;
}

}  // namespace optik::model::image
