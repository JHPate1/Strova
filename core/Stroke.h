/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Stroke.h
   Module:      Core
   Purpose:     Stroke data captured by the drawing engine.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#ifndef STROVA_CORE_STROKE_H
#define STROVA_CORE_STROKE_H

#include <vector>
#include <string>
#include <cstdint>
#include <SDL.h>
#include "Tool.h"
#include "Gradient.h"

   
struct StrokePoint
{
    float x = 0.0f;
    float y = 0.0f;

    
    float pressure = 1.0f;
};

struct Stroke
{
    SDL_Color color{ 0, 0, 0, 255 };
    float thickness = 2.0f;

    
    
    GradientConfig gradient{};

    ToolType tool = ToolType::Brush;
    std::string brushId = "strova.builtin.soft_round";
    std::string brushName = "Soft Round";
    int brushVersion = 1;
    bool brushMissing = false;
    std::uint64_t brushRuntimeRevision = 0;
    ToolSettings settings{};
    std::vector<StrokePoint> points;

    
    int fillTolerance = 35;   
    int fillGapClose = 0;    
};

#endif 
