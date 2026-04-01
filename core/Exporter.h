/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Exporter.h
   Module:      Core
   Purpose:     Export settings and entry points for render output.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include <string>

class DrawingEngine;
class BrushRenderer;

namespace strova::exporter
{
    struct Settings
    {
        int width = 1920;
        int height = 1080;
        int fps = 30;

        bool includeAlpha = false;
        SDL_Color bgColor{ 255,255,255,255 };

        bool flipVertical = false;

        int startFrame = 0;
        int endFrame = -1;

        int  mp4Crf = 18;
        int  mp4BitrateKbps = 0;
        int  mp4PresetIndex = 2;
        bool mp4UseYuv420 = true;
        bool mp4FastStart = true;
        bool mp4UseBaselineProfile = true;

        int  gifMaxColors = 256;
        bool gifDither = true;
        bool gifLoop = true;
        int  gifScalePct = 100;

        int  pngCompression = 6;
        bool pngInterlace = false;

        // Export orchestration.
        // Krita-style export is two-step for animated formats: render an image
        // sequence first, then encode with FFmpeg. These fields allow the
        // sequence stage to be retained or overridden without changing existing UI.
        std::string ffmpegPathOverride;
        bool keepSequenceForAnimatedFormats = false;
        bool preferSequenceEncodeForAnimatedFormats = true;
        int sequenceStartNumber = 0;
        std::string sequenceBaseName = "frame_";
    };

    struct ValidationResult
    {
        bool ok = true;
        std::string message;
    };

    ValidationResult validateSettings(
        const DrawingEngine& engine,
        const Settings& settings,
        const std::string& targetPath,
        bool requiresAnimatedEncoder);

    std::string resolveFFmpegExecutable(const Settings& settings);

    bool renderFrameToTexture(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        int frameIndex,
        SDL_Texture*& outTex,
        BrushRenderer& brush,
        const Settings& settings);

    bool exportPNGSequence(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& folder,
        const Settings& settings);

    bool exportGIF(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& filePath,
        const Settings& settings);

    bool exportMP4(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& filePath,
        const Settings& settings);
}
