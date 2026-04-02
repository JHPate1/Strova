/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        launcher/LauncherScreen.cpp
   Module:      Launcher
   Purpose:     Launcher screen drawing and project card behavior.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "LauncherScreen.h"
#include "../platform/AppPaths.h"

#include "../app/App.h"

#include <filesystem>
#include "../platform/FileDialog.h"
#include "../core/AppSettings.h"
#include "../core/DebugLog.h"

namespace fs = std::filesystem;

void LauncherScreen::handleEvent(App& app, SDL_Event& e)
{
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    app.launcherUi().handleEvent(e, mx, my);

    bool updateChecksEnabled = false;
    if (app.launcherUi().consumeUpdateChecksChanged(updateChecksEnabled))
    {
        strova::debug::log("LauncherScreen", std::string("Update Check Daily toggled to ") + (updateChecksEnabled ? "true" : "false"));
        app.appSettings.updateCheckDaily = updateChecksEnabled;
        std::string settingsErr;
        if (!AppSettingsIO::save(app.appSettings, settingsErr)) strova::debug::log("LauncherScreen", std::string("Saving settings after toggle failed: ") + settingsErr);
    }

    bool persistentDockingEnabled = false;
    if (app.launcherUi().consumePersistentDockingChanged(persistentDockingEnabled))
    {
        strova::debug::log("LauncherScreen", std::string("Persistent Docking toggled to ") + (persistentDockingEnabled ? "true" : "false"));
        app.appSettings.persistentDocking = persistentDockingEnabled;
        std::string settingsErr;
        if (!AppSettingsIO::save(app.appSettings, settingsErr)) strova::debug::log("LauncherScreen", std::string("Saving docking setting failed: ") + settingsErr);
    }

    const bool reqNew = app.launcherUi().requestNewProject;
    const bool reqOpenF = app.launcherUi().requestOpenFolder;
    const bool reqOpenP = app.launcherUi().requestOpenProject;
    const bool reqBrushCreator = app.launcherUi().requestOpenBrushCreator;
    const bool reqBrushProject = app.launcherUi().requestOpenBrushProject;
    const std::string reqPath = app.launcherUi().requestedProjectPath;
    const std::string reqBrushProjectPath = app.launcherUi().requestedBrushProjectPath;

    app.launcherUi().requestNewProject = false;
    app.launcherUi().requestOpenFolder = false;
    app.launcherUi().requestOpenProject = false;
    app.launcherUi().requestOpenBrushCreator = false;
    app.launcherUi().requestOpenBrushProject = false;
    app.launcherUi().requestedProjectPath.clear();
    app.launcherUi().requestedBrushProjectPath.clear();

    if (reqNew)
    {
        app.createDefaultProject();

        return;
    }

    if (reqBrushCreator)
    {
        app.openBrushCreatorWorkspace("", false);
        return;
    }

    if (reqBrushProject)
    {
        std::string path = reqBrushProjectPath;
        if (path.empty())
        {
            if (!platform::pickOpenBrushProjectFile(path))
                return;
        }
        if (!path.empty())
        {
            app.openBrushCreatorWorkspace(path, false);
        }
        return;
    }

    auto validateAndOpenPickedFolder = [&](const std::string& folder)
        {
            fs::path p = folder;

            if (p.extension() == ".strova" && fs::exists(p / "project.json"))
            {
                app.openProjectPath(p.string());
                return true;
            }

            if (fs::exists(p) && fs::is_directory(p))
            {
                for (auto& entry : fs::directory_iterator(p))
                {
                    if (!entry.is_directory()) continue;

                    fs::path sub = entry.path();
                    if (sub.extension() == ".strova" && fs::exists(sub / "project.json"))
                    {
                        app.openProjectPath(sub.string());
                        return true;
                    }
                }
            }

            strova::debug::log("LauncherScreen", "User picked invalid folder/project.");
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Strova",
                "Pick a valid *.strova project folder (must contain project.json).",
                app.windowHandle()
            );

            return false;
        };

    if (reqOpenF || reqOpenP)
    {
        if (!reqPath.empty())
        {
            strova::debug::log("LauncherScreen", std::string("Opening project path from launcher request: ") + reqPath);
            app.openProjectPath(reqPath);
            return;
        }

        std::string folder;
        if (platform::pickFolder(folder, strova::paths::getProjectsDir().string()))
        {
            validateAndOpenPickedFolder(folder);
        }

        return;
    }
}

void LauncherScreen::render(App& app, int w, int h)
{
    app.launcherUi().setUpdateChecksEnabled(app.appSettings.updateCheckDaily);
    app.launcherUi().setPersistentDockingEnabled(app.appSettings.persistentDocking);
    app.launcherUi().render(app.getRenderer(), w, h);
}
