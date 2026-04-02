/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Sidebar.h
   Module:      Ui
   Purpose:     Sidebar widget declarations.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include "../core/DrawingEngine.h"
#include "../ui/UI.h"
#include <vector>

class SidebarUI {
public:
    SidebarUI(DrawingEngine& engine);

    void draw(SDL_Renderer* renderer);
    void handleEvent(const SDL_Event& e);

private:
    DrawingEngine& engine;
    std::vector<UIElement*> elements;

    void setup();
};
