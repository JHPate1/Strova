/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        editor/Editor.h
   Module:      Editor
   Purpose:     Editor state and public interaction points.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>

class App;
enum class ExportMenuChoice
{
    None,
    MP4,
    PNGSequence,
    GIF
};

struct ExportMenuState
{
    bool open = false;
    SDL_Rect buttonRect{ 0,0,0,0 };
};

static ExportMenuState gExportMenu;

class Editor
{
public:
    void handleEvent(App& app, SDL_Event& e);
    void update(App& app, double dt);
    void render(App& app, int w, int h);
};
