/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Frame.h
   Module:      Core
   Purpose:     Frame-side data used by the drawing and playback systems.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include "Stroke.h"
#include <vector>

struct Frame {
    std::vector<Stroke> strokes;
};
