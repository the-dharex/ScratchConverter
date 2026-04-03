#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sc {

/// Rasterize an SVG (in-memory data) to RGBA pixels.
/// Returns an empty vector on failure.
/// outWidth / outHeight are filled with the resulting dimensions.
struct RasterResult {
    std::vector<uint8_t> pixels;   // RGBA, row-major
    int width  = 0;
    int height = 0;
};

RasterResult RasterizeSvg(const uint8_t* svgData, size_t svgSize,
                           float scale = 2.0f);

/// Encode RGBA pixels to a PNG file in memory.
std::vector<uint8_t> EncodePng(const uint8_t* rgba, int w, int h);

} // namespace sc
