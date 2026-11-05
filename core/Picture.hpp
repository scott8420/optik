#pragma once
// core/Picture.hpp -- a page's pixels for the screen, decoded by the SAME
// library that feeds the engine (Leptonica), so what the author sees is what
// was read. Thread-safe; the app calls it from a worker. GTK-free: the pixels
// are Cairo's ARGB32 layout (native-endian 0xAARRGGBB) in a plain vector.
#include "ocr/Layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace optik::core {

struct Picture {
    int width = 0, height = 0;            // after scaling
    double scale = 1.0;                   // picture px per page px
    std::vector<std::uint32_t> px;        // width * height, ARGB32
    bool valid() const { return width > 0 && height > 0; }
};

// Decode `path`, cut `crop` (empty = whole), scale so the longer side is at most
// `max_side` (0 = no scaling; never enlarged).
bool load_picture(const std::string& path, const ocr::Rect& crop, int max_side, Picture& out);

}  // namespace optik::core
