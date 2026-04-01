#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace strova::ui_text
{
    struct CachedTextTexture
    {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
        std::size_t bytes = 0;
    };

    struct CacheNode
    {
        CachedTextTexture value{};
        std::list<std::uint64_t>::iterator lruIt{};
    };

    struct CacheState
    {
        std::unordered_map<std::uint64_t, CacheNode> items;
        std::list<std::uint64_t> lru;
        std::size_t bytes = 0;
        std::size_t maxItems = 1024;
        std::size_t maxBytes = 32u * 1024u * 1024u;
    };

    inline CacheState& cacheState()
    {
        static CacheState state;
        return state;
    }

    inline std::uint64_t mixHash64(std::uint64_t h, std::uint64_t v)
    {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }

    inline std::uint64_t hashTextKey(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color)
    {
        std::uint64_t h = 1469598103934665603ull;
        h = mixHash64(h, (std::uint64_t)(uintptr_t)renderer);
        h = mixHash64(h, (std::uint64_t)(uintptr_t)font);
        for (unsigned char ch : text)
            h = mixHash64(h, (std::uint64_t)ch);
        const std::uint64_t rgba = ((std::uint64_t)color.r << 24) |
                                   ((std::uint64_t)color.g << 16) |
                                   ((std::uint64_t)color.b << 8) |
                                   (std::uint64_t)color.a;
        h = mixHash64(h, rgba);
        return h;
    }

    inline void destroyCachedTexture(CachedTextTexture& tt)
    {
        if (tt.tex)
            SDL_DestroyTexture(tt.tex);
        tt = {};
    }

    inline void trimCache()
    {
        CacheState& state = cacheState();
        while ((!state.lru.empty()) && (state.items.size() > state.maxItems || state.bytes > state.maxBytes))
        {
            const std::uint64_t key = state.lru.back();
            state.lru.pop_back();
            auto it = state.items.find(key);
            if (it == state.items.end())
                continue;
            state.bytes = (state.bytes > it->second.value.bytes) ? (state.bytes - it->second.value.bytes) : 0;
            destroyCachedTexture(it->second.value);
            state.items.erase(it);
        }
    }

    inline void purgeRenderer(SDL_Renderer* renderer)
    {
        if (!renderer)
            return;
        CacheState& state = cacheState();
        for (auto it = state.items.begin(); it != state.items.end(); )
        {
            auto next = std::next(it);
            if (it->second.value.tex)
            {
                it->second.value.bytes = std::max<std::size_t>(it->second.value.bytes, (std::size_t)it->second.value.w * (std::size_t)it->second.value.h * 4u);
                state.bytes = (state.bytes > it->second.value.bytes) ? (state.bytes - it->second.value.bytes) : 0;
                destroyCachedTexture(it->second.value);
                state.lru.erase(it->second.lruIt);
                state.items.erase(it);
            }
            it = next;
        }
    }

    inline int measureTextWidth(TTF_Font* font, const std::string& text)
    {
        if (!font || text.empty())
            return 0;
        int w = 0;
        int h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0)
            return 0;
        return w;
    }

    inline int measureTextHeight(TTF_Font* font)
    {
        return font ? std::max(1, TTF_FontHeight(font)) : 0;
    }

    inline const CachedTextTexture* fetchTextTexture(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color)
    {
        if (!renderer || !font || text.empty())
            return nullptr;

        CacheState& state = cacheState();
        const std::uint64_t key = hashTextKey(renderer, font, text, color);
        auto found = state.items.find(key);
        if (found != state.items.end())
        {
            state.lru.erase(found->second.lruIt);
            state.lru.push_front(key);
            found->second.lruIt = state.lru.begin();
            return &found->second.value;
        }

        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surf)
            return nullptr;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (!tex)
        {
            SDL_FreeSurface(surf);
            return nullptr;
        }

        CachedTextTexture value{};
        value.tex = tex;
        value.w = surf->w;
        value.h = surf->h;
        value.bytes = (std::size_t)std::max(1, surf->w) * (std::size_t)std::max(1, surf->h) * 4u;
        SDL_FreeSurface(surf);

        state.lru.push_front(key);
        state.bytes += value.bytes;
        auto [it, inserted] = state.items.emplace(key, CacheNode{});
        (void)inserted;
        it->second.value = value;
        it->second.lruIt = state.lru.begin();
        trimCache();
        auto refind = state.items.find(key);
        return (refind != state.items.end()) ? &refind->second.value : nullptr;
    }

    inline void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color)
    {
        const CachedTextTexture* tt = fetchTextTexture(renderer, font, text, color);
        if (!tt || !tt->tex)
            return;
        SDL_Rect dst{ x, y, tt->w, tt->h };
        SDL_RenderCopy(renderer, tt->tex, nullptr, &dst);
    }

    inline void drawTextClipped(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, const SDL_Rect& clip)
    {
        if (!renderer)
            return;
        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &oldClip);
        SDL_RenderSetClipRect(renderer, &clip);
        drawText(renderer, font, text, x, y, color);
        if (hadClip) SDL_RenderSetClipRect(renderer, &oldClip);
        else SDL_RenderSetClipRect(renderer, nullptr);
    }

    inline std::string ellipsizeText(TTF_Font* font, const std::string& text, int maxWidth)
    {
        if (!font || text.empty() || maxWidth <= 0)
            return std::string();
        if (measureTextWidth(font, text) <= maxWidth)
            return text;

        const std::string ellipsis = "...";
        const int ellipsisW = measureTextWidth(font, ellipsis);
        if (ellipsisW >= maxWidth)
            return ellipsis;

        std::size_t lo = 0;
        std::size_t hi = text.size();
        std::size_t best = 0;
        while (lo <= hi)
        {
            const std::size_t mid = lo + ((hi - lo) / 2);
            const std::string candidate = text.substr(0, mid) + ellipsis;
            if (measureTextWidth(font, candidate) <= maxWidth)
            {
                best = mid;
                lo = mid + 1;
            }
            else
            {
                if (mid == 0)
                    break;
                hi = mid - 1;
            }
        }
        return text.substr(0, best) + ellipsis;
    }

    inline void drawTextEllipsized(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const SDL_Rect& bounds, SDL_Color color)
    {
        if (bounds.w <= 0 || bounds.h <= 0)
            return;
        const std::string clipped = ellipsizeText(font, text, bounds.w);
        const int h = measureTextHeight(font);
        const int y = bounds.y + std::max(0, (bounds.h - h) / 2);
        drawTextClipped(renderer, font, clipped, bounds.x, y, color, bounds);
    }

    inline void drawTextLeftMiddle(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const SDL_Rect& bounds, SDL_Color color)
    {
        if (bounds.w <= 0 || bounds.h <= 0)
            return;
        const std::string clipped = ellipsizeText(font, text, bounds.w);
        const int h = measureTextHeight(font);
        const int y = bounds.y + std::max(0, (bounds.h - h) / 2);
        drawTextClipped(renderer, font, clipped, bounds.x, y, color, bounds);
    }

    inline void drawTextCentered(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const SDL_Rect& bounds, SDL_Color color)
    {
        if (bounds.w <= 0 || bounds.h <= 0)
            return;
        const std::string clipped = ellipsizeText(font, text, bounds.w);
        const int w = measureTextWidth(font, clipped);
        const int h = measureTextHeight(font);
        const int x = bounds.x + std::max(0, (bounds.w - w) / 2);
        const int y = bounds.y + std::max(0, (bounds.h - h) / 2);
        drawTextClipped(renderer, font, clipped, x, y, color, bounds);
    }


    inline std::vector<std::string> wrapText(TTF_Font* font, const std::string& text, int maxWidth, int maxLines = 0, bool ellipsizeLastLine = true)
    {
        std::vector<std::string> lines;
        if (!font || text.empty() || maxWidth <= 0)
            return lines;

        auto pushLine = [&](const std::string& line)
        {
            if (maxLines > 0 && (int)lines.size() + 1 >= maxLines && ellipsizeLastLine)
                lines.push_back(ellipsizeText(font, line, maxWidth));
            else
                lines.push_back(line);
        };

        std::string current;
        std::size_t i = 0;
        while (i < text.size())
        {
            if (text[i] == '\n')
            {
                pushLine(current);
                current.clear();
                ++i;
                if (maxLines > 0 && (int)lines.size() >= maxLines)
                    return lines;
                continue;
            }

            std::size_t start = i;
            while (i < text.size() && text[i] != ' ' && text[i] != '\n')
                ++i;
            const std::string word = text.substr(start, i - start);
            const std::string candidate = current.empty() ? word : (current + " " + word);
            if (measureTextWidth(font, candidate) <= maxWidth)
            {
                current = candidate;
            }
            else if (current.empty())
            {
                std::string partial;
                for (char ch : word)
                {
                    std::string next = partial + ch;
                    if (measureTextWidth(font, next) > maxWidth)
                    {
                        if (!partial.empty())
                        {
                            pushLine(partial);
                            partial.clear();
                            if (maxLines > 0 && (int)lines.size() >= maxLines)
                                return lines;
                        }
                    }
                    partial.push_back(ch);
                }
                current = partial;
            }
            else
            {
                pushLine(current);
                current = word;
                if (maxLines > 0 && (int)lines.size() >= maxLines)
                    return lines;
            }

            while (i < text.size() && text[i] == ' ')
                ++i;
        }

        if (!current.empty() && (maxLines <= 0 || (int)lines.size() < maxLines))
            pushLine(current);
        return lines;
    }

    inline int drawTextWrapped(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const SDL_Rect& bounds, SDL_Color color, int maxLines = 0, int lineGap = 2)
    {
        if (!renderer || bounds.w <= 0 || bounds.h <= 0)
            return 0;

        const std::vector<std::string> lines = wrapText(font, text, bounds.w, maxLines, true);
        const int lineH = measureTextHeight(font);
        const int step = std::max(1, lineH + lineGap);

        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &oldClip);
        SDL_RenderSetClipRect(renderer, &bounds);

        int drawn = 0;
        int y = bounds.y;
        for (const std::string& line : lines)
        {
            if (y + lineH > bounds.y + bounds.h)
                break;
            drawText(renderer, font, line, bounds.x, y, color);
            y += step;
            ++drawn;
        }

        if (hadClip) SDL_RenderSetClipRect(renderer, &oldClip);
        else SDL_RenderSetClipRect(renderer, nullptr);
        return drawn;
    }
}
