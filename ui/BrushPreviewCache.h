#pragma once

#include <SDL.h>

#include "../core/BrushSystem.h"
#include "../core/RenderCacheManager.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace strova::ui_brush_preview
{
    struct PreviewCacheEntry
    {
        SDL_Texture* tex = nullptr;
        int srcW = 0;
        int srcH = 0;
        std::uint64_t revision = 0;
        bool usingStampFallback = false;
        std::uint64_t lastUse = 0;
        std::size_t bytes = 0;
    };

    inline std::unordered_map<std::string, PreviewCacheEntry>& previewCache()
    {
        static std::unordered_map<std::string, PreviewCacheEntry> cache;
        return cache;
    }

    inline std::uint64_t& previewUseCounter()
    {
        static std::uint64_t ordinal = 0;
        return ordinal;
    }

    inline const brush::BrushStamp* previewSource(const brush::BrushPackage& pkg, bool& outFallbackToStamp)
    {
        outFallbackToStamp = false;
        if (pkg.preview.width > 0 && pkg.preview.height > 0 && !pkg.preview.rgba.empty())
            return &pkg.preview;
        if (pkg.stamp.width > 0 && pkg.stamp.height > 0 && !pkg.stamp.rgba.empty())
        {
            outFallbackToStamp = true;
            return &pkg.stamp;
        }
        return nullptr;
    }


    inline void trimPreviewCache(const std::string& protectedKey = std::string())
    {
        auto& cache = previewCache();
        while (render_cache::overBudget(render_cache::Bucket::BrushPreview) && !cache.empty())
        {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it)
            {
                if (it->first == protectedKey)
                    continue;
                if (oldest == cache.end() || it->second.lastUse < oldest->second.lastUse)
                    oldest = it;
            }
            if (oldest == cache.end())
                break;
            if (oldest->second.tex)
                SDL_DestroyTexture(oldest->second.tex);
            render_cache::erase(render_cache::Bucket::BrushPreview, oldest->first);
            cache.erase(oldest);
        }
    }

    inline void purgePreviewCache()
    {
        auto& cache = previewCache();
        for (auto& kv : cache)
            if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        cache.clear();
        render_cache::clearBucket(render_cache::Bucket::BrushPreview);
    }

    inline SDL_Texture* getPreviewTexture(SDL_Renderer* renderer, const brush::BrushPackage& pkg)
    {
        if (!renderer)
            return nullptr;

        bool fallbackToStamp = false;
        const brush::BrushStamp* source = previewSource(pkg, fallbackToStamp);
        if (!source)
            return nullptr;

        auto& cache = previewCache();
        auto& useCounter = previewUseCounter();
        ++useCounter;
        PreviewCacheEntry& entry = cache[pkg.manifest.id];
        if (entry.tex && entry.revision == source->revision && entry.srcW == source->width && entry.srcH == source->height && entry.usingStampFallback == fallbackToStamp)
        {
            entry.lastUse = useCounter;
            render_cache::markUsed(render_cache::Bucket::BrushPreview, pkg.manifest.id, entry.lastUse);
            return entry.tex;
        }

        if (entry.tex)
        {
            SDL_DestroyTexture(entry.tex);
            render_cache::erase(render_cache::Bucket::BrushPreview, pkg.manifest.id);
        }
        entry = {};
        entry.tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, source->width, source->height);
        if (!entry.tex)
            return nullptr;

        SDL_UpdateTexture(entry.tex, nullptr, source->rgba.data(), source->width * 4);
        SDL_SetTextureBlendMode(entry.tex, SDL_BLENDMODE_BLEND);
        entry.srcW = source->width;
        entry.srcH = source->height;
        entry.revision = source->revision;
        entry.usingStampFallback = fallbackToStamp;
        entry.lastUse = useCounter;
        entry.bytes = render_cache::estimateTextureBytes(source->width, source->height);
        render_cache::touch(render_cache::Bucket::BrushPreview, pkg.manifest.id, entry.bytes, entry.lastUse);
        trimPreviewCache(pkg.manifest.id);

        return entry.tex;
    }
}
