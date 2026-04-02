/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Sidebar.cpp
   Module:      Ui
   Purpose:     Sidebar layout and drawing logic.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "Sidebar.h"

SidebarUI::SidebarUI(DrawingEngine& eng) : engine(eng) {
    setup();
}

void SidebarUI::setup() {
    
    elements.push_back(new Button(820, 20, 100, 40, "Pen", { 50,50,50,255 }, [&]() { engine.setTool(ToolType::Pen); }));
    
    elements.push_back(new Button(820, 70, 100, 40, "Brush", { 80,80,200,255 }, [&]() { engine.setTool(ToolType::Brush); }));
    
    elements.push_back(new Button(820, 120, 100, 40, "Eraser", { 200,50,50,255 }, [&]() { engine.setTool(ToolType::Eraser); }));

    
    elements.push_back(
        new Slider(
            820, 180, 100, 20,
            1, 20,
            int(engine.getBrushSize()),
            [&](int v) { engine.setThickness(float(v)); }
        )
    );

    
    elements.push_back(new Button(820, 220, 30, 30, "", { 0,0,0,255 }, [&]() { engine.setColor({ 0,0,0,255 }); }));
    elements.push_back(new Button(860, 220, 30, 30, "", { 255,0,0,255 }, [&]() { engine.setColor({ 255,0,0,255 }); }));
    elements.push_back(new Button(820, 260, 30, 30, "", { 0,255,0,255 }, [&]() { engine.setColor({ 0,255,0,255 }); }));
    elements.push_back(new Button(860, 260, 30, 30, "", { 0,0,255,255 }, [&]() { engine.setColor({ 0,0,255,255 }); }));

    
}

void SidebarUI::draw(SDL_Renderer* renderer) {
    for (auto& e : elements) e->draw(renderer);
}

void SidebarUI::handleEvent(const SDL_Event& e) {
    for (auto& e1 : elements) e1->handleEvent(e);
}
