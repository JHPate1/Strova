/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        main.cpp
   Module:      Root
   Purpose:     Entry point and startup wiring for the app.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#include "app/App.h"
#include "core/DebugLog.h"

#include <exception>
#include <string>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    strova::debug::log("main", "Process entry.");

    try
    {
        App app;
        strova::debug::log("main", "App object created.");

        if (!app.init())
        {
            strova::debug::log("main", "App initialization failed.");
            return -1;
        }

        strova::debug::log("main", "App initialized successfully. Entering run loop.");
        app.run();
        strova::debug::log("main", "Run loop exited. Shutting down.");
        app.shutdown();
        strova::debug::log("main", "Shutdown complete.");
        return 0;
    }
    catch (const std::exception& ex)
    {
        strova::debug::log("main", std::string("Unhandled std::exception: ") + ex.what());
    }
    catch (...)
    {
        strova::debug::log("main", "Unhandled unknown exception.");
    }

    return -1;
}
