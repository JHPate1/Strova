/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Gradient.h
   Module:      Core
   Purpose:     Gradient data and sampling helpers for paint tools.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include <array>


static constexpr int STROVA_MAX_GRADIENT_STOPS = 4;

struct GradientConfig
{
    bool enabled = false;
    int mode = 0;

    std::array<float, STROVA_MAX_GRADIENT_STOPS> stopPos{};
    std::array<SDL_Color, STROVA_MAX_GRADIENT_STOPS> stopColor{};

    GradientConfig()
    {
        
        stopPos = { 0.0f, 1.0f, 0.0f, 0.0f };

        stopColor[0] = SDL_Color{ 255, 0, 0, 255 };
        stopColor[1] = SDL_Color{ 0, 0, 255, 255 };
        stopColor[2] = SDL_Color{ 0, 0, 0, 255 };
        stopColor[3] = SDL_Color{ 0, 0, 0, 255 };
    }
};
