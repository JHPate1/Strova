#include "PluginDocumentModel.h"

#include "PluginRegistry.h"
#include "PluginProjectData.h"

#include <algorithm>

namespace strova::plugin
{
    namespace
    {
        std::string resolveDisplayName(const ContentTypeRegistry* registry, const std::string& pluginId, const std::string& contentTypeId)
        {
            if (!registry)
                return contentTypeId.empty() ? pluginId : contentTypeId;
            for (const ContentTypeDescriptor& item : registry->items())
            {
                if (item.ownerPluginId == pluginId && item.id == contentTypeId)
                    return item.displayName.empty() ? item.id : item.displayName;
            }
            return contentTypeId.empty() ? pluginId : contentTypeId;
        }

        PluginRuntimeObject makeRuntimeObject(const PluginContentRecord& record, const ContentTypeRegistry* registry)
        {
            PluginRuntimeObject obj{};
            obj.instanceId = record.instanceId;
            obj.pluginId = record.pluginId;
            obj.contentTypeId = record.contentTypeId;
            obj.contentSchemaVersion = record.contentSchemaVersion;
            obj.pluginVersionSaved = record.pluginVersionSaved;
            obj.attachment = record.attachment;
            obj.payloadJson = record.payloadJson;
            obj.fallbackProxyJson = record.fallbackProxyJson;
            obj.unresolved = record.unresolved;
            obj.unresolvedReason = record.unresolvedReason;
            obj.status = RuntimeObjectStatus::Deleted;
            if (!record.instanceId.empty())
                obj.status = record.unresolved ? RuntimeObjectStatus::Unresolved : RuntimeObjectStatus::Resolved;
            obj.displayName = resolveDisplayName(registry, record.pluginId, record.contentTypeId);
            obj.attachmentKey = makeAttachmentKey(record.attachment);
            obj.dirty = false;
            return obj;
        }
    }

    std::string makeAttachmentKey(const PluginContentAttachment& attachment)
    {
        return attachment.kind + "|" + attachment.trackId + "|" + std::to_string(attachment.startFrame) + "|" +
               std::to_string(attachment.endFrame) + "|" + std::to_string(attachment.frame) + "|" + attachment.ownerId;
    }

    void rebuildRuntimeStoreFromProject(const ContentTypeRegistry* registry, const std::vector<PluginContentRecord>& contentRecords, PluginRuntimeStore& outStore)
    {
        outStore.objects.clear();
        outStore.objects.reserve(contentRecords.size());
        for (const PluginContentRecord& rec : contentRecords)
        {
            if (rec.pluginId.empty() || rec.instanceId.empty())
                continue;
            outStore.objects.push_back(makeRuntimeObject(rec, registry));
        }
        outStore.dirty = false;
        ++outStore.revision;
    }

    void syncRuntimeStoreToProject(const PluginRuntimeStore& store, std::vector<PluginContentRecord>& outContentRecords)
    {
        outContentRecords.clear();
        outContentRecords.reserve(store.objects.size());
        for (const PluginRuntimeObject& obj : store.objects)
        {
            if (obj.status == RuntimeObjectStatus::Deleted)
                continue;
            PluginContentRecord rec{};
            rec.pluginId = obj.pluginId;
            rec.contentTypeId = obj.contentTypeId;
            rec.pluginVersionSaved = obj.pluginVersionSaved;
            rec.contentSchemaVersion = obj.contentSchemaVersion;
            rec.instanceId = obj.instanceId;
            rec.attachment = obj.attachment;
            rec.payloadJson = obj.payloadJson;
            rec.fallbackProxyJson = obj.fallbackProxyJson;
            rec.unresolved = obj.unresolved;
            rec.unresolvedReason = obj.unresolvedReason;
            outContentRecords.push_back(std::move(rec));
        }
    }

    PluginRuntimeObject* findRuntimeObjectByInstanceId(PluginRuntimeStore& store, const std::string& instanceId)
    {
        for (PluginRuntimeObject& obj : store.objects)
        {
            if (obj.instanceId == instanceId && obj.status != RuntimeObjectStatus::Deleted)
                return &obj;
        }
        return nullptr;
    }

    const PluginRuntimeObject* findRuntimeObjectByInstanceId(const PluginRuntimeStore& store, const std::string& instanceId)
    {
        for (const PluginRuntimeObject& obj : store.objects)
        {
            if (obj.instanceId == instanceId && obj.status != RuntimeObjectStatus::Deleted)
                return &obj;
        }
        return nullptr;
    }

    std::vector<PluginRuntimeObject*> findRuntimeObjectsByAttachment(PluginRuntimeStore& store, const PluginContentAttachment& attachment, bool includeUnresolved)
    {
        std::vector<PluginRuntimeObject*> out;
        const std::string key = makeAttachmentKey(attachment);
        for (PluginRuntimeObject& obj : store.objects)
        {
            if (obj.status == RuntimeObjectStatus::Deleted)
                continue;
            if (!includeUnresolved && obj.unresolved)
                continue;
            if (obj.attachmentKey == key)
                out.push_back(&obj);
        }
        return out;
    }

    bool upsertRuntimeObject(PluginRuntimeStore& store, const PluginContentRecord& record, const ContentTypeRegistry* registry, std::string& outErr)
    {
        outErr.clear();
        if (record.pluginId.empty())
        {
            outErr = "Plugin runtime object is missing plugin id.";
            return false;
        }
        if (record.contentTypeId.empty())
        {
            outErr = "Plugin runtime object is missing content type id.";
            return false;
        }
        if (record.instanceId.empty())
        {
            outErr = "Plugin runtime object is missing instance id.";
            return false;
        }

        PluginRuntimeObject* existing = findRuntimeObjectByInstanceId(store, record.instanceId);
        if (existing)
        {
            *existing = makeRuntimeObject(record, registry);
            existing->dirty = true;
            store.dirty = true;
            ++store.revision;
            return true;
        }

        store.objects.push_back(makeRuntimeObject(record, registry));
        store.objects.back().dirty = true;
        store.dirty = true;
        ++store.revision;
        return true;
    }

    bool removeRuntimeObject(PluginRuntimeStore& store, const std::string& instanceId, std::string& outErr)
    {
        outErr.clear();
        if (instanceId.empty())
        {
            outErr = "Cannot remove runtime object with empty instance id.";
            return false;
        }
        for (PluginRuntimeObject& obj : store.objects)
        {
            if (obj.instanceId == instanceId && obj.status != RuntimeObjectStatus::Deleted)
            {
                obj.status = RuntimeObjectStatus::Deleted;
                obj.dirty = true;
                store.dirty = true;
                ++store.revision;
                return true;
            }
        }
        outErr = "Runtime object not found: " + instanceId;
        return false;
    }

    bool setPluginProjectStateEntry(std::vector<PluginProjectStateEntry>& entries, const std::string& pluginId, const std::string& stateJson, std::string& outErr)
    {
        outErr.clear();
        if (pluginId.empty())
        {
            outErr = "Plugin id is required for project state writes.";
            return false;
        }
        for (PluginProjectStateEntry& entry : entries)
        {
            if (entry.pluginId == pluginId)
            {
                entry.stateJson = stateJson.empty() ? "{}" : stateJson;
                return true;
            }
        }
        PluginProjectStateEntry entry{};
        entry.pluginId = pluginId;
        entry.stateJson = stateJson.empty() ? "{}" : stateJson;
        entries.push_back(std::move(entry));
        return true;
    }
}
