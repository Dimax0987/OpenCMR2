// Original UI artwork traced to Bezier contours at load time. Colour masks
// and silhouettes get separate distance fields; native-size draws still read
// the untouched original texture. No traced game assets are shipped.
#include "port/vector_sprite.h"
#include "render/gfx_internal.h"
#include "render/distance_field.h"
#include "platform/platform.h"
#include "port/sys.h"
#include "potracelib.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {
struct Cell {
    int x, y, w, h;
};
struct Image {
    int w, h;
    std::vector<uint32_t> pixels;
};
struct Built {
    int w, h;
    std::vector<uint8_t> pixels;
};
constexpr int kPad = 2, kOversample = 4;
constexpr float kSpread = 2.0f;

std::vector<Cell> Cells(const std::string &name, int w, int h) {
    if (name == "arrows4") {
        if (w == 256 && h == 128) {
            std::vector<Cell> cells;
            for (int i = 0; i < 8; i++)
                cells.push_back({(i % 4) * 64, (i / 4) * 64, 64, 64});
            return cells;
        }
        if (w == 512 && h == 256)
            return {{1, 0, 102, 102},   {103, 0, 102, 102},   {205, 0, 102, 102},   {307, 0, 102, 102},
                    {1, 102, 102, 102}, {103, 102, 102, 102}, {205, 102, 102, 102}, {307, 109, 107, 92}};
        return {};
    }
    if ((name == "ar_640a" || name == "ar_640d" || name == "arrow" || name == "tick" || name == "cross" ||
         name == "box" || name == "circle") &&
        w >= 8 && h >= 8 && w <= 64 && h <= 64)
        return {{0, 0, w, h}};
    return {};
}

bool Read(GfxTexture *texture, Image &image) {
    image.w = texture->width;
    image.h = texture->height;
    GfxTexture *copy = Gfx_CreateTexture(image.w, image.h, GFX_FORMAT_BGRA8, 1, GFX_TEXTURE_STAGING);
    if (!copy)
        return false;
    Gfx_CopyTexture(copy, texture);
    GfxLockedRect lock{};
    bool ok = Gfx_LockTexture(copy, 0, 0, &lock) != 0;
    if (ok) {
        image.pixels.resize((size_t)image.w * image.h);
        for (int y = 0; y < image.h; y++)
            memcpy(image.pixels.data() + y * image.w, (const uint8_t *)lock.pixels + y * lock.pitch,
                   image.w * 4);
        Gfx_UnlockTexture(copy, 0, 0);
    }
    Gfx_DestroyTexture(copy);
    return ok;
}

// Most frequent visible, non-black fill; the other colour is the original
// black outline. Monochrome symbols simply keep their original ink colour.
uint32_t FillColour(const Image &image, Cell c) {
    std::map<uint32_t, unsigned> colours;
    bool visible = false;
    for (int y = 0; y < c.h; y++)
        for (int x = 0; x < c.w; x++) {
            uint32_t p = image.pixels[(c.y + y) * image.w + c.x + x];
            if ((p >> 24) < 240)
                continue;
            visible = true;
            if (std::max({p & 255, (p >> 8) & 255, (p >> 16) & 255}) > 32)
                colours[p & 0xffffff]++;
        }
    auto best =
        std::max_element(colours.begin(), colours.end(), [](auto a, auto b) { return a.second < b.second; });
    return best != colours.end() ? best->first : visible ? 0 : 0xffffff;
}

float At(const std::vector<float> &mask, int w, int h, int x, int y) {
    return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : mask[y * w + x];
}
float Sample(const std::vector<float> &mask, int w, int h, float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float a = At(mask, w, h, ix, iy) * (1 - fx) + At(mask, w, h, ix + 1, iy) * fx;
    float b = At(mask, w, h, ix, iy + 1) * (1 - fx) + At(mask, w, h, ix + 1, iy + 1) * fx;
    return a * (1 - fy) + b * fy;
}

