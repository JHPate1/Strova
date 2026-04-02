#pragma once

#include "DrawingEngine.h"
#include "RenderCacheManager.h"
#include "../render/BrushRenderer.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace strova::layer_render
{
    struct Bounds
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
    };

    inline float degToRad(float deg)
    {
        return deg * 0.01745329251994329577f;
    }

    inline SDL_FPoint rotatePointAround(const SDL_FPoint& p, const SDL_FPoint& pivot, float rotationDeg)
    {
        if (std::fabs(rotationDeg) <= 0.0001f)
            return p;

        const float rad = degToRad(rotationDeg);
        const float s = std::sinf(rad);
        const float c = std::cosf(rad);
        const float lx = p.x - pivot.x;
        const float ly = p.y - pivot.y;
        return SDL_FPoint{
            pivot.x + lx * c - ly * s,
            pivot.y + lx * s + ly * c
        };
    }

    inline bool calcStrokeBounds(const std::vector<Stroke>& strokes, Bounds& out)
    {
        bool found = false;
        for (const auto& stroke : strokes)
        {
            for (const auto& pt : stroke.points)
            {
                if (!found)
                {
                    out.minX = out.maxX = pt.x;
                    out.minY = out.maxY = pt.y;
                    found = true;
                }
                else
                {
                    out.minX = std::min(out.minX, pt.x);
                    out.minY = std::min(out.minY, pt.y);
                    out.maxX = std::max(out.maxX, pt.x);
                    out.maxY = std::max(out.maxY, pt.y);
                }
            }
        }
        return found;
    }

    inline bool calcLocalBounds(const DrawingEngine::TrackLayer& layer, Bounds& out)
    {
        bool found = false;

        if (!layer.image.empty())
        {
            out.minX = -layer.image.width() * 0.5f;
            out.maxX = layer.image.width() * 0.5f;
            out.minY = -layer.image.height() * 0.5f;
            out.maxY = layer.image.height() * 0.5f;
            found = true;
        }

        Bounds strokeBounds{};
        if (calcStrokeBounds(layer.strokes, strokeBounds))
        {
            if (!found)
            {
                out = strokeBounds;
                found = true;
            }
            else
            {
                out.minX = std::min(out.minX, strokeBounds.minX);
                out.minY = std::min(out.minY, strokeBounds.minY);
                out.maxX = std::max(out.maxX, strokeBounds.maxX);
                out.maxY = std::max(out.maxY, strokeBounds.maxY);
            }
        }

        return found;
    }

    inline SDL_FPoint strokeLayerPivot(const DrawingEngine::TrackLayer& layer)
    {
        if (std::fabs(layer.transform.pivotX) > 0.0001f || std::fabs(layer.transform.pivotY) > 0.0001f)
            return SDL_FPoint{ layer.transform.pivotX, layer.transform.pivotY };
        Bounds b{};
        if (!calcStrokeBounds(layer.strokes, b))
            return SDL_FPoint{ 0.0f, 0.0f };
        return SDL_FPoint{ (b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f };
    }

    inline bool calcWorldBounds(const DrawingEngine::TrackLayer& layer, Bounds& out)
    {
        bool found = false;
        auto include = [&](const SDL_FPoint& p)
        {
            if (!found)
            {
                out.minX = out.maxX = p.x;
                out.minY = out.maxY = p.y;
                found = true;
            }
            else
            {
                out.minX = std::min(out.minX, p.x);
                out.minY = std::min(out.minY, p.y);
                out.maxX = std::max(out.maxX, p.x);
                out.maxY = std::max(out.maxY, p.y);
            }
        };

        if (!layer.image.empty())
        {
            const float hw = layer.image.width() * 0.5f;
            const float hh = layer.image.height() * 0.5f;
            const SDL_FPoint center{ layer.transform.posX, layer.transform.posY };
            const SDL_FPoint pivot{
                (std::fabs(layer.transform.pivotX) > 0.0001f || std::fabs(layer.transform.pivotY) > 0.0001f) ? layer.transform.pivotX : center.x,
                (std::fabs(layer.transform.pivotX) > 0.0001f || std::fabs(layer.transform.pivotY) > 0.0001f) ? layer.transform.pivotY : center.y
            };
            const SDL_FPoint corners[4] = {
                { center.x - hw, center.y - hh },
                { center.x + hw, center.y - hh },
                { center.x + hw, center.y + hh },
                { center.x - hw, center.y + hh }
            };
            for (SDL_FPoint p : corners)
                include(rotatePointAround(p, pivot, layer.transform.rotation));
        }

        if (!layer.strokes.empty())
        {
            const SDL_FPoint pivot = strokeLayerPivot(layer);
            for (const auto& s : layer.strokes)
            {
                for (const auto& pt : s.points)
                {
                    SDL_FPoint p{ pt.x, pt.y };
                    p = rotatePointAround(p, pivot, layer.transform.rotation);
                    p.x += layer.transform.posX;
                    p.y += layer.transform.posY;
                    include(p);
                }
            }
        }

        return found;
    }

    inline Stroke transformedStrokeCopy(const Stroke& src, const DrawingEngine::TrackLayer& layer)
    {
        Stroke out = src;
        if (out.points.empty())
            return out;

        const SDL_FPoint pivot = strokeLayerPivot(layer);
        for (auto& pt : out.points)
        {
            SDL_FPoint p{ pt.x, pt.y };
            p = rotatePointAround(p, pivot, layer.transform.rotation);
            pt.x = p.x + layer.transform.posX;
            pt.y = p.y + layer.transform.posY;
        }
        return out;
    }

    struct StrokeTransformCacheEntry
    {
        std::vector<Stroke> strokes;
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;
        std::list<std::string>::iterator lruIt{};
    };

    struct StrokeTransformCacheState
    {
        std::unordered_map<std::string, StrokeTransformCacheEntry> items;
        std::list<std::string> lru;
        std::size_t bytes = 0;
    };

    inline StrokeTransformCacheState& strokeTransformCacheState()
    {
        static StrokeTransformCacheState state;
        return state;
    }

    inline std::size_t estimateStrokeBytes(const std::vector<Stroke>& strokes)
    {
        std::size_t bytes = 0;
        for (const auto& stroke : strokes)
        {
            bytes += sizeof(Stroke);
            bytes += stroke.points.capacity() * sizeof(StrokePoint);
            bytes += stroke.brushId.capacity();
            bytes += stroke.brushName.capacity();
        }
        return bytes;
    }

    inline void trimStrokeTransformCache()
    {
        auto& state = strokeTransformCacheState();
        while (render_cache::overBudget(render_cache::Bucket::StrokeTransform) && !state.lru.empty())
        {
            const std::string key = state.lru.back();
            state.lru.pop_back();
            auto it = state.items.find(key);
            if (it == state.items.end())
                continue;
            state.bytes = (state.bytes > it->second.bytes) ? (state.bytes - it->second.bytes) : 0;
            render_cache::erase(render_cache::Bucket::StrokeTransform, key);
            state.items.erase(it);
        }
    }

    inline const std::vector<Stroke>* fetchTransformedStrokeCache(const DrawingEngine::TrackLayer& layer, const std::string& cacheKey, std::uint64_t frameOrdinal)
    {
        if (cacheKey.empty())
            return nullptr;

        auto& state = strokeTransformCacheState();
        auto it = state.items.find(cacheKey);
        if (it != state.items.end())
        {
            state.lru.erase(it->second.lruIt);
            state.lru.push_front(cacheKey);
            it->second.lruIt = state.lru.begin();
            it->second.lastUse = frameOrdinal;
            render_cache::markUsed(render_cache::Bucket::StrokeTransform, cacheKey, frameOrdinal);
            return &it->second.strokes;
        }

        StrokeTransformCacheEntry entry{};
        entry.strokes.reserve(layer.strokes.size());
        for (const auto& src : layer.strokes)
            entry.strokes.push_back(transformedStrokeCopy(src, layer));
        entry.bytes = estimateStrokeBytes(entry.strokes);
        entry.lastUse = frameOrdinal;
        state.lru.push_front(cacheKey);
        entry.lruIt = state.lru.begin();
        state.bytes += entry.bytes;
        render_cache::touch(render_cache::Bucket::StrokeTransform, cacheKey, entry.bytes, frameOrdinal);
        auto [insIt, inserted] = state.items.emplace(cacheKey, std::move(entry));
        (void)inserted;
        trimStrokeTransformCache();
        auto refind = state.items.find(cacheKey);
        return (refind != state.items.end()) ? &refind->second.strokes : nullptr;
    }


    struct ImageTextureCacheMeta
    {
        SDL_Texture* texture = nullptr;
        std::unordered_map<std::string, SDL_Texture*>* owner = nullptr;
        int width = 0;
        int height = 0;
        std::uint64_t surfaceRevision = 0;
        std::uint64_t lastUse = 0;
        std::size_t bytes = 0;
    };

    inline std::unordered_map<std::string, ImageTextureCacheMeta>& imageTextureMetaCache()
    {
        static std::unordered_map<std::string, ImageTextureCacheMeta> state;
        return state;
    }

    inline void eraseImageTextureMeta(const std::string& cacheKey)
    {
        if (cacheKey.empty())
            return;
        render_cache::erase(render_cache::Bucket::ImageTexture, cacheKey);
        imageTextureMetaCache().erase(cacheKey);
    }


    inline void trimImageTextureCache()
    {
        auto& metaCache = imageTextureMetaCache();
        while (render_cache::overBudget(render_cache::Bucket::ImageTexture) && !metaCache.empty())
        {
            auto oldest = metaCache.end();
            for (auto it = metaCache.begin(); it != metaCache.end(); ++it)
            {
                if (oldest == metaCache.end() || it->second.lastUse < oldest->second.lastUse)
                    oldest = it;
            }
            if (oldest == metaCache.end())
                break;

            if (oldest->second.owner)
            {
                auto ownerIt = oldest->second.owner->find(oldest->first);
                if (ownerIt != oldest->second.owner->end())
                {
                    if (ownerIt->second)
                        SDL_DestroyTexture(ownerIt->second);
                    oldest->second.owner->erase(ownerIt);
                }
                else if (oldest->second.texture)
                {
                    SDL_DestroyTexture(oldest->second.texture);
                }
            }
            else if (oldest->second.texture)
            {
                SDL_DestroyTexture(oldest->second.texture);
            }

            render_cache::erase(render_cache::Bucket::ImageTexture, oldest->first);
            metaCache.erase(oldest);
        }
    }

    inline void purgeAllImageTextureCache()
    {
        auto& metaCache = imageTextureMetaCache();
        for (auto& kv : metaCache)
        {
            if (kv.second.owner)
            {
                auto it = kv.second.owner->find(kv.first);
                if (it != kv.second.owner->end())
                {
                    if (it->second)
                        SDL_DestroyTexture(it->second);
                    kv.second.owner->erase(it);
                    continue;
                }
            }
            if (kv.second.texture)
                SDL_DestroyTexture(kv.second.texture);
        }
        metaCache.clear();
        render_cache::clearBucket(render_cache::Bucket::ImageTexture);
    }

    inline SDL_Texture* createTextureFromImage(SDL_Renderer* r, const DrawingEngine::LayerImage& image)
    {
        if (!r || image.empty())
            return nullptr;

        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            (void*)image.rgba().data(),
            image.width(),
            image.height(),
            32,
            image.width() * 4,
            SDL_PIXELFORMAT_RGBA32);
        if (!surf)
            return nullptr;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        if (tex)
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        return tex;
    }

    inline SDL_Texture* fetchCachedTexture(
        SDL_Renderer* r,
        const DrawingEngine::LayerImage& image,
        std::unordered_map<std::string, SDL_Texture*>* cache,
        const std::string& cacheKey)
    {
        if (!cache || cacheKey.empty())
            return createTextureFromImage(r, image);

        const std::uint64_t surfaceRevision = image.surfaceRevision();
        auto& metaCache = imageTextureMetaCache();
        auto it = cache->find(cacheKey);
        auto metaIt = metaCache.find(cacheKey);
        if (it != cache->end() && it->second)
        {
            SDL_Texture* existing = it->second;
            if (metaIt != metaCache.end() && metaIt->second.texture == existing)
            {
                const bool sameSize = metaIt->second.width == image.width() && metaIt->second.height == image.height();
                if (sameSize && metaIt->second.surfaceRevision == surfaceRevision)
                {
                    metaIt->second.lastUse = render_cache::nextUseOrdinal();
                    render_cache::markUsed(render_cache::Bucket::ImageTexture, cacheKey, metaIt->second.lastUse);
                    return existing;
                }

                if (sameSize && image.surfacePtr() && image.surfaceDirty() && !image.surfacePtr()->dirtyRects().empty())
                {
                    const auto& rects = image.surfacePtr()->dirtyRects();
                    const int pitch = image.width() * 4;
                    bool updated = true;
                    for (const auto& rect : rects)
                    {
                        SDL_Rect sdlRect{rect.x, rect.y, rect.w, rect.h};
                        const std::uint8_t* src = image.rgba().data() + static_cast<std::size_t>(rect.y * pitch + rect.x * 4);
                        if (SDL_UpdateTexture(existing, &sdlRect, src, pitch) != 0)
                        {
                            updated = false;
                            break;
                        }
                    }
                    if (updated)
                    {
                        metaIt->second.surfaceRevision = surfaceRevision;
                        metaIt->second.lastUse = render_cache::nextUseOrdinal();
                        render_cache::markUsed(render_cache::Bucket::ImageTexture, cacheKey, metaIt->second.lastUse);
                        if (image.surfacePtr())
                            image.surfacePtr()->clearDirty();
                        return existing;
                    }
                }
            }

            SDL_DestroyTexture(existing);
            render_cache::erase(render_cache::Bucket::ImageTexture, cacheKey);
            cache->erase(it);
            metaCache.erase(cacheKey);
        }

        SDL_Texture* tex = createTextureFromImage(r, image);
        if (tex)
        {
            (*cache)[cacheKey] = tex;
            ImageTextureCacheMeta meta{};
            meta.texture = tex;
            meta.owner = cache;
            meta.width = image.width();
            meta.height = image.height();
            meta.surfaceRevision = surfaceRevision;
            meta.lastUse = render_cache::nextUseOrdinal();
            meta.bytes = render_cache::estimateTextureBytes(image.width(), image.height());
            metaCache[cacheKey] = meta;
            render_cache::touch(render_cache::Bucket::ImageTexture, cacheKey, meta.bytes, meta.lastUse);
            trimImageTextureCache();
            if (image.surfacePtr())
                image.surfacePtr()->clearDirty();
        }
        return tex;
    }

    inline void drawTrackLayer(
        SDL_Renderer* r,
        BrushRenderer& brush,
        const DrawingEngine::TrackLayer& layer,
        float viewScale,
        float panX,
        float panY,
        int canvasX,
        int canvasY,
        float alphaMul = 1.0f,
        std::unordered_map<std::string, SDL_Texture*>* imageCache = nullptr,
        const std::string& imageCacheKey = std::string(),
        const std::string& transformedStrokeCacheKey = std::string(),
        std::uint64_t transformedStrokeCacheFrameOrdinal = 0)
    {
        if (!r)
            return;

        alphaMul = std::clamp(alphaMul, 0.0f, 1.0f);

        if (!layer.image.empty())
        {
            SDL_Texture* tex = fetchCachedTexture(r, layer.image, imageCache, imageCacheKey);
            if (tex)
            {
                SDL_FRect dst{};
                dst.w = layer.image.width() * viewScale;
                dst.h = layer.image.height() * viewScale;
                dst.x = canvasX + panX + (layer.transform.posX - layer.image.width() * 0.5f) * viewScale;
                dst.y = canvasY + panY + (layer.transform.posY - layer.image.height() * 0.5f) * viewScale;

                const Uint8 alpha = (Uint8)std::clamp((int)std::lround(255.0f * alphaMul), 0, 255);
                SDL_SetTextureAlphaMod(tex, alpha);
                SDL_FPoint center{
                    (std::fabs(layer.transform.pivotX) > 0.0001f || std::fabs(layer.transform.pivotY) > 0.0001f)
                        ? (layer.transform.pivotX - (layer.transform.posX - layer.image.width() * 0.5f)) * viewScale
                        : dst.w * 0.5f,
                    (std::fabs(layer.transform.pivotX) > 0.0001f || std::fabs(layer.transform.pivotY) > 0.0001f)
                        ? (layer.transform.pivotY - (layer.transform.posY - layer.image.height() * 0.5f)) * viewScale
                        : dst.h * 0.5f
                };
                SDL_RenderCopyExF(r, tex, nullptr, &dst, layer.transform.rotation, &center, SDL_FLIP_NONE);
                SDL_SetTextureAlphaMod(tex, 255);

                if (!imageCache || imageCacheKey.empty())
                    SDL_DestroyTexture(tex);
            }
        }

        const bool hasTransform = std::fabs(layer.transform.posX) > 0.0001f ||
                                  std::fabs(layer.transform.posY) > 0.0001f ||
                                  std::fabs(layer.transform.rotation) > 0.0001f;
        const std::vector<Stroke>* strokeSource = &layer.strokes;
        std::vector<Stroke> transformedScratch;
        if (hasTransform && !layer.strokes.empty())
        {
            if (const std::vector<Stroke>* cached = fetchTransformedStrokeCache(layer, transformedStrokeCacheKey, transformedStrokeCacheFrameOrdinal))
            {
                strokeSource = cached;
            }
            else
            {
                transformedScratch.reserve(layer.strokes.size());
                for (const auto& src : layer.strokes)
                    transformedScratch.push_back(transformedStrokeCopy(src, layer));
                strokeSource = &transformedScratch;
            }
        }

        for (const auto& src : *strokeSource)
        {
            if (alphaMul >= 0.999f)
            {
                brush.drawStroke(src, viewScale, panX, panY, canvasX, canvasY);
                continue;
            }
            Stroke s = src;
            s.color.a = (Uint8)std::clamp((int)std::lround((float)s.color.a * alphaMul), 0, 255);
            brush.drawStroke(s, viewScale, panX, panY, canvasX, canvasY);
        }
    }
}
