/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/IconButton.h
   Module:      Ui
   Purpose:     Icon button widget declarations.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>

class IconButton {
public:
    void setTexture(SDL_Texture* tex) { texture = tex; }
    void setRect(SDL_Rect r) { rect = r; }
    const SDL_Rect& getRect() const { return rect; }

    void setEnabled(bool e) { enabled = e; if (!enabled) pressed = false; }
    bool isEnabled() const { return enabled; }

    bool handleEvent(const SDL_Event& e, int mx, int my);

    bool hitTest(int mx, int my) const;

    void draw(SDL_Renderer* r, bool hovered) const;

    bool isPressed() const { return pressed; }

private:
    SDL_Texture* texture = nullptr; 
    SDL_Rect rect{ 0,0,0,0 };
    bool enabled = true;
    bool pressed = false;
};
