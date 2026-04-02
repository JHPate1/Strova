#pragma once

#include <filesystem>
#include <string>

namespace strova::plugin
{
    bool isValidPluginId(const std::string& pluginId);
    bool isRelativeSafePath(const std::filesystem::path& path);
    std::filesystem::path normalizeRelativePath(const std::filesystem::path& path);
    bool pathWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate);
    std::string shellEscapeArg(const std::string& value);
}
