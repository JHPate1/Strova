#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace strova::render_cache
{
    enum class Bucket : std::uint8_t
    {
        ImageTexture = 0,
        StrokeTransform,
        BrushPreview,
        Count
    };

    struct Entry
    {
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;
    };

    struct BucketState
    {
        std::unordered_map<std::string, Entry> entries;
        std::size_t bytes = 0;
        std::size_t maxBytes = 0;
        std::size_t maxItems = 0;
    };

    inline std::array<BucketState, static_cast<std::size_t>(Bucket::Count)>& allBuckets()
    {
        static std::array<BucketState, static_cast<std::size_t>(Bucket::Count)> buckets = []
        {
            std::array<BucketState, static_cast<std::size_t>(Bucket::Count)> out{};
            out[static_cast<std::size_t>(Bucket::ImageTexture)].maxBytes = 256u * 1024u * 1024u;
            out[static_cast<std::size_t>(Bucket::ImageTexture)].maxItems = 1024u;
            out[static_cast<std::size_t>(Bucket::StrokeTransform)].maxBytes = 96u * 1024u * 1024u;
            out[static_cast<std::size_t>(Bucket::StrokeTransform)].maxItems = 2048u;
            out[static_cast<std::size_t>(Bucket::BrushPreview)].maxBytes = 48u * 1024u * 1024u;
            out[static_cast<std::size_t>(Bucket::BrushPreview)].maxItems = 256u;
            return out;
        }();
        return buckets;
    }

    inline std::uint64_t& globalUseCounter()
    {
        static std::uint64_t counter = 1;
        return counter;
    }

    inline BucketState& bucketState(Bucket bucket)
    {
        return allBuckets()[static_cast<std::size_t>(bucket)];
    }

    inline std::uint64_t nextUseOrdinal()
    {
        return ++globalUseCounter();
    }

    inline void configureBucket(Bucket bucket, std::size_t maxBytes, std::size_t maxItems)
    {
        auto& state = bucketState(bucket);
        state.maxBytes = maxBytes;
        state.maxItems = maxItems;
    }

    inline void touch(Bucket bucket, const std::string& key, std::size_t bytes, std::uint64_t useOrdinal = 0)
    {
        if (key.empty())
            return;

        auto& state = bucketState(bucket);
        Entry& entry = state.entries[key];
        if (state.bytes >= entry.bytes)
            state.bytes -= entry.bytes;
        else
            state.bytes = 0;

        entry.bytes = bytes;
        entry.lastUse = useOrdinal ? useOrdinal : nextUseOrdinal();
        state.bytes += entry.bytes;
    }

    inline void markUsed(Bucket bucket, const std::string& key, std::uint64_t useOrdinal = 0)
    {
        if (key.empty())
            return;
        auto& state = bucketState(bucket);
        auto it = state.entries.find(key);
        if (it == state.entries.end())
            return;
        it->second.lastUse = useOrdinal ? useOrdinal : nextUseOrdinal();
    }

    inline void erase(Bucket bucket, const std::string& key)
    {
        if (key.empty())
            return;
        auto& state = bucketState(bucket);
        auto it = state.entries.find(key);
        if (it == state.entries.end())
            return;
        if (state.bytes >= it->second.bytes)
            state.bytes -= it->second.bytes;
        else
            state.bytes = 0;
        state.entries.erase(it);
    }

    inline bool overBudget(Bucket bucket)
    {
        const auto& state = bucketState(bucket);
        const bool bytesOver = state.maxBytes > 0 && state.bytes > state.maxBytes;
        const bool itemsOver = state.maxItems > 0 && state.entries.size() > state.maxItems;
        return bytesOver || itemsOver;
    }

    inline std::size_t bytes(Bucket bucket)
    {
        return bucketState(bucket).bytes;
    }

    inline std::size_t items(Bucket bucket)
    {
        return bucketState(bucket).entries.size();
    }

    inline std::size_t totalBytes()
    {
        std::size_t total = 0;
        for (const auto& bucket : allBuckets())
            total += bucket.bytes;
        return total;
    }

    inline void clearBucket(Bucket bucket)
    {
        auto& state = bucketState(bucket);
        state.entries.clear();
        state.bytes = 0;
    }

    inline void clearAll()
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(Bucket::Count); ++i)
            clearBucket(static_cast<Bucket>(i));
    }

    inline std::size_t estimateTextureBytes(int w, int h)
    {
        return static_cast<std::size_t>(std::max(0, w)) * static_cast<std::size_t>(std::max(0, h)) * 4u;
    }
}
