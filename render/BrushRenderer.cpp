/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        render/BrushRenderer.cpp
   Module:      Render
   Purpose:     Brush rendering, interpolation, and live tool effects.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../render/BrushRenderer.h"
#include "../core/Gradient.h"
#include "../core/BrushSystem.h"
#include "../core/BrushRuntime.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace
{
    static float clampf(float v, float a, float b)
    {
        return (v < a) ? a : (v > b ? b : v);
    }

    static SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t)
    {
        t = clampf(t, 0.0f, 1.0f);
        SDL_Color o{};
        o.r = (Uint8)std::lround(a.r * (1.0f - t) + b.r * t);
        o.g = (Uint8)std::lround(a.g * (1.0f - t) + b.g * t);
        o.b = (Uint8)std::lround(a.b * (1.0f - t) + b.b * t);
        o.a = (Uint8)std::lround(a.a * (1.0f - t) + b.a * t);
        return o;
    }

    static SDL_Color sampleStops4(const GradientConfig& g, float t)
    {
        t = clampf(t, 0.0f, 1.0f);

        int hi = 1;
        while (hi < STROVA_MAX_GRADIENT_STOPS && g.stopPos[hi] < t)
            ++hi;

        if (hi >= STROVA_MAX_GRADIENT_STOPS)
            return g.stopColor[STROVA_MAX_GRADIENT_STOPS - 1];

        int lo = hi - 1;
        float a = g.stopPos[lo];
        float b = g.stopPos[hi];
        float f = (b > a) ? (t - a) / (b - a) : 0.0f;
        return lerpColor(g.stopColor[lo], g.stopColor[hi], f);
    }

    struct CircleStamp
    {
        SDL_Texture* tex = nullptr;
        int diameter = 0;
    };

    struct MaskStampTex
    {
        SDL_Texture* tex = nullptr;
        int width = 0;
        int height = 0;
        std::uint64_t revision = 0;
    };

    struct ScratchPixelTex
    {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    static std::unordered_map<SDL_Renderer*, std::unordered_map<int, CircleStamp>> g_circleCache;
    static std::unordered_map<SDL_Renderer*, std::unordered_map<std::string, MaskStampTex>> g_maskCache;
    static std::unordered_map<SDL_Renderer*, std::unordered_map<std::uint64_t, ScratchPixelTex>> g_scratchPixelCache;

    static void destroyRendererCache(SDL_Renderer* r)
    {
        auto it = g_circleCache.find(r);
        if (it != g_circleCache.end())
        {
            for (auto& kv : it->second)
            {
                if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
            }
            g_circleCache.erase(it);
        }

        auto it2 = g_maskCache.find(r);
        if (it2 != g_maskCache.end())
        {
            for (auto& kv : it2->second)
            {
                if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
            }
            g_maskCache.erase(it2);
        }

        auto it3 = g_scratchPixelCache.find(r);
        if (it3 != g_scratchPixelCache.end())
        {
            for (auto& kv : it3->second)
            {
                if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
            }
            g_scratchPixelCache.erase(it3);
        }
    }

    static SDL_Texture* getCircleStamp(SDL_Renderer* r, int radius)
    {
        if (!r) return nullptr;
        radius = std::clamp(radius, 1, 512);

        auto& map = g_circleCache[r];
        auto it = map.find(radius);
        if (it != map.end() && it->second.tex)
            return it->second.tex;

        const int d = radius * 2;
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, d, d);
        if (!tex) return nullptr;

        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif

        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(tex, nullptr, &pixels, &pitch) != 0)
        {
            SDL_DestroyTexture(tex);
            return nullptr;
        }

        unsigned char* base = (unsigned char*)pixels;
        for (int y = 0; y < d; ++y)
        {
            unsigned char* p = base + y * pitch;
            float fy = (float)y + 0.5f - (float)radius;
            for (int x = 0; x < d; ++x)
            {
                float fx = (float)x + 0.5f - (float)radius;
                float dist = std::sqrt(fx * fx + fy * fy);
                float a = 1.0f - clampf((dist - ((float)radius - 1.0f)) / 1.35f, 0.0f, 1.0f);
                unsigned char A = (unsigned char)std::lround(a * 255.0f);
                unsigned char rgb = (A == 0) ? 0 : 255;
                p[x * 4 + 0] = rgb;
                p[x * 4 + 1] = rgb;
                p[x * 4 + 2] = rgb;
                p[x * 4 + 3] = A;
            }
        }

        SDL_UnlockTexture(tex);

        CircleStamp s;
        s.tex = tex;
        s.diameter = d;
        map[radius] = s;
        return tex;
    }

    static void drawStamp(SDL_Renderer* r, float cx, float cy, int radius, SDL_Color c, SDL_BlendMode blend = SDL_BLENDMODE_BLEND)
    {
        SDL_Texture* stamp = getCircleStamp(r, radius);
        if (!stamp)
        {
            SDL_SetRenderDrawBlendMode(r, blend);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderDrawPointF(r, cx, cy);
            return;
        }

        SDL_SetTextureBlendMode(stamp, blend);
        SDL_SetTextureColorMod(stamp, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(stamp, c.a);

        SDL_FRect dst;
        dst.x = cx - (float)radius;
        dst.y = cy - (float)radius;
        dst.w = (float)(radius * 2);
        dst.h = (float)(radius * 2);

        SDL_RenderCopyF(r, stamp, nullptr, &dst);
    }

    static void drawSoftStamp(SDL_Renderer* r, float cx, float cy, int radius, SDL_Color c, float hardness, float flow, SDL_BlendMode blend = SDL_BLENDMODE_BLEND)
    {
        hardness = clampf(hardness, 0.0f, 1.0f);
        flow = clampf(flow, 0.0f, 1.0f);
        int outer = std::max(1, radius);
        int core = std::max(1, (int)std::lround((float)outer * (0.12f + hardness * 0.88f)));

        SDL_Color body = c;
        body.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.18f + 0.42f * flow)), 0, 255);
        drawStamp(r, cx, cy, outer, body, blend);

        SDL_Color mid = c;
        mid.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.24f + 0.48f * flow)), 0, 255);
        drawStamp(r, cx, cy, std::max(core, (int)std::lround(outer * 0.72f)), mid, blend);

        SDL_Color center = c;
        center.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.30f + 0.70f * flow)), 0, 255);
        drawStamp(r, cx, cy, core, center, blend);
    }

    static SDL_BlendMode sdlBlendForBrush(strova::brush::BlendMode mode)
    {
        switch (mode)
        {
        case strova::brush::BlendMode::Additive: return SDL_BLENDMODE_ADD;
        case strova::brush::BlendMode::Screen: return SDL_BLENDMODE_ADD;
        case strova::brush::BlendMode::Erase: return SDL_BLENDMODE_BLEND;
        case strova::brush::BlendMode::Multiply: return SDL_BLENDMODE_MOD;
        case strova::brush::BlendMode::Overlay: return SDL_BLENDMODE_BLEND;
        case strova::brush::BlendMode::Normal:
        default: return SDL_BLENDMODE_BLEND;
        }
    }

    static SDL_Texture* getMaskStamp(SDL_Renderer* r, const strova::brush::BrushPackage& pkg)
    {
        if (!r || pkg.stamp.width <= 0 || pkg.stamp.height <= 0) return nullptr;
        const std::string key = pkg.manifest.id;
        auto& cache = g_maskCache[r];
        auto it = cache.find(key);
        if (it != cache.end() && it->second.tex && it->second.revision == pkg.stamp.revision)
            return it->second.tex;

        if (it != cache.end() && it->second.tex)
        {
            SDL_DestroyTexture(it->second.tex);
            cache.erase(it);
        }

        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pkg.stamp.width, pkg.stamp.height);
        if (!tex) return nullptr;

        std::vector<std::uint8_t> rgba((size_t)pkg.stamp.width * (size_t)pkg.stamp.height * 4ull, 255u);
        for (size_t i = 0, pxCount = (size_t)pkg.stamp.width * (size_t)pkg.stamp.height; i < pxCount; ++i)
        {
            const std::uint8_t m = !pkg.stamp.mask.empty() ? pkg.stamp.mask[i] : pkg.stamp.rgba[i * 4 + 3];
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = m;
        }

        SDL_UpdateTexture(tex, nullptr, rgba.data(), pkg.stamp.width * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(tex,
            strova::brush::generatorPreservesTextureScale(pkg.manifest.generator)
            ? SDL_ScaleModeNearest
            : SDL_ScaleModeLinear);
#endif
        cache[key] = MaskStampTex{ tex, pkg.stamp.width, pkg.stamp.height, pkg.stamp.revision };
        return tex;
    }

    static SDL_Color sampleStrokeGradient(const GradientConfig& g, float t)
    {
        if (!g.enabled || g.mode == 0)
            return g.stopColor[0];
        return sampleStops4(g, t);
    }

    static void drawMaskStamp(SDL_Renderer* r, SDL_Texture* stamp, int srcW, int srcH, float cx, float cy, float sizePx, SDL_Color c, float angleDeg, SDL_BlendMode blend)
    {
        if (!r || !stamp || sizePx <= 0.0f) return;
        SDL_SetTextureBlendMode(stamp, blend);
        SDL_SetTextureColorMod(stamp, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(stamp, c.a);

        const int tw = std::max(1, srcW);
        const int th = std::max(1, srcH);
        SDL_FRect dst{};
        dst.w = std::max(1.0f, sizePx);
        dst.h = std::max(1.0f, sizePx * ((float)th / (float)tw));
        dst.x = cx - dst.w * 0.5f;
        dst.y = cy - dst.h * 0.5f;
#if SDL_VERSION_ATLEAST(2,0,10)
        SDL_FPoint center{ dst.w * 0.5f, dst.h * 0.5f };
        SDL_RenderCopyExF(r, stamp, nullptr, &dst, angleDeg, &center, SDL_FLIP_NONE);
#else
        SDL_RenderCopyF(r, stamp, nullptr, &dst);
#endif
    }

    static float hashNoise(float v)
    {
        float s = std::sin(v * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    }

    static float signedNoise(float seed)
    {
        return hashNoise(seed) * 2.0f - 1.0f;
    }

    static std::vector<StrokePoint> makeRectOutline(const StrokePoint& a, const StrokePoint& b)
    {
        float x0 = std::min(a.x, b.x);
        float y0 = std::min(a.y, b.y);
        float x1 = std::max(a.x, b.x);
        float y1 = std::max(a.y, b.y);

        std::vector<StrokePoint> out;
        out.reserve(5);
        out.push_back({ x0, y0 });
        out.push_back({ x1, y0 });
        out.push_back({ x1, y1 });
        out.push_back({ x0, y1 });
        out.push_back({ x0, y0 });
        return out;
    }

    static std::vector<StrokePoint> makeEllipseOutline(const StrokePoint& a, const StrokePoint& b)
    {
        float x0 = std::min(a.x, b.x);
        float y0 = std::min(a.y, b.y);
        float x1 = std::max(a.x, b.x);
        float y1 = std::max(a.y, b.y);

        float cx = (x0 + x1) * 0.5f;
        float cy = (y0 + y1) * 0.5f;
        float rx = (x1 - x0) * 0.5f;
        float ry = (y1 - y0) * 0.5f;

        int seg = std::max(32, std::min(180, (int)(std::max(rx, ry) * 2.0f)));

        std::vector<StrokePoint> out;
        out.reserve(seg + 1);
        for (int i = 0; i <= seg; ++i)
        {
            float t = (float)i / (float)seg;
            float ang = t * 6.28318530718f;
            StrokePoint p;
            p.x = cx + std::cos(ang) * rx;
            p.y = cy + std::sin(ang) * ry;
            out.push_back(p);
        }
        return out;
    }

    static SDL_PixelFormat* rgbaFormat()
    {
        static SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
        return fmt;
    }

    static SDL_Texture* getScratchPixelTexture(SDL_Renderer* r, int w, int h)
    {
        if (!r || w <= 0 || h <= 0) return nullptr;
        const std::uint64_t key = ((std::uint64_t)(std::uint32_t)w << 32) | (std::uint32_t)h;
        auto& cache = g_scratchPixelCache[r];
        auto it = cache.find(key);
        if (it != cache.end() && it->second.tex)
            return it->second.tex;
        ScratchPixelTex entry{};
        entry.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!entry.tex) return nullptr;
        entry.w = w;
        entry.h = h;
        SDL_SetTextureBlendMode(entry.tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(entry.tex, SDL_ScaleModeNearest);
#endif
        cache[key] = entry;
        return entry.tex;
    }

    static bool readRegion(SDL_Renderer* r, int x, int y, int w, int h, std::vector<Uint32>& out)
    {
        out.clear();
        if (!r || w <= 0 || h <= 0) return false;

        SDL_Rect src{ x, y, w, h };
        out.resize((size_t)w * (size_t)h);
        if (SDL_RenderReadPixels(r, &src, SDL_PIXELFORMAT_RGBA32, out.data(), w * (int)sizeof(Uint32)) != 0)
        {
            out.clear();
            return false;
        }
        return true;
    }

    static SDL_Color readPixelColor(SDL_Renderer* r, int x, int y)
    {
        Uint32 px = 0;
        SDL_Rect rc{ x, y, 1, 1 };
        if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_RGBA32, &px, (int)sizeof(Uint32)) != 0)
            return SDL_Color{ 0, 0, 0, 0 };

        SDL_Color c{};
        SDL_PixelFormat* fmt = rgbaFormat();
        if (!fmt) return SDL_Color{ 0, 0, 0, 0 };
        SDL_GetRGBA(px, fmt, &c.r, &c.g, &c.b, &c.a);
        return c;
    }

    static void blurPixels(std::vector<Uint32>& px, int w, int h, int radius)
    {
        radius = std::clamp(radius, 1, 12);
        if (w <= 0 || h <= 0) return;

        SDL_PixelFormat* fmt = rgbaFormat();
        if (!fmt) return;

        std::vector<Uint32> src = px;
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                int r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int oy = -radius; oy <= radius; ++oy)
                {
                    int yy = std::clamp(y + oy, 0, h - 1);
                    for (int ox = -radius; ox <= radius; ++ox)
                    {
                        int xx = std::clamp(x + ox, 0, w - 1);
                        SDL_Color c{};
                        SDL_GetRGBA(src[(size_t)yy * (size_t)w + (size_t)xx], fmt, &c.r, &c.g, &c.b, &c.a);
                        r += c.r; g += c.g; b += c.b; a += c.a; ++n;
                    }
                }
                SDL_Color o{
                    (Uint8)(r / std::max(1, n)),
                    (Uint8)(g / std::max(1, n)),
                    (Uint8)(b / std::max(1, n)),
                    (Uint8)(a / std::max(1, n))
                };
                px[(size_t)y * (size_t)w + (size_t)x] = SDL_MapRGBA(fmt, o.r, o.g, o.b, o.a);
            }
        }

    }

    static void drawPixelTexture(SDL_Renderer* r, int x, int y, int w, int h, const std::vector<Uint32>& px, Uint8 alpha, SDL_BlendMode blend = SDL_BLENDMODE_BLEND)
    {
        if (!r || w <= 0 || h <= 0 || px.empty()) return;

        SDL_Texture* tex = getScratchPixelTexture(r, w, h);
        if (!tex) return;

        SDL_SetTextureBlendMode(tex, blend);
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_UpdateTexture(tex, nullptr, px.data(), w * (int)sizeof(Uint32));
        SDL_Rect dst{ x, y, w, h };
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
    }
}

