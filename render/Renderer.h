/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        render/Renderer.h
   Module:      Render
   Purpose:     Renderer-facing helper declarations.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include "../core/Stroke.h"

class Renderer {
public:
    Renderer(SDL_Renderer* renderer);
    void drawStroke(const Stroke& stroke);

private:
    SDL_Renderer* renderer;
};
