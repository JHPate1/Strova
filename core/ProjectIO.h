/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ProjectIO.h
   Module:      Core
   Purpose:     Project serialization and file-system helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include "Project.h"
#include "Stroke.h"
#include <string>
#include <vector>

namespace strova { class TimelineWidget; }

namespace ProjectIO {

    bool loadFromFolder(const std::string& folderPath, Project& outProject, std::string& outError);
    bool saveToFolder(const Project& project, std::string& outError);
    bool isValidProjectFolder(const std::string& folderPath);

    bool saveTimelineToFolder(const std::string& folderPath,
        const strova::TimelineWidget& timeline,
        std::string& outError);

    bool loadTimelineFromFolder(const std::string& folderPath,
        strova::TimelineWidget& timeline,
        std::string& outError);

    bool loadFramesFromFolder(const std::string& folderPath,
        int frameCount,
        std::vector<std::vector<Stroke>>& outFrameStrokes,
        std::string& err);
}
