/* ============================================================================
   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/AppSettings.h
   Module:      Core
   Purpose:     Settings load and save helpers for lightweight app config.

   Notes:
   - Handles a tiny JSON-like settings file for update checks.
   - Keep field names stable unless you also migrate existing saved files.
   - Write format is intentionally simple so load stays dependency-free.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include "../platform/AppPaths.h"
#include "DebugLog.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

struct AppSettings
{
    bool updateCheckDaily = true;
    std::int64_t lastUpdateCheckEpoch = 0;
    bool persistentDocking = true;
};

namespace AppSettingsIO
{
    namespace detail
    {
        inline bool readAll(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return false;
            }

            std::ostringstream stream;
            stream << file.rdbuf();
            out = stream.str();
            return true;
        }

        inline bool writeAll(const std::filesystem::path& path, const std::string& text)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            std::ofstream file(path, std::ios::binary);
            if (!file)
            {
                return false;
            }

            file << text;
            return true;
        }

        inline bool findBoolField(const std::string& text, const std::string& key, bool& outValue)
        {
            const std::string marker = "\"" + key + "\"";
            std::size_t pos = text.find(marker);
            if (pos == std::string::npos)
            {
                return false;
            }

            pos = text.find(':', pos);
            if (pos == std::string::npos)
            {
                return false;
            }

            ++pos;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }

            if (text.compare(pos, 4, "true") == 0)
            {
                outValue = true;
                return true;
            }

            if (text.compare(pos, 5, "false") == 0)
            {
                outValue = false;
                return true;
            }

            return false;
        }

        inline bool findInt64Field(const std::string& text, const std::string& key, std::int64_t& outValue)
        {
            const std::string marker = "\"" + key + "\"";
            std::size_t pos = text.find(marker);
            if (pos == std::string::npos)
            {
                return false;
            }

            pos = text.find(':', pos);
            if (pos == std::string::npos)
            {
                return false;
            }

            ++pos;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }

            const std::size_t start = pos;
            if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
            {
                ++pos;
            }

            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }

            if (pos == start || (pos == start + 1 && (text[start] == '-' || text[start] == '+')))
            {
                return false;
            }

            try
            {
                outValue = std::stoll(text.substr(start, pos - start));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
    }

    inline bool save(const AppSettings& settings, std::string& outError)
    {
        outError.clear();

        const std::filesystem::path path = strova::paths::getSettingsPath();

        std::ostringstream stream;
        stream << "{\n";
        stream << "  \"updateCheckDaily\": " << (settings.updateCheckDaily ? "true" : "false") << ",\n";
        stream << "  \"lastUpdateCheckEpoch\": " << settings.lastUpdateCheckEpoch << ",\n";
        stream << "  \"persistentDocking\": " << (settings.persistentDocking ? "true" : "false") << "\n";
        stream << "}\n";

        strova::debug::logPath("AppSettings", "Saving settings file", path);
        if (!detail::writeAll(path, stream.str()))
        {
            outError = "Could not write settings file.";
            strova::debug::log("AppSettings", outError);
            return false;
        }

        strova::debug::log(
            "AppSettings",
            std::string("Saved settings. updateCheckDaily=") + (settings.updateCheckDaily ? "true" : "false") +
            ", lastUpdateCheckEpoch=" + std::to_string(settings.lastUpdateCheckEpoch) +
            ", persistentDocking=" + std::string(settings.persistentDocking ? "true" : "false"));
        return true;
    }

    inline bool load(AppSettings& outSettings, std::string& outError)
    {
        outError.clear();

        const std::filesystem::path path = strova::paths::getSettingsPath();
        strova::debug::logPath("AppSettings", "Loading settings file", path);

        if (!std::filesystem::exists(path))
        {
            strova::debug::log("AppSettings", "Settings file does not exist. Creating defaults.");
            outSettings = AppSettings{};
            return save(outSettings, outError);
        }

        std::string text;
        if (!detail::readAll(path, text))
        {
            outError = "Could not read settings file.";
            strova::debug::log("AppSettings", outError);
            return false;
        }

        AppSettings parsed{};
        bool flag = true;
        std::int64_t epoch = 0;

        if (detail::findBoolField(text, "updateCheckDaily", flag))
        {
            parsed.updateCheckDaily = flag;
        }

        if (detail::findInt64Field(text, "lastUpdateCheckEpoch", epoch))
        {
            parsed.lastUpdateCheckEpoch = epoch;
        }

        if (detail::findBoolField(text, "persistentDocking", flag))
        {
            parsed.persistentDocking = flag;
        }

        outSettings = parsed;

        strova::debug::log(
            "AppSettings",
            std::string("Loaded settings. updateCheckDaily=") + (outSettings.updateCheckDaily ? "true" : "false") +
            ", lastUpdateCheckEpoch=" + std::to_string(outSettings.lastUpdateCheckEpoch) +
            ", persistentDocking=" + std::string(outSettings.persistentDocking ? "true" : "false"));
        return true;
    }
}