// Trace the original mask, preserving small dots,
// holes and sharp corners. Rasterise the resulting cubic paths only to build
// their distance field; display scaling follows the field, not bitmap texels.
std::vector<float> Trace(const std::vector<float> &mask, int w, int h, int up, bool curves) {
    const int traceScale = curves && w > 64 ? 4 : 1;
    int tw = w * traceScale, th = h * traceScale;
    const int bits = sizeof(potrace_word) * 8;
    int stride = (tw + bits - 1) / bits;
    std::vector<potrace_word> map((size_t)stride * th);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++)
            if (Sample(mask, w, h, (x + .5f) / traceScale - .5f, (y + .5f) / traceScale - .5f) >= .5f)
                map[(th - 1 - y) * stride + x / bits] |= (potrace_word)1 << (bits - 1 - x % bits);
    potrace_bitmap_t bitmap{tw, th, stride, map.data()};
    potrace_param_t *param = potrace_param_default();
    if (!param)
        return {};
    param->turdsize = 0;
    param->alphamax = curves ? 1.0 : 0.0;
    param->opttolerance = 0.025 * traceScale;
    potrace_state_t *state = potrace_trace(param, &bitmap);
    potrace_param_free(param);
    if (!state)
        return {};
    if (state->status != POTRACE_STATUS_OK) {
        potrace_state_free(state);
        return {};
    }
    std::vector<stbtt_vertex> vertices;
    auto vertex = [&](int type, potrace_dpoint_t p, potrace_dpoint_t a, potrace_dpoint_t b) {
        stbtt_vertex v{};
        v.type = (unsigned char)type;
        v.x = (short)lround(p.x * 64 / traceScale);
        v.y = (short)lround((th - p.y) * 64 / traceScale);
        v.cx = (short)lround(a.x * 64 / traceScale);
        v.cy = (short)lround((th - a.y) * 64 / traceScale);
        v.cx1 = (short)lround(b.x * 64 / traceScale);
        v.cy1 = (short)lround((th - b.y) * 64 / traceScale);
        vertices.push_back(v);
    };
    for (potrace_path_t *path = state->plist; path; path = path->next) {
        const auto &curve = path->curve;
        if (!curve.n)
            continue;
        vertex(STBTT_vmove, curve.c[curve.n - 1][2], {}, {});
        for (int i = 0; i < curve.n; i++) {
            if (curve.tag[i] == POTRACE_CORNER) {
                vertex(STBTT_vline, curve.c[i][1], {}, {});
                vertex(STBTT_vline, curve.c[i][2], {}, {});
            } else
                vertex(STBTT_vcubic, curve.c[i][2], curve.c[i][0], curve.c[i][1]);
        }
    }
    potrace_state_free(state);
    int rw = w * up, rh = h * up;
    std::vector<unsigned char> pixels((size_t)rw * rh);
    if (!vertices.empty()) {
        stbtt__bitmap raster{rw, rh, rw, pixels.data()};
        stbtt_Rasterize(&raster, 0.1f, vertices.data(), (int)vertices.size(), up / 64.0f, up / 64.0f, 0, 0, 0,
                        0, 0, nullptr);
    }
    std::vector<float> coverage(pixels.size());
    for (size_t i = 0; i < pixels.size(); i++)
        coverage[i] = pixels[i] / 255.0f;
    auto distance = DistanceField::FromCoverage(coverage, rw, rh);
    // Antialiasing contains subpixel stroke-width information. Match the
    // original painted area instead of rounding every partial pixel to a
    // whole pixel during tracing. The correction is bounded to 0.35 source
    // pixels and moves both sides of a stroke equally.
    double target = std::accumulate(mask.begin(), mask.end(), 0.0) * up * up;
    const float limit = 0.35f * up;
    size_t inside = 0;
    std::vector<float> edge;
    for (float d : distance) {
        if (d > limit + 1)
            inside++;
        else if (d >= -limit - 1)
            edge.push_back(d);
    }
    float lo = -limit, hi = limit;
    for (int iteration = 0; iteration < 14; iteration++) {
        float shift = (lo + hi) * .5f;
        double area = (double)inside;
        for (float d : edge)
            area += std::clamp(d + shift + .5f, 0.0f, 1.0f);
        if (area < target)
            lo = shift;
        else
            hi = shift;
    }
    float shift = (lo + hi) * .5f;
    for (float &d : distance)
        d += shift;
    return distance;
}

