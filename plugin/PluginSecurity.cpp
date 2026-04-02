#include "PluginSecurity.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace strova::plugin
{
    bool isValidPluginId(const std::string& pluginId)
    {
        if (pluginId.size() < 3) return false;
        bool sawDot = false;
        for (const char c : pluginId)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || c == '.' || c == '_' || c == '-')
            {
                if (c == '.') sawDot = true;
                continue;
            }
            return false;
        }
        return sawDot;
    }

    std::filesystem::path normalizeRelativePath(const std::filesystem::path& path)
    {
        return path.lexically_normal();
    }

    bool isRelativeSafePath(const std::filesystem::path& path)
    {
        if (path.empty()) return false;
        if (path.is_absolute()) return false;
        const std::filesystem::path normalized = normalizeRelativePath(path);
        for (const auto& part : normalized)
        {
            const std::string s = part.string();
            if (s == "..") return false;
        }
        return true;
    }

    bool pathWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate)
    {
        std::error_code ec;
        const std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, ec);
        const std::filesystem::path normalizedCandidate = std::filesystem::weakly_canonical(candidate, ec);
        if (ec) return false;

        auto rootIt = normalizedRoot.begin();
        auto candIt = normalizedCandidate.begin();
        for (; rootIt != normalizedRoot.end(); ++rootIt, ++candIt)
        {
            if (candIt == normalizedCandidate.end() || *rootIt != *candIt)
                return false;
        }
        return true;
    }

    std::string shellEscapeArg(const std::string& value)
    {
#ifdef _WIN32
        std::string out = "'";
        for (char c : value)
        {
            if (c == '\'') out += "''";
            else out += c;
        }
        out += "'";
        return out;
#else
        std::string out = "'";
        for (char c : value)
        {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
#endif
    }
}
