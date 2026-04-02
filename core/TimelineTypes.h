/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/TimelineTypes.h
   Module:      Core
   Purpose:     Timeline-specific enums and small shared types.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ClipSourceType : std::uint8_t
{
    Frames = 0,
    Flow = 1
};

enum class BlendMode : std::uint8_t
{
    Normal = 0
};

struct ClipId { std::uint32_t v = 0; };
struct TrackId { std::uint32_t v = 0; };

struct TimelineClip
{
    ClipId id{};
    ClipSourceType sourceType = ClipSourceType::Frames;

    std::int32_t startFrame = 0;
    std::int32_t lengthFrames = 1;

    std::int32_t srcStartFrame = 0;

    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;

    bool enabled = true;
    bool locked = false;

    std::string name;
};

struct TimelineTrack
{
    TrackId id{};
    std::string name;

    bool visible = true;
    bool locked = false;

    std::vector<TimelineClip> clips;
};

struct TimelineSelection
{
    std::vector<ClipId> clips;
    TrackId primaryTrack{};
    ClipId primaryClip{};
    bool hasPrimary = false;

    void clear()
    {
        clips.clear();
        primaryTrack = {};
        primaryClip = {};
        hasPrimary = false;
    }
};

struct TimelineView
{
    float zoomPxPerFrame = 6.0f;
    int scrollX = 0;
    int scrollY = 0;
    int rowH = 46;

    int headerW = 180;
    int rulerH = 26;

    int snapPx = 10;
    bool snapping = true;
};

struct TimelineState
{
    std::int32_t playheadFrame = 0;
    TimelineView view{};
    TimelineSelection selection{};
    std::vector<TimelineTrack> tracks;
};
