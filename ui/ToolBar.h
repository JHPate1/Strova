/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ToolBar.h
   Module:      Ui
   Purpose:     Toolbar declarations used by the editor UI.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include "../core/Tool.h"

class ToolBar
{
public:
    ToolBar();

    void setSelectedTool(ToolType t) { selected = t; }
    ToolType getSelectedTool() const { return selected; }

    void handleClick(int mx, int my, const SDL_Rect& toolsArea);
    void handleWheel(const SDL_Event& e, int mx, int my, const SDL_Rect& toolsArea);
    void draw(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& toolsArea);

private:
    ToolType selected = ToolType::Brush;

    int scrollY = 0; 

    struct ToolBtn {
        ToolType tool;
        const char* label;
        const char* glyph; 
    };

    static const ToolBtn* buttons();
    static int buttonCount();

    bool pointInRect(int x, int y, const SDL_Rect& rc) const;
};
