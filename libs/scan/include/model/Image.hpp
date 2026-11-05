#pragma once
#include "model/Spread.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Image (s22 -- the decode seam, and the ONLY place Leptonica appears).
//
// spread:: asks its question of a greyscale plane and split:: answers in
// rectangles; neither needs to know what a PNG is. That leaves exactly two jobs
// for an image library, and they are the whole of this file:
//
//   load_grey   -- get the pixels into a plane the engine can read
//   write_crop  -- put a rectangle of them back on disk
//
// Keeping the seam this narrow is what let phases 1 and 2 be proven with no
// image library at all. It also concentrates the code that CANNOT be proven in
// the sandbox into one small file with two functions -- so what rides on
// Scott's crc is a decode, not a decision.
//
// The plane is TIGHTLY PACKED, deliberately. Leptonica's 8bpp rows are 32-bit
// word aligned and byte order within a word is not what a naive reader expects,
// so the pixels are copied out through GET_DATA_BYTE rather than the raster
// being handed over as-is. That costs one pass over the image and removes an
// entire class of "every fourth column is wrong" bug that would look exactly
// like a bad scan.
//
// The bedrock rule holds here: the engine always reads black-on-white. Nothing
// in this file inverts, thresholds, or enhances. A scan that arrives inverted
// is Condition's problem, in phase 0, where the author can see it.
namespace optik::model::image {

// An owning greyscale plane. 0 is black, 255 is white, one byte per pixel, no
// row padding.
struct GreyImage {
    std::vector<std::uint8_t> px;
    int width  = 0;
    int height = 0;

    bool valid() const { return width > 0 && height > 0 && px.size() == std::size_t(width) * height; }

    spread::Grey view() const {
        return spread::Grey{px.data(), width, height, width};
    }
};

// Decode any format Leptonica reads, convert to 8bpp grey, drop any colormap.
// Returns false on a file that will not open or decode -- which is a fact about
// that file, not a reason to stop, so a caller walking a folder should record
// it and continue.
bool load_grey(const std::string& path, GreyImage& out);

// Write the rectangle [x0,x1) x [y0,y1) of `src` to `out` as a PNG.
//
// Re-reads the source rather than taking a decoded image, because the only
// caller today is the diagnostic dump and clarity beats a second read. When the
// real crop path lands it will want a Pix-based overload that clips an
// already-open image; that is an optimisation to make when there is a caller
// that feels it, not before.
bool write_crop(const std::string& src, int x0, int y0, int x1, int y1,
                const std::string& out);

// An owning RGB plane, 3 bytes per pixel, no row padding. Exists ONLY so a
// diagnostic can draw on a page in a colour that cannot be mistaken for ink.
//
// The engine has no use for colour and must not grow one -- the bedrock rule is
// that it always reads black-on-white, and every measurement in the tree is
// taken off an 8-bit plane. What this serves is the other channel: an overlay
// drawn in mid-grey on a mid-grey scan is an overlay nobody can check, and an
// eyes channel whose output is ambiguous is not evidence.
struct RgbImage {
    std::vector<std::uint8_t> px;   // r,g,b interleaved
    int width  = 0;
    int height = 0;

    bool valid() const {
        return width > 0 && height > 0 && px.size() == std::size_t(width) * height * 3;
    }

    // Build from a greyscale view, so the page underneath is the page the
    // engine actually read -- not a re-decode that might differ.
    static RgbImage from(const spread::Grey& g);

    void set(int x, int y, std::uint8_t r, std::uint8_t g_, std::uint8_t b) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        std::uint8_t* p = px.data() + (std::size_t(y) * width + x) * 3;
        p[0] = r; p[1] = g_; p[2] = b;
    }
};

bool write_rgb(const RgbImage& img, const std::string& out);

}  // namespace optik::model::image
