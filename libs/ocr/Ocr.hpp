#pragma once
// ocr/Ocr.hpp -- the engine seam. The ONLY header that stands in front of
// Tesseract; Tesseract's own headers appear in Ocr.cpp and nowhere else.
//
// One Engine per thread: a TessBaseAPI is not shareable. The worker pool gives
// each worker its own (core/Jobs).
//
// FineReader's model, which is the reason this layer exists in this shape: the
// engine PROPOSES the page's areas and their text; the author adjusts what is
// wrong and re-reads just that area. So there are two verbs -- read a whole page
// (layout + text), and read one rectangle as one block.

#include "Layout.hpp"

#include <memory>
#include <string>
#include <vector>

namespace optik::ocr {

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Load a language ("eng", "eng+fra"). Data from TESSDATA_PREFIX or the
    // distro's default location. False with a reason a person can act on.
    bool init(const std::string& language, std::string* why = nullptr);
    bool ready() const;
    const std::string& language() const;

    // The whole page: layout analysis and recognition. `crop` is the page's
    // rectangle on the scan (empty = the whole image). Areas come back in
    // reading order, in page space.
    bool read_page(const std::string& image_path, const Rect& crop,
                   std::vector<Area>& out, std::string* why = nullptr);

    // One rectangle (page space) read as a single block of text -- the re-read
    // after an author moved or drew an area.
    bool read_area(const std::string& image_path, const Rect& crop, const Rect& area,
                   std::vector<Para>& out, std::string* why = nullptr);

    static std::string version();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

}  // namespace optik::ocr
