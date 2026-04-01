/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        platform/AppPaths.h
   Module:      Platform
   Purpose:     Platform-aware paths for settings, logs, and app data.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include "../core/DebugLog.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <limits.h>
#  include <unistd.h>
#endif

namespace strova::paths
{
    inline std::filesystem::path getExecutablePath()
    {
#ifdef _WIN32
        wchar_t buffer[4096]{};
        const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
        if (len == 0)
        {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(buffer);
#else
        char buffer[PATH_MAX]{};
        const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len <= 0)
        {
            return std::filesystem::current_path();
        }

        buffer[len] = '\0';
        return std::filesystem::path(buffer);
#endif
    }

    inline std::filesystem::path getExecutableDir()
    {
        const std::filesystem::path path = getExecutablePath();
        if (path.has_parent_path())
        {
            return path.parent_path();
        }
        return std::filesystem::current_path();
    }

    inline std::filesystem::path getAppDataDir()
    {
#ifdef _WIN32
        const char* roaming = std::getenv("APPDATA");
        if (roaming && *roaming)
        {
            return std::filesystem::path(roaming) / "Strova";
        }

        const char* local = std::getenv("LOCALAPPDATA");
        if (local && *local)
        {
            return std::filesystem::path(local) / "Strova";
        }

        return std::filesystem::current_path() / "StrovaData";
#else
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
        {
            return std::filesystem::path(xdg) / "strova";
        }

        const char* home = std::getenv("HOME");
        if (home && *home)
        {
            return std::filesystem::path(home) / ".config" / "strova";
        }

        return std::filesystem::current_path() / ".strova";
#endif
    }



    inline std::filesystem::path getAssetsDir()
    {
        return getAppDataDir() / "assets";
    }

    inline std::filesystem::path getProjectsDir()
    {
        return getAppDataDir() / "projects";
    }

    inline std::filesystem::path getExportsDir()
    {
        return getAppDataDir() / "exports";
    }

    inline std::filesystem::path getPluginsDir()
    {
        return getAppDataDir() / "plugins";
    }

    inline std::filesystem::path getInstalledPluginsDir()
    {
        return getPluginsDir() / "installed";
    }

    inline std::filesystem::path getPluginStorageDir()
    {
        return getPluginsDir() / "storage";
    }

    inline std::filesystem::path resolveAssetPath(const std::filesystem::path& relativePath)
    {
        std::filesystem::path rel = relativePath.is_absolute() ? relativePath.filename() : relativePath;
        if (!rel.empty() && rel.begin() != rel.end())
        {
            auto it = rel.begin();
            if (it->string() == "assets")
            {
                std::filesystem::path trimmed;
                ++it;
                for (; it != rel.end(); ++it)
                {
                    trimmed /= *it;
                }
                if (!trimmed.empty())
                {
                    rel = trimmed;
                }
            }
        }
        const std::filesystem::path appDataAsset = getAssetsDir() / rel;
        if (std::filesystem::exists(appDataAsset))
        {
            return appDataAsset;
        }

        const std::filesystem::path exeAsset = getExecutableDir() / "assets" / rel;
        if (std::filesystem::exists(exeAsset))
        {
            return exeAsset;
        }

        return appDataAsset;
    }

    inline std::filesystem::path getSettingsPath()
    {
        const std::filesystem::path path = getAppDataDir() / "settings.json";
        strova::debug::logPath("AppPaths", "Resolved settings path", path);
        return path;
    }

    inline std::string getPlatformKey()
    {
#ifdef _WIN32
        return "windows";
#else
        return "linux";
#endif
    }
}