Built Build(const Image &image, const std::vector<Cell> &cells, bool curves) {
    int texels = cells.front().w >= 96 ? 2 : 4;
    int up = texels * kOversample;
    Built built{image.w * texels, image.h * texels * 2, {}};
    built.pixels.assign((size_t)built.w * built.h * 4, 0);
    for (Cell cell : cells) {
        int w = cell.w + 2 * kPad, h = cell.h + 2 * kPad;
        std::vector<float> alpha((size_t)w * h), fill(alpha.size());
        uint32_t colour = FillColour(image, cell);
        float base[] = {float(colour & 255), float((colour >> 8) & 255), float((colour >> 16) & 255)};
        float norm = base[0] * base[0] + base[1] * base[1] + base[2] * base[2];
        bool coloured = false;
        for (int y = 0; y < cell.h; y++)
            for (int x = 0; x < cell.w; x++) {
                uint32_t p = image.pixels[(cell.y + y) * image.w + cell.x + x];
                size_t i = (y + kPad) * w + x + kPad;
                alpha[i] = (p >> 24) / 255.0f;
                float dot = (p & 255) * base[0] + ((p >> 8) & 255) * base[1] + ((p >> 16) & 255) * base[2];
                fill[i] = norm > 0 ? std::clamp(dot / norm, 0.0f, 1.0f) * alpha[i] : alpha[i];
                coloured |= alpha[i] > .9f && fill[i] < .1f && norm > 0;
            }
        auto outer = Trace(alpha, w, h, up, curves);
        auto inner = coloured ? Trace(fill, w, h, up, curves) : std::vector<float>();
        if (outer.empty() || (coloured && inner.empty()))
            return {};
        for (int y = 0; y < cell.h * texels; y++)
            for (int x = 0; x < cell.w * texels; x++) {
                float distance[2]{};
                for (int sy = 0; sy < kOversample; sy++)
                    for (int sx = 0; sx < kOversample; sx++) {
                        size_t i = (size_t)(kPad * up + y * kOversample + sy) * (w * up) + kPad * up +
                                   x * kOversample + sx;
                        distance[0] += outer[i];
                        if (coloured)
                            distance[1] += inner[i];
                    }
                size_t i = (size_t)(cell.y * texels + y) * built.w + cell.x * texels + x;
                for (int channel = 0; channel < 2; channel++) {
                    float field = .5f + distance[channel] / (kOversample * kOversample * up * 2 * kSpread);
                    built.pixels[i * 4 + channel] = (uint8_t)lroundf(std::clamp(field, 0.0f, 1.0f) * 255);
                }
                // BGRA bytes: B = outer coverage field, G = fill field. R
                // marks whether the fill/black boundary is used.
                built.pixels[i * 4 + 2] = coloured ? 255 : 0;
                built.pixels[i * 4 + 3] = 255;
                size_t palette = i + (size_t)built.w * (built.h / 2);
                for (int channel = 0; channel < 3; channel++)
                    built.pixels[palette * 4 + channel] = (uint8_t)base[channel];
                built.pixels[palette * 4 + 3] = 255;
            }
    }
    return built;
}
} // namespace

extern "C" void VectorSprite_Replace(const char *name, GfxTexture *texture) {
    if (!name || !texture || texture->replacement || !Platform_GetSettingInt("video.vector_icons", 1))
        return;
    std::string base(name);
    base = base.substr(base.find_last_of("\\/") + 1);
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || (base.substr(dot) != ".tga" && base.substr(dot) != ".dds"))
        return;
    base.resize(dot);
    auto cells = Cells(base, texture->width, texture->height);
    if (cells.empty())
        return;
    Image image{};
    if (!Read(texture, image))
        return;
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t pixel : image.pixels) {
        hash ^= pixel;
        hash *= 1099511628211ull;
    }
    std::string key =
        base + "|" + std::to_string(image.w) + "x" + std::to_string(image.h) + "|" + std::to_string(hash);
    static std::map<std::string, Built> cache;
    auto found = cache.find(key);
    if (found == cache.end()) {
        Built built = Build(image, cells, base == "arrows4" || base == "circle");
        if (built.pixels.empty())
            return;
        size_t bytes = built.pixels.size();
        for (const auto &entry : cache)
            bytes += entry.second.pixels.size();
        if (bytes > 32 * 1024 * 1024)
            cache.clear();
        found = cache.emplace(key, std::move(built)).first;
        Sys_Log("icons: %s: traced %d original cells to Bezier outlines", base.c_str(), (int)cells.size());
    }
    const Built &built = found->second;
    GfxTexture *replacement =
        Gfx_CreateTexture(built.w, built.h, GFX_FORMAT_BGRA8, 1, GFX_TEXTURE_VECTOR_ICON);
    GfxLockedRect lock{};
    if (!replacement || !Gfx_LockTexture(replacement, 0, 0, &lock)) {
        Gfx_DestroyTexture(replacement);
        return;
    }
    for (int y = 0; y < built.h; y++)
        memcpy((uint8_t *)lock.pixels + (size_t)y * lock.pitch, built.pixels.data() + (size_t)y * built.w * 4,
               built.w * 4);
    Gfx_UnlockTexture(replacement, 0, 0);
    Gfx_SetTextureReplacement(texture, replacement);
}
