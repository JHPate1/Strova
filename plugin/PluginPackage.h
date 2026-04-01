#pragma once

#include "PluginAPI.h"

#include <filesystem>
#include <string>
#include <vector>

namespace strova::plugin
{
    struct ManifestPermissions
    {
        bool projectRead = false;
        bool projectWrite = false;
        bool pluginStorage = false;
        bool bulkDeleteProjectFiles = false;
        bool outsideProjectFs = false;

        std::uint64_t toMask() const;
    };

    struct PluginManifest
    {
        std::string format;
        int manifestVersion = 0;
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string engineMin;
        std::string engineMax;
        int abiVersion = 0;
        std::string description;
        std::string category;
        std::string icon;
        std::string docsPath;
        std::string entryWindows;
        std::string entryLinux;
        std::vector<std::string> capabilities;
        ManifestPermissions permissions{};

        std::uint64_t capabilityMask() const;
        std::filesystem::path platformEntryPath(const std::string& platform) const;
    };

    struct PackageValidationResult
    {
        bool ok = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        void addError(const std::string& message);
        void addWarning(const std::string& message);
        std::string firstMessage() const;
    };

    enum class PackageSourceType
    {
        Directory,
        Archive
    };

    struct DiscoveredPackage
    {
        PluginManifest manifest{};
        std::filesystem::path packageRoot;
        std::filesystem::path manifestPath;
        std::filesystem::path entryPath;
        PackageSourceType sourceType = PackageSourceType::Directory;
        PackageValidationResult validation{};
    };

    bool parseManifestText(const std::string& text, PluginManifest& outManifest, std::string& outErr);
    PackageValidationResult validateManifest(const PluginManifest& manifest, const std::filesystem::path& packageRoot, const std::string& appVersion, const std::string& platform);
    bool discoverPackageFromDirectory(const std::filesystem::path& directory, const std::string& appVersion, const std::string& platform, DiscoveredPackage& outPackage, std::string& outErr);
    bool installPackageFromSource(const std::filesystem::path& sourcePath,
                                  const std::filesystem::path& installRoot,
                                  const std::string& appVersion,
                                  const std::string& platform,
                                  DiscoveredPackage& outInstalled,
                                  std::string& outErr);
}
