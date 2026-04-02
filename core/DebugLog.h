/* ============================================================================
   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/DebugLog.h
   Module:      Core
   Purpose:     Small logging helpers used across the project.

   Notes:
   - Keeps logging lightweight and header-only so it stays easy to use anywhere.
   - Log path resolution should stay stable because settings and startup code rely on it.
   - Console and debugger output mirror the file log to make startup issues easier to spot.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace strova::debug
{
    inline std::mutex& logMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    inline std::filesystem::path getPreferredLogPath()
    {
        std::error_code ec;

#ifdef _WIN32
        const char* appdata = std::getenv("APPDATA");
        if (appdata && *appdata)
        {
            const std::filesystem::path dir = std::filesystem::path(appdata) / "Strova";
            std::filesystem::create_directories(dir, ec);
            if (!ec)
            {
                return dir / "debug.log";
            }
        }
#else
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
        {
            const std::filesystem::path dir = std::filesystem::path(xdg) / "strova";
            std::filesystem::create_directories(dir, ec);
            if (!ec)
            {
                return dir / "debug.log";
            }
        }

        const char* home = std::getenv("HOME");
        if (home && *home)
        {
            const std::filesystem::path dir = std::filesystem::path(home) / ".config" / "strova";
            std::filesystem::create_directories(dir, ec);
            if (!ec)
            {
                return dir / "debug.log";
            }
        }
#endif

        return std::filesystem::current_path() / "debug.log";
    }

    inline std::string nowText()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);

        std::tm tmValue{};
#ifdef _WIN32
        localtime_s(&tmValue, &tt);
#else
        localtime_r(&tt, &tmValue);
#endif

        std::ostringstream stream;
        stream << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    inline void log(const std::string& scope, const std::string& message)
    {
        std::lock_guard<std::mutex> guard(logMutex());

        const std::string line = "[" + nowText() + "] [" + scope + "] " + message + "\n";
        const std::filesystem::path logFilePath = getPreferredLogPath();

        std::ofstream out(logFilePath, std::ios::app | std::ios::binary);
        if (out.is_open())
        {
            out << line;
        }

        std::cerr << line;

#ifdef _WIN32
        OutputDebugStringA(line.c_str());
#endif
    }

    inline void logPath(const std::string& scope, const std::string& label, const std::filesystem::path& path)
    {
        log(scope, label + ": " + path.string());
    }
}