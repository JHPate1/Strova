#pragma once

#include "PluginProjectData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace strova::plugin
{
    struct ContentTypeDescriptor;
    class ContentTypeRegistry;

    enum class RuntimeObjectStatus
    {
        Resolved,
        Unresolved,
        Deleted
    };

    struct PluginRuntimeObject
    {
        std::string instanceId;
        std::string pluginId;
        std::string contentTypeId;
        int contentSchemaVersion = 1;
        std::string pluginVersionSaved;
        PluginContentAttachment attachment{};
        std::string payloadJson;
        std::string fallbackProxyJson;
        bool unresolved = false;
        std::string unresolvedReason;
        RuntimeObjectStatus status = RuntimeObjectStatus::Resolved;
        std::string displayName;
        std::string attachmentKey;
        bool dirty = false;
    };

    struct PluginRuntimeStore
    {
        std::vector<PluginRuntimeObject> objects{};
        bool dirty = false;
        std::uint64_t revision = 0;
    };

    std::string makeAttachmentKey(const PluginContentAttachment& attachment);
    void rebuildRuntimeStoreFromProject(const ContentTypeRegistry* registry, const std::vector<PluginContentRecord>& contentRecords, PluginRuntimeStore& outStore);
    void syncRuntimeStoreToProject(const PluginRuntimeStore& store, std::vector<PluginContentRecord>& outContentRecords);
    PluginRuntimeObject* findRuntimeObjectByInstanceId(PluginRuntimeStore& store, const std::string& instanceId);
    const PluginRuntimeObject* findRuntimeObjectByInstanceId(const PluginRuntimeStore& store, const std::string& instanceId);
    std::vector<PluginRuntimeObject*> findRuntimeObjectsByAttachment(PluginRuntimeStore& store, const PluginContentAttachment& attachment, bool includeUnresolved);
    bool upsertRuntimeObject(PluginRuntimeStore& store, const PluginContentRecord& record, const ContentTypeRegistry* registry, std::string& outErr);
    bool removeRuntimeObject(PluginRuntimeStore& store, const std::string& instanceId, std::string& outErr);
    bool setPluginProjectStateEntry(std::vector<PluginProjectStateEntry>& entries, const std::string& pluginId, const std::string& stateJson, std::string& outErr);
}
