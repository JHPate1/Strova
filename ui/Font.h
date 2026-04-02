/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Font.h
   Module:      Ui
   Purpose:     Small bitmap font helpers used by the UI.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL_ttf.h>
#include <string>

class Font {
public:
    bool init(const char* path, int ptsize) {
        font = TTF_OpenFont(path, ptsize);
        return font != nullptr;
    }
    void shutdown() {
        if (font) { TTF_CloseFont(font); font = nullptr; }
    }
    TTF_Font* get() const { return font; }

private:
    TTF_Font* font = nullptr;
};
