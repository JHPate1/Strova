#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class RasterSurface
{
public:
    struct DirtyRect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        bool empty() const { return w <= 0 || h <= 0; }
    };

    struct DirtyTile
    {
        int tx = 0;
        int ty = 0;

        bool operator==(const DirtyTile& other) const
        {
            return tx == other.tx && ty == other.ty;
        }
    };

    static constexpr int kDefaultTileSize = 128;

    RasterSurface() = default;
    RasterSurface(int width, int height, std::vector<std::uint8_t> pixels, std::string sourcePath = {})
    {
        setData(width, height, std::move(pixels), sourcePath);
    }

    bool empty() const
    {
        if (m_width <= 0 || m_height <= 0)
            return true;
        const std::size_t needed = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4ull;
        return m_pixels.size() < needed;
    }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int tileSize() const { return m_tileSize; }
    int tileColumns() const { return (m_width + m_tileSize - 1) / m_tileSize; }
    int tileRows() const { return (m_height + m_tileSize - 1) / m_tileSize; }
    const std::string& sourcePath() const { return m_sourcePath; }
    void setSourcePath(const std::string& path) { m_sourcePath = path; }

    const std::vector<std::uint8_t>& pixels() const { return m_pixels; }
    std::vector<std::uint8_t>& pixelsMutable()
    {
        ensureFullDirty();
        markDirty();
        return m_pixels;
    }

    void reset()
    {
        m_width = 0;
        m_height = 0;
        m_sourcePath.clear();
        m_pixels.clear();
        m_dirtyRects.clear();
        m_dirtyTiles.clear();
        markDirty();
    }

    void setData(int width, int height, std::vector<std::uint8_t> pixels, const std::string& sourcePath = {})
    {
        m_width = width;
        m_height = height;
        m_sourcePath = sourcePath;
        m_pixels = std::move(pixels);
        ensureFullDirty();
        markDirty();
    }

    bool writeSubRect(int x, int y, int width, int height, const std::uint8_t* rgba, int srcPitchBytes)
    {
        if (!rgba || width <= 0 || height <= 0 || empty())
            return false;

        if (x < 0 || y < 0 || x + width > m_width || y + height > m_height)
            return false;

        if (srcPitchBytes <= 0)
            srcPitchBytes = width * 4;

        const int dstPitch = m_width * 4;
        for (int row = 0; row < height; ++row)
        {
            const std::uint8_t* src = rgba + static_cast<std::size_t>(row) * static_cast<std::size_t>(srcPitchBytes);
            std::uint8_t* dst = m_pixels.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dstPitch)) + static_cast<std::size_t>(x * 4);
            std::copy(src, src + static_cast<std::size_t>(width * 4), dst);
        }

        markDirtyRect(x, y, width, height);
        return true;
    }

    std::uint64_t revision() const { return m_revision; }
    bool dirty() const { return m_dirty; }
    void clearDirty()
    {
        m_dirty = false;
        m_dirtyRects.clear();
        m_dirtyTiles.clear();
    }

    void markDirty()
    {
        ++m_revision;
        m_dirty = true;
    }

    void markDirtyRect(int x, int y, int width, int height)
    {
        DirtyRect rect = clampRect({x, y, width, height});
        if (rect.empty())
            return;

        appendDirtyRect(rect);
        appendDirtyTilesForRect(rect);
        markDirty();
    }

    const std::vector<DirtyRect>& dirtyRects() const { return m_dirtyRects; }
    const std::vector<DirtyTile>& dirtyTiles() const { return m_dirtyTiles; }
    std::size_t dirtyTileCount() const { return m_dirtyTiles.size(); }

    std::size_t byteSize() const
    {
        return sizeof(RasterSurface)
            + m_pixels.capacity() * sizeof(std::uint8_t)
            + m_sourcePath.capacity()
            + m_dirtyRects.capacity() * sizeof(DirtyRect)
            + m_dirtyTiles.capacity() * sizeof(DirtyTile);
    }

    std::shared_ptr<RasterSurface> clone() const
    {
        auto out = std::make_shared<RasterSurface>();
        out->m_width = m_width;
        out->m_height = m_height;
        out->m_tileSize = m_tileSize;
        out->m_sourcePath = m_sourcePath;
        out->m_pixels = m_pixels;
        out->m_revision = m_revision;
        out->m_dirty = m_dirty;
        out->m_dirtyRects = m_dirtyRects;
        out->m_dirtyTiles = m_dirtyTiles;
        return out;
    }

private:
    DirtyRect clampRect(DirtyRect rect) const
    {
        if (m_width <= 0 || m_height <= 0)
            return {};

        const int x0 = (std::max)(0, rect.x);
        const int y0 = (std::max)(0, rect.y);
        const int x1 = (std::min)(m_width, rect.x + rect.w);
        const int y1 = (std::min)(m_height, rect.y + rect.h);
        if (x1 <= x0 || y1 <= y0)
            return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    void ensureFullDirty()
    {
        m_dirtyRects.clear();
        m_dirtyTiles.clear();
        if (m_width > 0 && m_height > 0)
        {
            m_dirtyRects.push_back({0, 0, m_width, m_height});
            appendDirtyTilesForRect(m_dirtyRects.back());
        }
    }

    void appendDirtyRect(const DirtyRect& rect)
    {
        if (rect.empty())
            return;

        if (m_dirtyRects.empty())
        {
            m_dirtyRects.push_back(rect);
            return;
        }

        for (auto& existing : m_dirtyRects)
        {
            const bool overlaps = !(rect.x + rect.w <= existing.x ||
                                    existing.x + existing.w <= rect.x ||
                                    rect.y + rect.h <= existing.y ||
                                    existing.y + existing.h <= rect.y);
            const bool adjacent = (std::abs((rect.x + rect.w) - existing.x) <= 2) ||
                                   (std::abs((existing.x + existing.w) - rect.x) <= 2) ||
                                   (std::abs((rect.y + rect.h) - existing.y) <= 2) ||
                                   (std::abs((existing.y + existing.h) - rect.y) <= 2);
            if (overlaps || adjacent)
            {
                const int x0 = (std::min)(existing.x, rect.x);
                const int y0 = (std::min)(existing.y, rect.y);
                const int x1 = (std::max)(existing.x + existing.w, rect.x + rect.w);
                const int y1 = (std::max)(existing.y + existing.h, rect.y + rect.h);
                existing = {x0, y0, x1 - x0, y1 - y0};
                return;
            }
        }

        m_dirtyRects.push_back(rect);
    }

    void appendDirtyTilesForRect(const DirtyRect& rect)
    {
        if (rect.empty())
            return;

        const int x0 = rect.x / m_tileSize;
        const int y0 = rect.y / m_tileSize;
        const int x1 = (rect.x + rect.w - 1) / m_tileSize;
        const int y1 = (rect.y + rect.h - 1) / m_tileSize;
        for (int ty = y0; ty <= y1; ++ty)
        {
            for (int tx = x0; tx <= x1; ++tx)
            {
                DirtyTile tile{tx, ty};
                auto found = std::find(m_dirtyTiles.begin(), m_dirtyTiles.end(), tile);
                if (found == m_dirtyTiles.end())
                    m_dirtyTiles.push_back(tile);
            }
        }
    }

private:
    int m_width = 0;
    int m_height = 0;
    int m_tileSize = kDefaultTileSize;
    std::string m_sourcePath;
    std::vector<std::uint8_t> m_pixels;
    std::uint64_t m_revision = 1;
    bool m_dirty = true;
    std::vector<DirtyRect> m_dirtyRects;
    std::vector<DirtyTile> m_dirtyTiles;
};
