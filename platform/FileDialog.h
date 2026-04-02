/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        platform/FileDialog.h
   Module:      Platform
   Purpose:     File dialog entry points used by the app.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <string>

namespace platform {

    bool pickFolder(std::string& outFolder, const std::string& defaultFolder = "");
    bool pickOpenFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickOpenBrushOrProject(std::string& outPath, const std::string& defaultPath = "");
    bool pickOpenBrushProjectFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickOpenLuaFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickOpenAnyFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickSaveBrushFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickSaveBrushProjectFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickSaveLuaFile(std::string& outPath, const std::string& defaultPath = "");
    bool pickSaveAnyFile(std::string& outPath, const std::string& defaultPath = "");
}
