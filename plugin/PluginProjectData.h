#pragma once

#include <string>
#include <vector>

struct Project;

namespace strova::plugin
{
    enum class DependencyStatus
    {
        Available,
        Missing,
        Disabled,
        Faulted,
        IncompatibleVersion,
        SchemaUnsupported
    };

    struct PluginDependency
    {
        std::string pluginId;
        std::string displayName;
        std::string savedWithPluginVersion;
        std::vector<int> requiredSchemaVersions;
        int contentInstances = 0;
        bool fallbackAvailable = false;
        std::string missingBehavior = "preserve_unresolved";
    };

    struct PluginContentAttachment
    {
        std::string kind;
        std::string trackId;
        int startFrame = 0;
        int endFrame = 0;
        int frame = 0;
        std::string ownerId;
    };

    struct PluginContentRecord
    {
        std::string pluginId;
        std::string contentTypeId;
        std::string pluginVersionSaved;
        int contentSchemaVersion = 1;
        std::string instanceId;
        PluginContentAttachment attachment{};
        std::string payloadJson;
        std::string fallbackProxyJson;
        bool unresolved = false;
        std::string unresolvedReason;
    };

    struct PluginProjectStateEntry
    {
        std::string pluginId;
        std::string stateJson;
    };

    struct DependencyIssue
    {
        std::string pluginId;
        std::string displayName;
        std::string savedVersion;
        int affectedItems = 0;
        bool fallbackAvailable = false;
        DependencyStatus status = DependencyStatus::Available;
        std::string message;
    };

    bool serializeProjectPluginData(const Project& project, std::string& outJsonAppend);
    void collectPluginDependencyUsage(Project& project);
    bool loadProjectPluginDataFromJson(const std::string& projectJson, Project& project, std::string& outErr);
}
