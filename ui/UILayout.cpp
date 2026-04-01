/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/UILayout.cpp
   Module:      Ui
   Purpose:     UI layout calculations for the main editor view.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#include "UILayout.h"

#include <algorithm>

UILayout calculateUILayout(int w, int h, float leftBarRatio)
{
    UILayout ui;

    const int topH = static_cast<int>(h * 0.06f);
    const int bottomH = static_cast<int>(h * 0.20f);
    int leftW = static_cast<int>(w * leftBarRatio);
    leftW = std::clamp(leftW, 60, 300);

    ui.topBar = {0, 0, w, topH};
    ui.leftBar = {0, topH, leftW, h - topH - bottomH};
    ui.bottomBar = {0, h - bottomH, w, bottomH};
    ui.canvas = {leftW, topH, w - leftW, h - topH - bottomH};

    return ui;
}
