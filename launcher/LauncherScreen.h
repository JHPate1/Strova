/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        launcher/LauncherScreen.h
   Module:      Launcher
   Purpose:     Launcher screen state and helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <string>

union SDL_Event;

class App;

class LauncherScreen
{
public:
    void handleEvent(App& app, SDL_Event& e);
    void render(App& app, int w, int h);
};
