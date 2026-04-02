/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        render/Renderer.cpp
   Module:      Render
   Purpose:     Shared rendering helpers used by the app.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "Renderer.h"

Renderer::Renderer(SDL_Renderer* r) : renderer(r) {}

void Renderer::drawStroke(const Stroke& stroke) {
    SDL_SetRenderDrawColor(
        renderer,
        stroke.color.r,
        stroke.color.g,
        stroke.color.b,
        stroke.color.a
    );

    for (size_t i = 1; i < stroke.points.size(); i++) {
        SDL_RenderDrawLine(
            renderer,
            (int)stroke.points[i - 1].x,
            (int)stroke.points[i - 1].y,
            (int)stroke.points[i].x,
            (int)stroke.points[i].y
        );
    }
}
