/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ProjectData.h
   Module:      Core
   Purpose:     Lightweight project-side data used by editor state.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Stroke.h"

struct StrokeData {
    uint32_t rgba = 0xFF000000;
    float size = 6.0f;
    int tool = 0;
    std::vector<StrokePoint> points;
};

struct FrameData {
    std::vector<StrokeData> strokes;
};


struct EngineTrackData {
    int id = 0;
    int kind = 1;
    std::string name = "Track";
    bool locked = false;
};

struct TimelineTrackData {
    int id = 0;
    int kind = 1;
    std::string name = "Track";
    bool locked = false;

    int engineTrackId = 0; 
};

struct TimelineClipData {
    int id = 0;
    int trackId = 0;
    int startFrame = 0;
    int lengthFrames = 1;
    std::string label = "Clip";
};

struct TimelineStateData {
    int nextTrackId = 1;
    int nextClipId = 1;

    int scrollX = 0;
    int scrollY = 0;

    float pxPerFrame = 14.0f;

    std::vector<TimelineTrackData> tracks;
    std::vector<TimelineClipData>  clips;

    inline void clear()
    {
        nextTrackId = 1;
        nextClipId = 1;
        scrollX = 0;
        scrollY = 0;
        pxPerFrame = 14.0f;
        tracks.clear();
        clips.clear();
    }
};

struct ProjectData {
    std::string folderPath;
    std::string name = "Untitled";
    int width = 1920;
    int height = 1080;
    int fps = 30;

    int currentFrame = 0;
    std::vector<FrameData> frames;

    int engineNextTrackId = 1;
    int engineActiveTrack = 0;
    std::vector<EngineTrackData> engineTracks;

    TimelineStateData timeline;

    
    inline void resetForNewProject(
        const std::string& newFolderPath,
        const std::string& newName,
        int w, int h, int newFps,
        int initialFrames = 1
    )
    {
        folderPath = newFolderPath;
        name = newName;
        width = w;
        height = h;
        fps = newFps;

        currentFrame = 0;

        frames.clear();
        if (initialFrames < 1) initialFrames = 1;
        frames.resize((size_t)initialFrames);

        engineNextTrackId = 1;
        engineActiveTrack = 0;
        engineTracks.clear();

        timeline.clear();
    }
};
