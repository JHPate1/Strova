/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Tool.h
   Module:      Core
   Purpose:     Tool definitions and brush option data.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <algorithm>
#include <string>

enum class ToolType : int
{
    Brush = 0,
    Pen = 1,
    Pencil = 2,
    Marker = 3,
    Airbrush = 4,
    Eraser = 5,
    Line = 6,
    Rect = 7,

    Ellipse = 8,
    Fill = 9,
    Select = 10,
    Eyedropper = 11,

    Calligraphy = 12,
    SoftEraser = 13,
    Smudge = 14,
    Blur = 15,
    Glow = 16,
    Ruler = 17
};


static constexpr int kToolTypeCount = 18;

inline int toolIndex(ToolType t)
{
    int i = (int)t;
    if (i < 0) i = 0;
    if (i >= kToolTypeCount) i = kToolTypeCount - 1;
    return i;
}


struct Tool
{
    ToolType type = ToolType::Brush;
    SDL_Color color{ 0, 0, 0, 255 };
    float thickness = 2.0f;

    Tool() = default;
    Tool(ToolType t, SDL_Color c, float th) : type(t), color(c), thickness(th) {}
};


struct ToolSettings
{
    
    float size = 18.0f;        
    float opacity = 1.0f;      
    float stabilizer = 0.15f;  

    
    float hardness = 0.85f;    
    float spacing = 0.10f;     
    float flow = 1.0f;         
    float scatter = 0.0f;      

    
    float strength = 0.5f;     

    
    float angleDeg = 0.0f;     
    float aspect = 0.5f;       

    
    
    float airRadius = 60.0f;   
    float airDensity = 0.5f;   

    
    float eraserStrength = 1.0f; 

    
    float smudgeStrength = 0.55f; 
    float blurRadius = 8.0f;      
    float jitterSize = 0.0f;
    float jitterOpacity = 0.0f;
    float jitterRotation = 0.0f;
    float spacingJitter = 0.0f;

    std::string brushId = "strova.builtin.soft_round";
    std::string brushDisplayName = "Soft Round";
    int brushVersion = 1;
    bool brushSupportsUserColor = true;
    bool brushSupportsGradient = true;

    
    int fillTolerance = 35;     

    void clamp()
    {
        auto fclamp = [](float v, float a, float b)
            {
                return std::max(a, std::min(b, v));
            };

        size = fclamp(size, 1.0f, 200.0f);
        opacity = fclamp(opacity, 0.0f, 1.0f);
        stabilizer = fclamp(stabilizer, 0.0f, 1.0f);

        hardness = fclamp(hardness, 0.0f, 1.0f);
        spacing = fclamp(spacing, 0.0f, 1.0f);
        flow = fclamp(flow, 0.0f, 1.0f);
        scatter = fclamp(scatter, 0.0f, 1.0f);

        strength = fclamp(strength, 0.0f, 1.0f);

        angleDeg = fclamp(angleDeg, 0.0f, 180.0f);
        aspect = fclamp(aspect, 0.0f, 1.0f);

        airRadius = fclamp(airRadius, 2.0f, 120.0f);
        airDensity = fclamp(airDensity, 0.0f, 1.0f);

        eraserStrength = fclamp(eraserStrength, 0.0f, 1.0f);

        smudgeStrength = fclamp(smudgeStrength, 0.0f, 1.0f);
        blurRadius = fclamp(blurRadius, 1.0f, 50.0f);
        jitterSize = fclamp(jitterSize, 0.0f, 1.0f);
        jitterOpacity = fclamp(jitterOpacity, 0.0f, 1.0f);
        jitterRotation = fclamp(jitterRotation, 0.0f, 1.0f);
        spacingJitter = fclamp(spacingJitter, 0.0f, 1.0f);

        fillTolerance = std::max(0, std::min(255, fillTolerance));
    }
};
