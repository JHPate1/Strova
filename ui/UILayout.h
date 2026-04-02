/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/UILayout.h
   Module:      Ui
   Purpose:     Rect layout data used by editor UI code.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include <SDL.h>

struct UILayout
{
    SDL_Rect topBar{};
    SDL_Rect leftBar{};
    SDL_Rect bottomBar{};
    SDL_Rect canvas{};
};

UILayout calculateUILayout(int windowW, int windowH, float leftBarRatio);
