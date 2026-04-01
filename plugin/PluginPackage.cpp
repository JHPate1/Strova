#include "PluginPackage.h"

#include "PluginSecurity.h"
#include "../core/DebugLog.h"
#include "../core/SerializationUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace strova::plugin
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string trim(std::string s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            return s;
        }

        std::vector<int> parseVersionParts(const std::string& version)
        {
            std::vector<int> parts;
            std::string current;
            for (char c : version)
            {
                if (std::isdigit(static_cast<unsigned char>(c)))
                {
                    current.push_back(c);
                }
                else if (c == '.')
                {
                    if (!current.empty())
                    {
                        parts.push_back(std::atoi(current.c_str()));
                        current.clear();
                    }
                    else
                    {
                        parts.push_back(0);
                    }
                }
                else
                {
                    break;
                }
            }
            if (!current.empty())
                parts.push_back(std::atoi(current.c_str()));
            return parts;
        }

        int compareVersions(const std::string& a, const std::string& b)
        {
            const std::vector<int> av = parseVersionParts(a);
            const std::vector<int> bv = parseVersionParts(b);
            const std::size_t count = std::max(av.size(), bv.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                const int ai = i < av.size() ? av[i] : 0;
                const int bi = i < bv.size() ? bv[i] : 0;
                if (ai < bi) return -1;
                if (ai > bi) return 1;
            }
            return 0;
        }

        bool matchesVersionGlob(const std::string& version, const std::string& pattern)
        {
            const std::string trimmed = trim(pattern);
            const std::size_t star = trimmed.find('x');
            if (star == std::string::npos)
                return compareVersions(version, trimmed) == 0;

            const std::string prefix = trimmed.substr(0, star);
            return version.rfind(prefix, 0) == 0;
        }

        std::string findStringFieldInObject(const std::string& objectText, const char* key)
        {
            return strova::iojson::findStr(objectText, key, std::string());
        }

        bool findBoolFieldInObject(const std::string& objectText, const char* key, bool defaultValue)
        {
            size_t pos = 0;
            bool out = defaultValue;
            if (!strova::iojson::findKeyPosAfterColon(objectText, key, pos)) return defaultValue;
            return strova::iojson::parseBoolAt(objectText, pos, out) ? out : defaultValue;
        }

        bool findObjectSlice(const std::string& text, const char* key, std::string& outObject)
        {
            size_t pos = 0;
            if (!strova::iojson::findKeyPosAfterColon(text, key, pos)) return false;
            pos = text.find('{', pos);
            if (pos == std::string::npos) return false;
            const size_t end = strova::iojson::findMatchingBrace(text, pos);
            if (end == std::string::npos) return false;
            outObject = text.substr(pos, (end - pos) + 1);
            return true;
        }

        bool extractStringArray(const std::string& text, const char* key, std::vector<std::string>& outValues)
        {
            outValues.clear();
            const std::string arr = strova::iojson::extractArrayText(text, key);
            if (arr.empty()) return false;
            size_t i = 0;
            while (i < arr.size() && arr[i] != '[') ++i;
            if (i >= arr.size()) return false;
            ++i;
            while (i < arr.size())
            {
                strova::iojson::skipWs(arr, i);
                if (i < arr.size() && arr[i] == ']') break;
                std::string value;
                if (!strova::iojson::parseStringAt(arr, i, value)) return false;
                outValues.push_back(value);
                strova::iojson::skipWs(arr, i);
                if (i < arr.size() && arr[i] == ',') ++i;
            }
            return true;
        }

        void applyPermissionMask(ManifestPermissions& permissions, std::uint64_t mask)
        {
            if ((mask & permissionBit(Permission::ProjectRead)) != 0) permissions.projectRead = true;
            if ((mask & permissionBit(Permission::ProjectWrite)) != 0) permissions.projectWrite = true;
            if ((mask & permissionBit(Permission::PluginStorage)) != 0) permissions.pluginStorage = true;
            if ((mask & permissionBit(Permission::BulkDeleteProjectFiles)) != 0) permissions.bulkDeleteProjectFiles = true;
            if ((mask & permissionBit(Permission::OutsideProjectFs)) != 0) permissions.outsideProjectFs = true;
        }

        std::string cmdQuote(const std::string& value)
        {
#ifdef _WIN32
            std::string out = "\"";
            for (char c : value)
            {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            out += "\"";
            return out;
#else
            return shellEscapeArg(value);
#endif
        }

        bool runCommand(const std::string& cmd, std::string& outErr)
        {
            const int rc = std::system(cmd.c_str());
            if (rc != 0)
            {
                outErr = "Command failed: " + cmd;
                return false;
            }
            return true;
        }

        bool extractArchiveToDirectory(const fs::path& archivePath, const fs::path& outDir, std::string& outErr)
        {
            std::error_code ec;
            fs::create_directories(outDir, ec);
#ifdef _WIN32
            const fs::path tempArchive = outDir.parent_path() / "__strova_pkg_extract__.zip";
            fs::copy_file(archivePath, tempArchive, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                outErr = "Failed to prepare plugin archive for extraction: " + ec.message();
                return false;
            }

            const std::string cmd = std::string("cmd /C tar -xf ") +
                cmdQuote(tempArchive.string()) +
                " -C " +
                cmdQuote(outDir.string());
            const bool ok = runCommand(cmd, outErr);
            fs::remove(tempArchive, ec);
            return ok;
#else
            const std::string cmd = "unzip -o " + shellEscapeArg(archivePath.string()) + " -d " + shellEscapeArg(outDir.string());
            return runCommand(cmd, outErr);
#endif
        }

        bool findManifestRoot(const fs::path& start, fs::path& outRoot, fs::path& outManifestPath)
        {
            const std::array<const char*, 2> manifestNames{ "manifest.json", "plugin.json" };
            for (const char* name : manifestNames)
            {
                const fs::path candidate = start / name;
                if (fs::exists(candidate))
                {
                    outRoot = start;
                    outManifestPath = candidate;
                    return true;
                }
            }

            std::vector<fs::path> hits;
            std::error_code ec;
            for (fs::recursive_directory_iterator it(start, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it.depth() > 3)
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file()) continue;
                const std::string filename = it->path().filename().string();
                if (filename == "manifest.json" || filename == "plugin.json")
                    hits.push_back(it->path());
            }

            if (hits.size() == 1)
            {
                outManifestPath = hits.front();
                outRoot = outManifestPath.parent_path();
                return true;
            }
            return false;
        }

        bool copyDirectoryTree(const fs::path& from, const fs::path& to, std::string& outErr)
        {
            std::error_code ec;
            fs::create_directories(to.parent_path(), ec);
            fs::remove_all(to, ec);
            fs::create_directories(to, ec);
            if (ec)
            {
                outErr = "Failed to create plugin install directory: " + ec.message();
                return false;
            }
            fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                outErr = "Failed to copy plugin package into install directory: " + ec.message();
                return false;
            }
            return true;
        }
    }

    std::uint64_t ManifestPermissions::toMask() const
    {
        std::uint64_t mask = 0;
        if (projectRead) mask |= permissionBit(Permission::ProjectRead);
        if (projectWrite) mask |= permissionBit(Permission::ProjectWrite);
        if (pluginStorage) mask |= permissionBit(Permission::PluginStorage);
        if (bulkDeleteProjectFiles) mask |= permissionBit(Permission::BulkDeleteProjectFiles);
        if (outsideProjectFs) mask |= permissionBit(Permission::OutsideProjectFs);
        return mask;
    }

    std::uint64_t PluginManifest::capabilityMask() const
    {
        std::uint64_t mask = 0;
        for (const std::string& entry : capabilities)
            mask |= capabilityMaskFromString(entry);
        return mask;
    }

    fs::path PluginManifest::platformEntryPath(const std::string& platform) const
    {
        if (platform == "windows") return entryWindows;
        return entryLinux;
    }

    void PackageValidationResult::addError(const std::string& message)
    {
        errors.push_back(message);
        ok = false;
    }

    void PackageValidationResult::addWarning(const std::string& message)
    {
        warnings.push_back(message);
    }

    std::string PackageValidationResult::firstMessage() const
    {
        if (!errors.empty()) return errors.front();
        if (!warnings.empty()) return warnings.front();
        return ok ? "OK" : "Unknown validation failure";
    }


    bool parseManifestText(const std::string& text, PluginManifest& outManifest, std::string& outErr)
    {
        outManifest = {};
        outManifest.format = strova::iojson::findStr(text, "format", "");
        outManifest.manifestVersion = strova::iojson::findInt(text, "manifest_version", 0);
        outManifest.id = strova::iojson::findStr(text, "id", "");
        outManifest.name = strova::iojson::findStr(text, "name", "");
        outManifest.author = strova::iojson::findStr(text, "author", "");
        outManifest.version = strova::iojson::findStr(text, "version", "");
        outManifest.engineMin = strova::iojson::findStr(text, "engine_min", "");
        outManifest.engineMax = strova::iojson::findStr(text, "engine_max", "");
        outManifest.abiVersion = strova::iojson::findInt(text, "abi_version", 0);
        outManifest.description = strova::iojson::findStr(text, "description", "");
        outManifest.category = strova::iojson::findStr(text, "category", "");
        outManifest.icon = strova::iojson::findStr(text, "icon", "");
        outManifest.docsPath = strova::iojson::findStr(text, "docs", "");

        std::string entryObject;
        if (findObjectSlice(text, "entry", entryObject))
        {
            outManifest.entryWindows = findStringFieldInObject(entryObject, "windows");
            outManifest.entryLinux = findStringFieldInObject(entryObject, "linux");
        }
        else
        {
            const std::string flatEntry = strova::iojson::findStr(text, "entry", "");
            if (!flatEntry.empty())
            {
                outManifest.entryWindows = flatEntry;
                outManifest.entryLinux = flatEntry;
            }
        }

        extractStringArray(text, "capabilities", outManifest.capabilities);

        std::string permissionObject;
        if (findObjectSlice(text, "permissions", permissionObject))
        {
            outManifest.permissions.projectRead = findBoolFieldInObject(permissionObject, "project_read", false);
            outManifest.permissions.projectWrite = findBoolFieldInObject(permissionObject, "project_write", false);
            outManifest.permissions.pluginStorage = findBoolFieldInObject(permissionObject, "plugin_storage", false);
            outManifest.permissions.bulkDeleteProjectFiles = findBoolFieldInObject(permissionObject, "bulk_delete_project_files", false);
            outManifest.permissions.outsideProjectFs = findBoolFieldInObject(permissionObject, "outside_project_fs", false);
        }
        else
        {
            std::vector<std::string> permissionEntries;
            if (extractStringArray(text, "permissions", permissionEntries))
            {
                std::uint64_t mask = 0;
                for (const std::string& entry : permissionEntries)
                    mask |= permissionMaskFromString(entry);
                applyPermissionMask(outManifest.permissions, mask);
            }
        }

        const bool looksLikeLegacyPluginJson =
            outManifest.format.empty() &&
            !outManifest.id.empty() &&
            !outManifest.name.empty() &&
            !outManifest.version.empty() &&
            (!outManifest.entryWindows.empty() || !outManifest.entryLinux.empty());

        if (looksLikeLegacyPluginJson)
        {
            outManifest.format = "strovin";
            if (outManifest.manifestVersion <= 0)
                outManifest.manifestVersion = 1;

            if (outManifest.abiVersion <= 0)
            {
                const std::string apiVersionStr = strova::iojson::findStr(text, "api_version", "");
                outManifest.abiVersion = apiVersionStr.empty() ? (int)kAbiVersion : std::max(1, std::atoi(apiVersionStr.c_str()));
            }

            if (outManifest.engineMin.empty())
                outManifest.engineMin = "0.0.0";

            if (outManifest.capabilities.empty())
            {
                const std::string type = strova::iojson::findStr(text, "type", "");
                if (type == "panel")
                    outManifest.capabilities.push_back("dock_panels");
                else if (type == "command")
                    outManifest.capabilities.push_back("commands");
            }
        }

        if (outManifest.format.empty())
        {
            outErr = "Plugin package is missing manifest.json or a compatible plugin.json manifest.";
            return false;
        }
        return true;
    }

    PackageValidationResult validateManifest(const PluginManifest& manifest, const fs::path& packageRoot, const std::string& appVersion, const std::string& platform)
    {
        PackageValidationResult result{};
        result.ok = true;

        if (manifest.format != "strovin")
            result.addError("Plugin manifest format must be 'strovin'.");
        if (manifest.manifestVersion != 1)
            result.addError("Unsupported plugin manifest_version. Expected 1.");
        if (!isValidPluginId(manifest.id))
            result.addError("Plugin id is missing or invalid.");
        if (manifest.name.empty())
            result.addError("Plugin name is missing.");
        if (manifest.version.empty())
            result.addError("Plugin version is missing.");
        if (manifest.abiVersion != static_cast<int>(kAbiVersion))
            result.addError("Plugin abi_version does not match the host ABI.");
        if (manifest.engineMin.empty())
            result.addError("Plugin engine_min is required.");

        const fs::path entryRelative = manifest.platformEntryPath(platform);
        if (entryRelative.empty())
            result.addError("Plugin entry path is missing for platform '" + platform + "'.");
        else if (!isRelativeSafePath(entryRelative))
            result.addError("Plugin entry path must be relative and stay inside the package root.");
        else if (!fs::exists(packageRoot / entryRelative))
            result.addError("Plugin entry binary was not found inside the package.");

        if (!manifest.engineMin.empty() && compareVersions(appVersion, manifest.engineMin) < 0)
            result.addError("Current Strova version is lower than plugin engine_min.");

        if (!manifest.engineMax.empty() && !matchesVersionGlob(appVersion, manifest.engineMax) && compareVersions(appVersion, manifest.engineMax) > 0)
            result.addError("Current Strova version is higher than plugin engine_max.");

        for (const std::string& capability : manifest.capabilities)
        {
            if (capabilityMaskFromString(capability) == 0)
                result.addError("Unknown plugin capability: " + capability);
        }

        if (manifest.permissions.outsideProjectFs)
            result.addWarning("Plugin requests outside_project_fs. Normal builds should reject that permission later.");

        if (manifest.permissions.bulkDeleteProjectFiles && !manifest.permissions.projectWrite)
            result.addWarning("Plugin requests bulk_delete_project_files without project_write.");

        return result;
    }

    bool discoverPackageFromDirectory(const fs::path& directory,
        const std::string& appVersion,
        const std::string& platform,
        DiscoveredPackage& outPackage,
        std::string& outErr)
    {
        outPackage = {};
        fs::path packageRoot;
        fs::path manifestPath;
        if (!findManifestRoot(directory, packageRoot, manifestPath))
        {
            outErr = "Could not find a unique manifest.json or plugin.json in plugin package.";
            return false;
        }

        std::string manifestText;
        if (!strova::iojson::readTextFile(manifestPath, manifestText))
        {
            outErr = "Failed to read plugin manifest.json.";
            return false;
        }

        PluginManifest manifest{};
        if (!parseManifestText(manifestText, manifest, outErr))
            return false;

        outPackage.manifest = manifest;
        outPackage.packageRoot = packageRoot;
        outPackage.manifestPath = manifestPath;
        outPackage.entryPath = packageRoot / manifest.platformEntryPath(platform);
        outPackage.sourceType = PackageSourceType::Directory;
        outPackage.validation = validateManifest(manifest, packageRoot, appVersion, platform);
        if (!outPackage.validation.ok)
        {
            outErr = outPackage.validation.firstMessage();
            return false;
        }
        return true;
    }

    bool installPackageFromSource(const fs::path& sourcePath,
        const fs::path& installRoot,
        const std::string& appVersion,
        const std::string& platform,
        DiscoveredPackage& outInstalled,
        std::string& outErr)
    {
        outInstalled = {};
        std::error_code ec;
        fs::create_directories(installRoot, ec);

        if (fs::is_directory(sourcePath))
        {
            DiscoveredPackage pkg{};
            if (!discoverPackageFromDirectory(sourcePath, appVersion, platform, pkg, outErr))
                return false;
            const fs::path finalInstallPath = installRoot / pkg.manifest.id;
            if (!copyDirectoryTree(pkg.packageRoot, finalInstallPath, outErr))
                return false;
            return discoverPackageFromDirectory(finalInstallPath, appVersion, platform, outInstalled, outErr);
        }

        if (sourcePath.extension() != ".strovin")
        {
            outErr = "Plugin install source must be a package directory or a .strovin archive.";
            return false;
        }

        const fs::path tempRoot = installRoot.parent_path() / "_install_tmp" / (std::to_string(std::time(nullptr)) + std::string("_") + sourcePath.stem().string());
        fs::remove_all(tempRoot, ec);
        fs::create_directories(tempRoot, ec);
        if (!extractArchiveToDirectory(sourcePath, tempRoot, outErr))
        {
            fs::remove_all(tempRoot, ec);
            return false;
        }

        DiscoveredPackage pkg{};
        if (!discoverPackageFromDirectory(tempRoot, appVersion, platform, pkg, outErr))
        {
            fs::remove_all(tempRoot, ec);
            return false;
        }

        const fs::path finalInstallPath = installRoot / pkg.manifest.id;
        if (!copyDirectoryTree(pkg.packageRoot, finalInstallPath, outErr))
        {
            fs::remove_all(tempRoot, ec);
            return false;
        }

        fs::remove_all(tempRoot, ec);
        return discoverPackageFromDirectory(finalInstallPath, appVersion, platform, outInstalled, outErr);
    }
}
