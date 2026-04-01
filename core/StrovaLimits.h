#pragma once

#include <algorithm>
#include <cstddef>

namespace strova::limits
{
    static constexpr int kMaxCanvasWidth = 8192;
    static constexpr int kMaxCanvasHeight = 8192;
    static constexpr int kMaxProjectFps = 240;

    static constexpr int kMaxTimelineFrames = 120000;
    static constexpr int kMaxTimelineTracks = 2048;
    static constexpr int kMaxDrawTracks = 1024;
    static constexpr int kMaxFlowTracks = 512;
    static constexpr int kMaxFlowLinkTracks = 512;
    static constexpr int kMaxAudioTracks = 256;

    static constexpr int kDefaultFlowMaxFrames = 1200;
    static constexpr int kMaxFlowGeneratedFrames = 12000;
    static constexpr int kMaxFlowSamplesPerCapture = 32768;
    static constexpr int kMaxFlowLinkSamplesPerCapture = 16384;
    static constexpr int kMaxFlowLinkClipsPerTrack = 4096;

    static constexpr std::size_t kMaxUndoBytes = 2ull * 1024ull * 1024ull * 1024ull;
    static constexpr std::size_t kMaxTextureCacheBytes = 1536ull * 1024ull * 1024ull;

    inline int clampCanvasWidth(int width)
    {
        return std::clamp(width, 1, kMaxCanvasWidth);
    }

    inline int clampCanvasHeight(int height)
    {
        return std::clamp(height, 1, kMaxCanvasHeight);
    }

    inline int clampProjectFps(int fps)
    {
        return std::clamp(fps, 1, kMaxProjectFps);
    }

    inline int clampTimelineFrames(int frames)
    {
        return std::clamp(frames, 1, kMaxTimelineFrames);
    }

    inline std::size_t clampTimelineFrameCount(std::size_t frames)
    {
        return (std::min)(frames, static_cast<std::size_t>(kMaxTimelineFrames));
    }
}
