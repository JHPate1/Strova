/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ToolBank.cpp
   Module:      Core
   Purpose:     Built-in tool registration and setup.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "ToolBank.h"
#include "ToolRegistry.h"

static ToolSettings defaultsFor(ToolType t)
{
    ToolSettings s{};

    
    s.size = 18.0f;
    s.opacity = 1.0f;
    s.stabilizer = 0.15f;

    s.hardness = 0.85f;
    s.spacing = 0.10f;
    s.flow = 1.0f;
    s.scatter = 0.0f;

    s.angleDeg = 0.0f;
    s.strength = 0.5f;

    s.fillTolerance = 35;

    switch (t)
    {
    case ToolType::Brush:
        s.size = 18.0f;
        s.hardness = 0.80f;
        s.spacing = 0.10f;
        s.stabilizer = 0.18f;
        s.brushId = "strova.builtin.soft_round";
        s.brushDisplayName = "Soft Round";
        break;

    case ToolType::Pencil:
        s.size = 6.0f;
        s.hardness = 1.0f;
        s.spacing = 0.18f;
        s.stabilizer = 0.05f;
        break;

    case ToolType::Pen:
        s.size = 8.0f;
        s.hardness = 0.95f;
        s.spacing = 0.12f;
        s.stabilizer = 0.10f;
        break;

    case ToolType::Marker:
        s.size = 22.0f;
        s.hardness = 0.65f;
        s.opacity = 0.85f;
        s.spacing = 0.08f;
        s.stabilizer = 0.12f;
        break;

    case ToolType::Airbrush:
        s.size = 60.0f;
        s.hardness = 0.25f;
        s.opacity = 0.35f;
        s.flow = 0.55f;
        s.spacing = 0.06f;
        s.stabilizer = 0.08f;
        break;

    case ToolType::Calligraphy:
        s.size = 26.0f;
        s.hardness = 0.9f;
        s.angleDeg = 25.0f;
        s.spacing = 0.10f;
        s.stabilizer = 0.18f;
        break;

    case ToolType::Eraser:
        s.size = 38.0f;
        s.hardness = 0.65f;
        s.opacity = 1.0f;
        s.stabilizer = 0.08f;
        break;

    case ToolType::SoftEraser:
        s.size = 50.0f;
        s.hardness = 0.25f;
        s.opacity = 1.0f;
        s.stabilizer = 0.08f;
        break;

    case ToolType::Smudge:
        s.size = 40.0f;
        s.strength = 0.55f;
        s.stabilizer = 0.08f;
        break;

    case ToolType::Blur:
        s.size = 45.0f;
        s.strength = 0.45f;
        s.stabilizer = 0.08f;
        break;

    case ToolType::Glow:
        s.size = 28.0f;
        s.opacity = 0.9f;
        s.hardness = 0.45f;
        s.flow = 0.75f;
        s.spacing = 0.09f;
        s.stabilizer = 0.14f;
        break;

    case ToolType::Ruler:
        s.size = 2.0f;
        s.opacity = 1.0f;
        s.stabilizer = 0.0f;
        break;

    case ToolType::Fill:
        s.fillTolerance = 35;
        break;

        
        
    case ToolType::Line:
        [[fallthrough]];
    case ToolType::Rect:
        [[fallthrough]];
    case ToolType::Ellipse:
        [[fallthrough]];
    case ToolType::Select:
        [[fallthrough]];
    case ToolType::Eyedropper:
        [[fallthrough]];
    default:
        break;
    }

    s.clamp();
    return s;
}

ToolBank::ToolBank()
{
    resetDefaults();
}

ToolSettings& ToolBank::get(ToolType t)
{
    return m_settings[(size_t)toolIndex(t)];
}

const ToolSettings& ToolBank::get(ToolType t) const
{
    return m_settings[(size_t)toolIndex(t)];
}

void ToolBank::resetDefaults()
{
    for (ToolType t : strova::tools::orderedToolTypes())
    {
        m_settings[(size_t)toolIndex(t)] = defaultsFor(t);
    }
}
