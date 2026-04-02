/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Project.h
   Module:      Core
   Purpose:     Project metadata shared by load, save, and UI flows.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include "../plugin/PluginProjectData.h"
#include "../plugin/PluginDocumentModel.h"

struct Project {
    std::string name = "Untitled";
    int width = 1920;
    int height = 1080;
    int fps = 30;

    
    std::string folderPath;

    std::vector<strova::plugin::PluginDependency> pluginDependencies{};
    std::vector<strova::plugin::PluginContentRecord> pluginContents{};
    std::vector<strova::plugin::PluginProjectStateEntry> pluginProjectStates{};
    strova::plugin::PluginRuntimeStore pluginRuntime{};

    std::string projectJsonPath() const {
        return folderPath.empty() ? "project.json" : (folderPath + "/project.json");
    }
};
