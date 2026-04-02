/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        render/BrushRenderer.h
   Module:      Render
   Purpose:     Brush renderer state and paint entry points.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include "../core/Stroke.h"

namespace strova::brush { struct BrushPackage; }

class BrushRenderer
{
public:
    explicit BrushRenderer(SDL_Renderer* r);
    ~BrushRenderer(); 

    
    
    static void purgeCache(SDL_Renderer* r);

    
    
    inline void drawStroke(const Stroke& s)
    {
        drawStroke(s, 1.0f, 0.0f, 0.0f, 0, 0);
    }

    
    void drawStroke(
        const Stroke& s,
        float scale,
        float panX,
        float panY,
        int canvasX,
        int canvasY
    );

    void drawStrokeWithPackage(
        const Stroke& s,
        const strova::brush::BrushPackage* overridePkg,
        float scale,
        float panX,
        float panY,
        int canvasX,
        int canvasY
    );

    
    void drawStrokeExport(const Stroke& s);

private:
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* brushTex = nullptr;
    int           brushTexSize = 64;
};
