/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ToolBank.h
   Module:      Core
   Purpose:     Tool bank declarations used by the editor.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include "Tool.h"
#include "ToolRegistry.h"
#include <array>

class ToolBank
{
public:
    ToolBank();

    
    ToolSettings& get(ToolType t);
    const ToolSettings& get(ToolType t) const;

    
    void resetDefaults();

private:
    std::array<ToolSettings, kToolTypeCount> m_settings{};
};
