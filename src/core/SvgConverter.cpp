#include "core/SvgConverter.hpp"

#include <cstring>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>

// ── nanosvg ──────────────────────────────────────────────────
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// ── stb for PNG encoding and image decoding ──────────────────
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

// ── stb_truetype for text rendering ──────────────────────────
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace sc {

// ─────────────────────────────────────────────────────────────
// Base64 decoder
// ─────────────────────────────────────────────────────────────
static const int b64table[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
};

static std::vector<uint8_t> DecodeBase64(std::string_view input)
{
    std::vector<uint8_t> out;
    out.reserve(input.size() * 3 / 4);

    int val = 0, bits = -8;
    for (char ch : input) {
        auto c = static_cast<unsigned char>(ch);
        if (c >= 128 || b64table[c] == -1) continue;
        val = (val << 6) | b64table[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// Try to extract an embedded raster image from SVG
// Scratch often wraps bitmap art in <image xlink:href="data:image/png;base64,..."/>
// ─────────────────────────────────────────────────────────────
static RasterResult TryExtractEmbeddedImage(const uint8_t* svgData, size_t svgSize)
{
    RasterResult result;
    std::string_view sv(reinterpret_cast<const char*>(svgData), svgSize);

    // Look for data:image URI in href attribute
    constexpr std::string_view markers[] = {
        "data:image/png;base64,",
        "data:image/jpeg;base64,",
        "data:image/jpg;base64,",
    };

    for (auto& marker : markers) {
        auto pos = sv.find(marker);
        if (pos == std::string_view::npos) continue;

        auto b64Start = pos + marker.size();
        // Find the end of the base64 string (terminated by " or ')
        auto b64End = sv.find_first_of("\"'", b64Start);
        if (b64End == std::string_view::npos) continue;

        auto b64 = sv.substr(b64Start, b64End - b64Start);
        auto imageBytes = DecodeBase64(b64);
        if (imageBytes.empty()) continue;

        // Decode the image with stb_image
        int w = 0, h = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(
            imageBytes.data(), static_cast<int>(imageBytes.size()),
            &w, &h, &channels, 4);  // force RGBA

        if (pixels && w > 0 && h > 0) {
            result.width  = w;
            result.height = h;
            result.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
            stbi_image_free(pixels);
            return result;
        }
        if (pixels) stbi_image_free(pixels);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
// SVG <text> element parsing and rendering
// ─────────────────────────────────────────────────────────────

// Load a system sans-serif font for text rendering
static std::vector<uint8_t> LoadSystemFont()
{
    // Try common sans-serif font paths
    const char* paths[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\calibri.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNSText.ttf",
#endif
    };
    for (auto* p : paths) {
        std::ifstream f(p, std::ios::binary);
        if (f) {
            f.seekg(0, std::ios::end);
            auto sz = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(data.data()), sz);
            return data;
        }
    }
    return {};
}

static const std::vector<uint8_t>& GetFontData()
{
    static std::vector<uint8_t> data = LoadSystemFont();
    return data;
}

struct SvgTextSpan {
    std::string text;
    double dy = 0; // vertical offset in px (before scaling)
};

struct SvgTextElement {
    double tx = 0, ty = 0;   // translate
    double sx = 1, sy = 1;   // scale
    double fontSize = 40;
    uint8_t r = 255, g = 255, b = 255, a = 255;
    double strokeWidth = 0;
    uint8_t sr = 0, sg = 0, sb = 0; // stroke color
    std::vector<SvgTextSpan> spans;
};

// Parse hex color like #rrggbb or #rgb
static void ParseHexColor(const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (hex.size() == 7 && hex[0] == '#') {
        r = static_cast<uint8_t>(std::stoul(hex.substr(1, 2), nullptr, 16));
        g = static_cast<uint8_t>(std::stoul(hex.substr(3, 2), nullptr, 16));
        b = static_cast<uint8_t>(std::stoul(hex.substr(5, 2), nullptr, 16));
    } else if (hex.size() == 4 && hex[0] == '#') {
        r = static_cast<uint8_t>(std::stoul(hex.substr(1, 1), nullptr, 16) * 17);
        g = static_cast<uint8_t>(std::stoul(hex.substr(2, 1), nullptr, 16) * 17);
        b = static_cast<uint8_t>(std::stoul(hex.substr(3, 1), nullptr, 16) * 17);
    }
}

// Extract attribute value from a tag string: attr="value"
static std::string GetAttr(const std::string& tag, const std::string& attr)
{
    auto key = attr + "=\"";
    auto pos = tag.find(key);
    if (pos == std::string::npos) return "";
    auto start = pos + key.size();
    auto end = tag.find('"', start);
    if (end == std::string::npos) return "";
    return tag.substr(start, end - start);
}

// Parse transform="translate(x,y) scale(sx,sy)" or similar
static void ParseTransform(const std::string& transform,
                           double& tx, double& ty, double& sx, double& sy)
{
    // translate
    std::regex trRe(R"(translate\(\s*([0-9eE.+-]+)[,\s]+([0-9eE.+-]+)\s*\))");
    std::smatch m;
    if (std::regex_search(transform, m, trRe)) {
        tx = std::stod(m[1]);
        ty = std::stod(m[2]);
    }
    // scale - handle both scale(s) and scale(sx,sy)
    std::regex scRe(R"(scale\(\s*([0-9eE.+-]+)(?:[,\s]+([0-9eE.+-]+))?\s*\))");
    if (std::regex_search(transform, m, scRe)) {
        sx = std::stod(m[1]);
        sy = m[2].matched ? std::stod(m[2]) : sx;
    }
}

// Extract text content from between tags, stripping child tags
static std::string ExtractTextContent(const std::string& s)
{
    std::string result;
    bool inTag = false;
    for (char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) result += c;
    }
    return result;
}

// Parse all <text> elements from SVG XML
static std::vector<SvgTextElement> ParseSvgTextElements(const char* svgStr, size_t svgLen)
{
    std::vector<SvgTextElement> result;
    std::string_view sv(svgStr, svgLen);

    // Accumulate group transforms: scan all <g transform="..."> tags
    // Scratch SVGs typically have: <g transform="translate(-X,-Y)"> wrapping everything
    double groupTx = 0, groupTy = 0;
    {
        size_t gpos = 0;
        while (gpos < sv.size()) {
            auto gStart = sv.find("<g ", gpos);
            if (gStart == std::string_view::npos) break;
            auto gEnd = sv.find('>', gStart);
            if (gEnd == std::string_view::npos) break;
            std::string gTag(sv.substr(gStart, gEnd - gStart + 1));
            auto gTransform = GetAttr(gTag, "transform");
            if (!gTransform.empty()) {
                double tx = 0, ty = 0, sx = 1, sy = 1;
                ParseTransform(gTransform, tx, ty, sx, sy);
                groupTx += tx;
                groupTy += ty;
            }
            gpos = gEnd + 1;
        }
    }

    size_t pos = 0;
    while (pos < sv.size()) {
        auto textStart = sv.find("<text ", pos);
        if (textStart == std::string_view::npos) break;

        // Find the closing </text>
        auto textEnd = sv.find("</text>", textStart);
        if (textEnd == std::string_view::npos) break;
        textEnd += 7; // include </text>

        std::string textBlock(sv.substr(textStart, textEnd - textStart));
        pos = textEnd;

        // Get the opening <text ...> tag
        auto tagEnd = textBlock.find('>');
        if (tagEnd == std::string::npos) continue;
        std::string tag = textBlock.substr(0, tagEnd + 1);

        SvgTextElement elem;

        // Parse text's own transform and add group transforms
        auto transform = GetAttr(tag, "transform");
        if (!transform.empty())
            ParseTransform(transform, elem.tx, elem.ty, elem.sx, elem.sy);
        elem.tx += groupTx;
        elem.ty += groupTy;

        // Parse fill color
        auto fill = GetAttr(tag, "fill");
        if (!fill.empty() && fill != "none")
            ParseHexColor(fill, elem.r, elem.g, elem.b);

        // Parse stroke
        auto stroke = GetAttr(tag, "stroke");
        auto strokeW = GetAttr(tag, "stroke-width");
        if (!stroke.empty() && stroke != "none" && !strokeW.empty()) {
            ParseHexColor(stroke, elem.sr, elem.sg, elem.sb);
            elem.strokeWidth = std::stod(strokeW);
        }

        // Parse font-size
        auto fs = GetAttr(tag, "font-size");
        if (!fs.empty()) elem.fontSize = std::stod(fs);

        // Parse opacity
        auto opacity = GetAttr(tag, "opacity");
        if (!opacity.empty()) elem.a = static_cast<uint8_t>(std::stod(opacity) * 255);

        // Parse tspan elements
        std::string body = textBlock.substr(tagEnd + 1);
        size_t tpos = 0;
        while (tpos < body.size()) {
            auto tsStart = body.find("<tspan", tpos);
            if (tsStart == std::string::npos) {
                // No more tspans — try getting raw text content
                auto raw = ExtractTextContent(body.substr(tpos));
                if (!raw.empty()) {
                    SvgTextSpan span;
                    span.text = raw;
                    elem.spans.push_back(std::move(span));
                }
                break;
            }
            auto tsTagEnd = body.find('>', tsStart);
            if (tsTagEnd == std::string::npos) break;
            auto tsClose = body.find("</tspan>", tsTagEnd);
            if (tsClose == std::string::npos) break;

            std::string tsTag = body.substr(tsStart, tsTagEnd - tsStart + 1);
            std::string content = body.substr(tsTagEnd + 1, tsClose - tsTagEnd - 1);

            SvgTextSpan span;
            span.text = ExtractTextContent(content);

            auto dyStr = GetAttr(tsTag, "dy");
            if (!dyStr.empty()) {
                // Remove "px" suffix if present
                if (dyStr.size() > 2 && dyStr.substr(dyStr.size() - 2) == "px")
                    dyStr = dyStr.substr(0, dyStr.size() - 2);
                span.dy = std::stod(dyStr);
            }

            elem.spans.push_back(std::move(span));
            tpos = tsClose + 8;
        }

        if (!elem.spans.empty())
            result.push_back(std::move(elem));
    }
    return result;
}

// Alpha-blend a pixel (premultiplied-aware)
static void BlendPixel(uint8_t* dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (sa == 0) return;
    int a = sa;
    int ia = 255 - a;
    dst[0] = static_cast<uint8_t>((sr * a + dst[0] * ia) / 255);
    dst[1] = static_cast<uint8_t>((sg * a + dst[1] * ia) / 255);
    dst[2] = static_cast<uint8_t>((sb * a + dst[2] * ia) / 255);
    dst[3] = static_cast<uint8_t>(std::min(255, a + (dst[3] * ia) / 255));
}

// Render parsed text elements onto an RGBA buffer
static void RenderTextElements(uint8_t* rgba, int imgW, int imgH, float rasterScale,
                               const std::vector<SvgTextElement>& texts)
{
    auto& fontData = GetFontData();
    if (fontData.empty()) return;

    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, fontData.data(),
                        stbtt_GetFontOffsetForIndex(fontData.data(), 0)))
        return;

    for (auto& elem : texts) {
        // Effective pixel size = fontSize * scaleY * rasterScale
        float pixelSize = static_cast<float>(elem.fontSize * std::abs(elem.sy) * rasterScale);
        if (pixelSize < 1.0f) continue;

        float stbScale = stbtt_ScaleForPixelHeight(&font, pixelSize);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        float ascentPx = ascent * stbScale;

        // Starting position in raster coordinates.
        float cursorX = static_cast<float>(elem.tx * rasterScale);
        float cursorY = static_cast<float>(elem.ty * rasterScale);

        for (auto& span : elem.spans) {
            cursorY += static_cast<float>(span.dy * std::abs(elem.sy) * rasterScale);

            float lineX = cursorX;
            float baselineY = cursorY;

            auto drawGlyph = [&](int codepoint, float x, float y,
                                  uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
                int gw, gh, xoff, yoff;
                auto* bitmap = stbtt_GetCodepointBitmap(&font, stbScale, stbScale,
                                                         codepoint, &gw, &gh, &xoff, &yoff);
                if (!bitmap) return;

                int destX = static_cast<int>(x) + xoff;
                int destY = static_cast<int>(y) + yoff;

                for (int py = 0; py < gh; ++py) {
                    int dy = destY + py;
                    if (dy < 0 || dy >= imgH) continue;
                    for (int px = 0; px < gw; ++px) {
                        int dx = destX + px;
                        if (dx < 0 || dx >= imgW) continue;
                        uint8_t coverage = bitmap[py * gw + px];
                        if (coverage == 0) continue;
                        uint8_t finalAlpha = static_cast<uint8_t>((coverage * ca) / 255);
                        BlendPixel(&rgba[(dy * imgW + dx) * 4], cr, cg, cb, finalAlpha);
                    }
                }
                stbtt_FreeBitmap(bitmap, nullptr);
            };

            for (size_t ci = 0; ci < span.text.size(); ) {
                // Simple UTF-8 decode for ASCII + basic latin
                int codepoint = static_cast<unsigned char>(span.text[ci]);
                int cpLen = 1;
                if ((codepoint & 0xE0) == 0xC0 && ci + 1 < span.text.size()) {
                    codepoint = ((codepoint & 0x1F) << 6) |
                                (static_cast<unsigned char>(span.text[ci+1]) & 0x3F);
                    cpLen = 2;
                } else if ((codepoint & 0xF0) == 0xE0 && ci + 2 < span.text.size()) {
                    codepoint = ((codepoint & 0x0F) << 12) |
                                ((static_cast<unsigned char>(span.text[ci+1]) & 0x3F) << 6) |
                                (static_cast<unsigned char>(span.text[ci+2]) & 0x3F);
                    cpLen = 3;
                }

                // Draw stroke first (if any) — simple approach: draw at offsets
                if (elem.strokeWidth > 0) {
                    float sw = static_cast<float>(elem.strokeWidth * std::abs(elem.sy) * rasterScale);
                    int iSw = std::max(1, static_cast<int>(std::ceil(sw)));
                    for (int oy = -iSw; oy <= iSw; ++oy) {
                        for (int ox = -iSw; ox <= iSw; ++ox) {
                            if (ox == 0 && oy == 0) continue;
                            if (ox * ox + oy * oy > iSw * iSw) continue;
                            drawGlyph(codepoint, lineX + ox, baselineY + oy,
                                       elem.sr, elem.sg, elem.sb, elem.a);
                        }
                    }
                }

                // Draw fill
                drawGlyph(codepoint, lineX, baselineY, elem.r, elem.g, elem.b, elem.a);

                // Advance cursor
                int advW, lsb;
                stbtt_GetCodepointHMetrics(&font, codepoint, &advW, &lsb);
                lineX += advW * stbScale;

                // Kerning
                if (ci + cpLen < span.text.size()) {
                    int next = static_cast<unsigned char>(span.text[ci + cpLen]);
                    lineX += stbtt_GetCodepointKernAdvance(&font, codepoint, next) * stbScale;
                }

                ci += cpLen;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Check if nanosvg rasterization produced any non-transparent pixels
// ─────────────────────────────────────────────────────────────
static bool HasVisiblePixels(const uint8_t* rgba, int w, int h)
{
    size_t total = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < total; ++i) {
        if (rgba[i * 4 + 3] > 0) return true;  // alpha > 0
    }
    return false;
}

RasterResult RasterizeSvg(const uint8_t* svgData, size_t svgSize, float scale)
{
    RasterResult result;

    // 1. Try extracting embedded raster image first
    result = TryExtractEmbeddedImage(svgData, svgSize);
    if (!result.pixels.empty())
        return result;

    // 2. Fall back to nanosvg vector rasterization
    auto textElems = ParseSvgTextElements(reinterpret_cast<const char*>(svgData), svgSize);

    std::string svgStr(reinterpret_cast<const char*>(svgData), svgSize);

    NSVGimage* image = nsvgParse(svgStr.data(), "px", 96.0f);
    if (!image) return result;

    int w = static_cast<int>(std::ceil(image->width  * scale));
    int h = static_cast<int>(std::ceil(image->height * scale));

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        nsvgDelete(image);
        return result;
    }

    result.pixels.resize(static_cast<size_t>(w) * h * 4, 0);
    result.width  = w;
    result.height = h;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        result.pixels.clear();
        return result;
    }

    nsvgRasterize(rast, image, 0, 0, scale, result.pixels.data(), w, h, w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    // 3. Render <text> elements that nanosvg ignores
    if (!textElems.empty()) {
        RenderTextElements(result.pixels.data(), w, h, scale, textElems);
    }

    // If everything is still blank, clear it
    if (!HasVisiblePixels(result.pixels.data(), w, h)) {
        result.pixels.clear();
        result.width = result.height = 0;
    }

    return result;
}

// Callback for stbi_write_png_to_func
static void PngWriteCallback(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> EncodePng(const uint8_t* rgba, int w, int h)
{
    std::vector<uint8_t> out;
    stbi_write_png_to_func(PngWriteCallback, &out, w, h, 4, rgba, w * 4);
    return out;
}

} // namespace sc