BrushRenderer::BrushRenderer(SDL_Renderer* r)
    : renderer(r)
{
}

BrushRenderer::~BrushRenderer()
{
}

void BrushRenderer::purgeCache(SDL_Renderer* r)
{
    destroyRendererCache(r);
}

void BrushRenderer::drawStroke(const Stroke& s, float scale, float panX, float panY, int canvasX, int canvasY)
{
    drawStrokeWithPackage(s, nullptr, scale, panX, panY, canvasX, canvasY);
}

void BrushRenderer::drawStrokeWithPackage(const Stroke& s, const strova::brush::BrushPackage* overridePkg, float scale, float panX, float panY, int canvasX, int canvasY)
{
    if (!renderer || s.points.empty())
        return;

    auto toScreen = [&](float wx, float wy) -> SDL_FPoint
    {
        SDL_FPoint p;
        p.x = (float)canvasX + panX + wx * scale;
        p.y = (float)canvasY + panY + wy * scale;
        return p;
    };

    if (s.tool == ToolType::Fill)
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, s.color.r, s.color.g, s.color.b, s.color.a);
        const size_t n = s.points.size();
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            const StrokePoint& pa = s.points[i];
            const StrokePoint& pb = s.points[i + 1];
            SDL_FPoint a = toScreen(pa.x, pa.y);
            SDL_FPoint b = toScreen(pb.x, pb.y);
            if (std::fabs(pa.y - pb.y) < 0.0001f)
                SDL_RenderDrawLineF(renderer, a.x, a.y, b.x, a.y);
            else
            {
                float rx = std::min(a.x, b.x);
                float ry = std::min(a.y, b.y);
                float rw = std::max(1.0f, std::fabs(b.x - a.x));
                float rh = std::max(1.0f, std::fabs(b.y - a.y));
                SDL_FRect rct{ rx, ry, rw, rh };
                SDL_RenderFillRectF(renderer, &rct);
            }
        }
        return;
    }

    const std::vector<StrokePoint>* ptsPtr = &s.points;
    std::vector<StrokePoint> tmp;
    if (s.tool == ToolType::Rect && s.points.size() >= 2)
    {
        tmp = makeRectOutline(s.points[0], s.points[1]);
        ptsPtr = &tmp;
    }
    else if (s.tool == ToolType::Ellipse && s.points.size() >= 2)
    {
        tmp = makeEllipseOutline(s.points[0], s.points[1]);
        ptsPtr = &tmp;
    }
    const std::vector<StrokePoint>& pts = *ptsPtr;
    if (pts.empty()) return;

    if (s.tool == ToolType::Brush)
    {
        const strova::brush::BrushPackage* pkg = overridePkg;
        if (!pkg)
        {
            if (auto* mgr = strova::brush::globalManager())
            {
                pkg = mgr->findById(!s.brushId.empty() ? s.brushId : s.settings.brushId);
                if (!pkg) pkg = mgr->selected();
            }
        }
        if (pkg)
        {
            float totalLen = 0.0f;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                const float dx = pts[i].x - pts[i - 1].x;
                const float dy = pts[i].y - pts[i - 1].y;
                totalLen += SDL_sqrtf(dx * dx + dy * dy);
            }

            const strova::brush::CompiledBrushRuntime rt = strova::brush::compileBrushRuntime(pkg, s, scale, totalLen);
            SDL_Texture* stamp = rt.valid() ? getMaskStamp(renderer, *pkg) : nullptr;
            if (stamp)
            {
                if (pts.size() == 1)
                {
                    SDL_FPoint p0 = toScreen(pts[0].x, pts[0].y);
                    SDL_Color c = strova::brush::compiledColorAt(rt, s, 0.0f, pts[0].pressure, 0.0f);
                    const float angleDeg = strova::brush::compiledAngleDeg(rt, s, 0.0f, 0.0f);
                    drawMaskStamp(renderer, stamp, rt.stampWidth, rt.stampHeight, p0.x, p0.y, rt.diameterPx, c, angleDeg, sdlBlendForBrush(rt.params.blendMode));
                    return;
                }

                float accWorld = 0.0f;
                for (size_t i = 1; i < pts.size(); ++i)
                {
                    SDL_FPoint a = toScreen(pts[i - 1].x, pts[i - 1].y);
                    SDL_FPoint b = toScreen(pts[i].x, pts[i].y);
                    float dx = b.x - a.x;
                    float dy = b.y - a.y;
                    float lenPx = SDL_sqrtf(dx * dx + dy * dy);

                    float worldSeg = SDL_sqrtf((pts[i].x - pts[i - 1].x) * (pts[i].x - pts[i - 1].x) + (pts[i].y - pts[i - 1].y) * (pts[i].y - pts[i - 1].y));
                    const int count = std::max(1, (int)std::ceil(lenPx / std::max(0.1f, rt.spacingPx)));
                    const float dirAngle = (lenPx > 0.0001f) ? (std::atan2(dy, dx) * 57.2957795f) : 0.0f;

                    for (int j = 0; j <= count; ++j)
                    {
                        const float t2 = (float)j / (float)count;
                        const float pressure = pts[i - 1].pressure * (1.0f - t2) + pts[i].pressure * t2;
                        float sx = a.x + dx * t2;
                        float sy = a.y + dy * t2;
                        const float seed = accWorld + worldSeg * t2 + (float)(i * 131 + j * 17);

                        const float spacingJit = 1.0f + signedNoise(seed * 0.21f) * s.settings.spacingJitter;
                        sx += signedNoise(seed * 1.13f + 7.0f) * rt.scatterPx;
                        sy += signedNoise(seed * 1.71f + 5.0f) * rt.scatterPx;

                        const float sizePx = strova::brush::compiledSizePx(rt, s, pressure, seed);
                        const float angleDeg = strova::brush::compiledAngleDeg(rt, s, seed, dirAngle);
                        const float strokeU = (rt.totalLen > 0.0001f) ? ((accWorld + worldSeg * t2) / rt.totalLen) : 0.0f;
                        const SDL_Color c = strova::brush::compiledColorAt(rt, s, strokeU, pressure, seed);

                        sx += dx * t2 * (spacingJit - 1.0f) * 0.15f;
                        sy += dy * t2 * (spacingJit - 1.0f) * 0.15f;
                        drawMaskStamp(renderer, stamp, rt.stampWidth, rt.stampHeight, sx, sy, sizePx, c, angleDeg, sdlBlendForBrush(rt.params.blendMode));
                    }
                    accWorld += worldSeg;
                }
                return;
            }
        }
    }

    float diameterPx = std::max(1.0f, s.thickness * scale);
    int radiusPx = std::max(1, (int)std::lround(diameterPx * 0.5f));

    const ToolSettings& settings = s.settings;
    float spacingPx = std::max(0.35f, diameterPx * std::max(0.01f, settings.spacing > 0.0f ? settings.spacing : 0.08f));
    float scatterPx = settings.scatter * radiusPx * 0.85f;
    float hardness = (settings.hardness > 0.0f || s.tool == ToolType::Brush || s.tool == ToolType::Glow || s.tool == ToolType::Airbrush || s.tool == ToolType::Marker || s.tool == ToolType::Pencil || s.tool == ToolType::Pen)
        ? settings.hardness : 0.85f;
    float flow = (settings.flow > 0.0f || s.tool == ToolType::Airbrush || s.tool == ToolType::Glow || s.tool == ToolType::Marker)
        ? settings.flow : 1.0f;

    SDL_Color base = s.color;
    if (settings.opacity >= 0.0f)
        base.a = (Uint8)std::clamp((int)std::lround((float)base.a * settings.opacity), 0, 255);

    bool useGrad = (s.gradient.enabled && s.gradient.mode != 0);
    float totalLen = 0.0f;
    if (useGrad)
    {
        for (size_t i = 1; i < pts.size(); ++i)
        {
            float dx = pts[i].x - pts[i - 1].x;
            float dy = pts[i].y - pts[i - 1].y;
            totalLen += SDL_sqrtf(dx * dx + dy * dy);
        }
        if (totalLen <= 0.00001f) useGrad = false;
    }

    auto renderNormal = [&](float sx, float sy, int r, SDL_Color c, float pressure)
    {
        pressure = clampf(pressure, 0.1f, 1.25f);
        int rr = std::max(1, (int)std::lround((float)r * pressure));
        drawSoftStamp(renderer, sx, sy, rr, c, hardness, flow);
    };

    auto renderPencil = [&](float sx, float sy, int r, SDL_Color c, float pressure, float seed)
    {
        float n = hashNoise(seed * 0.137f + sx * 0.17f + sy * 0.11f);
        int rr = std::max(1, (int)std::lround((float)r * (0.55f + 0.25f * pressure + 0.15f * n)));
        SDL_Color core = c;
        core.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.30f + 0.50f * flow + 0.20f * n)), 0, 255);
        drawStamp(renderer, sx, sy, rr, core);
        SDL_Color grain = c;
        grain.a = (Uint8)std::clamp((int)std::lround((float)c.a * 0.12f), 0, 255);
        drawStamp(renderer, sx + (n - 0.5f) * rr, sy + (0.5f - n) * rr, std::max(1, rr - 1), grain);
    };

    auto renderMarker = [&](float sx, float sy, int r, SDL_Color c, float nx, float ny)
    {
        SDL_Color body = c;
        body.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.55f + 0.35f * flow)), 0, 255);
        int rr = std::max(1, (int)std::lround((float)r * 0.95f));
        for (int k = -2; k <= 2; ++k)
        {
            float off = (float)k * rr * 0.18f;
            drawSoftStamp(renderer, sx + nx * off, sy + ny * off, rr, body, 0.65f, flow);
        }
    };

    auto renderGlow = [&](float sx, float sy, int r, SDL_Color c)
    {
        SDL_Color halo = c;
        halo.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.08f + 0.16f * flow)), 0, 255);
        drawSoftStamp(renderer, sx, sy, std::max(1, (int)std::lround((float)r * 1.8f)), halo, 0.10f, flow, SDL_BLENDMODE_ADD);

        SDL_Color core = c;
        core.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.18f + 0.45f * flow)), 0, 255);
        drawSoftStamp(renderer, sx, sy, std::max(1, (int)std::lround((float)r * 1.1f)), core, 0.30f, flow, SDL_BLENDMODE_ADD);
    };

    auto renderAirbrush = [&](float sx, float sy, int r, SDL_Color c, float seed)
    {
        float radius = std::max((float)r, settings.airRadius * scale * 0.5f);
        int count = std::max(6, (int)std::lround(14.0f + settings.airDensity * 48.0f));
        for (int k = 0; k < count; ++k)
        {
            float a = 6.2831853f * hashNoise(seed * 1.71f + (float)k * 0.73f);
            float d = radius * std::sqrt(hashNoise(seed * 2.13f + (float)k * 1.37f));
            float px = sx + std::cos(a) * d;
            float py = sy + std::sin(a) * d;
            SDL_Color mist = c;
            mist.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.03f + settings.airDensity * 0.10f + flow * 0.08f)), 0, 255);
            drawStamp(renderer, px, py, std::max(1, (int)std::lround(radius * 0.06f)), mist);
        }
        SDL_Color center = c;
        center.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.04f + 0.20f * flow)), 0, 255);
        drawSoftStamp(renderer, sx, sy, std::max(1, (int)std::lround(radius * 0.20f)), center, 0.10f, flow);
    };

    auto renderSmudge = [&](float sx, float sy, int r, float prevSx, float prevSy, float seed)
    {
        SDL_Color sample = readPixelColor(renderer, (int)std::lround(prevSx), (int)std::lround(prevSy));
        SDL_Color mixed = lerpColor(sample, base, 0.15f);
        mixed.a = (Uint8)std::clamp((int)std::lround((float)base.a * (0.18f + settings.smudgeStrength * 0.35f)), 0, 255);
        float dragX = (sx - prevSx) * (0.25f + settings.smudgeStrength * 0.95f);
        float dragY = (sy - prevSy) * (0.25f + settings.smudgeStrength * 0.95f);
        drawSoftStamp(renderer, sx - dragX * 0.65f, sy - dragY * 0.65f, std::max(1, (int)std::lround((float)r * 1.05f)), mixed, 0.15f, 1.0f);
        SDL_Color smear = mixed;
        smear.a = (Uint8)std::clamp((int)std::lround((float)mixed.a * 0.70f), 0, 255);
        drawSoftStamp(renderer, sx, sy, r, smear, 0.05f, 1.0f);
    };

    auto renderBlurRegion = [&](float sx, float sy, int r)
    {
        const int rr = std::max(2, (int)std::lround((float)r * 1.15f));
        const int size = rr * 2 + 1;
        std::vector<Uint32> src;
        if (!readRegion(renderer, (int)std::lround(sx) - rr, (int)std::lround(sy) - rr, size, size, src))
            return;
        std::vector<Uint32> blurred = src;
        blurPixels(blurred, size, size, std::max(1, std::min(4, (int)std::lround(settings.blurRadius * 0.18f))));

        SDL_PixelFormat* fmt = rgbaFormat();
        if (!fmt) return;
        const float radius = (float)rr;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float dx = ((float)x - radius);
                const float dy = ((float)y - radius);
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float t = clampf(1.0f - dist / std::max(1.0f, radius), 0.0f, 1.0f);
                if (t <= 0.0f) continue;
                SDL_Color a{}, b{};
                SDL_GetRGBA(src[(size_t)y * (size_t)size + (size_t)x], fmt, &a.r, &a.g, &a.b, &a.a);
                SDL_GetRGBA(blurred[(size_t)y * (size_t)size + (size_t)x], fmt, &b.r, &b.g, &b.b, &b.a);
                SDL_Color o = lerpColor(a, b, t * (0.35f + settings.strength * 0.55f));
                src[(size_t)y * (size_t)size + (size_t)x] = SDL_MapRGBA(fmt, o.r, o.g, o.b, o.a);
            }
        }
        drawPixelTexture(renderer, (int)std::lround(sx) - rr, (int)std::lround(sy) - rr, size, size, src, 255, SDL_BLENDMODE_BLEND);
    };

    auto renderSoftEraser = [&](float sx, float sy, int r)
    {
        SDL_Color eraseCol{ 255, 255, 255, (Uint8)std::clamp((int)std::lround(70.0f + flow * 90.0f), 0, 255) };
        drawSoftStamp(renderer, sx, sy, std::max(1, (int)std::lround((float)r * 1.15f)), eraseCol, 0.05f, flow, SDL_BLENDMODE_BLEND);
    };

    auto renderCalligraphy = [&](float sx, float sy, int r, SDL_Color c)
    {
        const float angle = settings.angleDeg * 0.01745329252f;
        const float cs = std::cos(angle);
        const float sn = std::sin(angle);
        const float major = std::max(1.0f, (float)r);
        const float minor = std::max(1.0f, major * std::clamp((float)settings.aspect, 0.10f, 1.0f));
        const float extent = std::max(0.0f, major - minor);
        for (int k = -2; k <= 2; ++k)
        {
            const float t = (float)k / 2.0f;
            const float ox = cs * extent * t;
            const float oy = sn * extent * t;
            SDL_Color dab = c;
            dab.a = (Uint8)std::clamp((int)std::lround((float)c.a * (0.70f + 0.08f * (2 - std::abs(k)))), 0, 255);
            drawSoftStamp(renderer, sx + ox, sy + oy, (int)std::lround(minor), dab, 0.92f, 1.0f);
        }
    };

    auto drawStep = [&](float sx, float sy, int r, SDL_Color c, float pressure, float nx, float ny, float seed, float prevSx, float prevSy)
    {
        if (scatterPx > 0.0f)
        {
            sx += signedNoise(seed * 1.13f + 4.0f) * scatterPx;
            sy += signedNoise(seed * 1.91f + 9.0f) * scatterPx;
        }

        switch (s.tool)
        {
        case ToolType::Pencil:   renderPencil(sx, sy, r, c, pressure, seed); break;
        case ToolType::Pen:      drawSoftStamp(renderer, sx, sy, std::max(1, (int)std::lround(r * 0.92f)), c, 0.98f, 1.0f); break;
        case ToolType::Marker:   renderMarker(sx, sy, r, c, nx, ny); break;
        case ToolType::Glow:     renderGlow(sx, sy, r, c); break;
        case ToolType::Airbrush: renderAirbrush(sx, sy, r, c, seed); break;
        case ToolType::Smudge:   renderSmudge(sx, sy, r, prevSx, prevSy, seed); break;
        case ToolType::Blur:
            renderBlurRegion(sx, sy, r);
            break;
        case ToolType::SoftEraser:
            renderSoftEraser(sx, sy, r);
            break;
        case ToolType::Calligraphy:
            renderCalligraphy(sx, sy, r, c);
            break;
        default:
            renderNormal(sx, sy, r, c, pressure);
            break;
        }
    };

    if (pts.size() == 1)
    {
        SDL_FPoint p0 = toScreen(pts[0].x, pts[0].y);
        drawStep(p0.x, p0.y, radiusPx, base, pts[0].pressure, 0.0f, 1.0f, 0.0f, p0.x, p0.y);
        return;
    }

    float acc = 0.0f;
    SDL_FPoint lastDrawn = toScreen(pts.front().x, pts.front().y);

    for (size_t i = 1; i < pts.size(); ++i)
    {
        SDL_FPoint a = toScreen(pts[i - 1].x, pts[i - 1].y);
        SDL_FPoint b = toScreen(pts[i].x, pts[i].y);
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float lenPx = SDL_sqrtf(dx * dx + dy * dy);

        float worldSeg = 0.0f;
        if (useGrad)
        {
            float dxw = pts[i].x - pts[i - 1].x;
            float dyw = pts[i].y - pts[i - 1].y;
            worldSeg = SDL_sqrtf(dxw * dxw + dyw * dyw);
        }

        float nx = 0.0f;
        float ny = 1.0f;
        if (lenPx > 0.0001f)
        {
            nx = -dy / lenPx;
            ny = dx / lenPx;
        }

        float toolSpacingMul = 1.0f;
        if (s.tool == ToolType::Smudge) toolSpacingMul = 2.4f;
        else if (s.tool == ToolType::Blur) toolSpacingMul = 3.0f;
        else if (s.tool == ToolType::Airbrush) toolSpacingMul = 1.35f;
        int count = std::max(1, (int)std::ceil(lenPx / std::max(1.0f, spacingPx * toolSpacingMul)));
        for (int j = 0; j <= count; ++j)
        {
            float t = (float)j / (float)count;
            float sx = a.x + dx * t;
            float sy = a.y + dy * t;

            SDL_Color c = base;
            if (useGrad)
            {
                float u = (acc + worldSeg * t) / totalLen;
                c = sampleStops4(s.gradient, u);
                c.a = base.a;
            }

            float pressure = pts[i - 1].pressure * (1.0f - t) + pts[i].pressure * t;
            float seed = acc + worldSeg * t + (float)i * 17.0f + (float)j * 0.37f;
            drawStep(sx, sy, radiusPx, c, pressure, nx, ny, seed, lastDrawn.x, lastDrawn.y);
            lastDrawn = SDL_FPoint{ sx, sy };
        }

        if (useGrad) acc += worldSeg;
    }
}

void BrushRenderer::drawStrokeExport(const Stroke& s)
{
    drawStroke(s, 1.0f, 0.0f, 0.0f, 0, 0);
}