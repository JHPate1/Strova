#include "PluginManager.h"
#include "PluginFlowBridge.h"

#include "../core/DebugLog.h"
#include "../core/Project.h"
#include "../app/App.h"
#include "../core/SerializationUtils.h"
#include "../platform/AppPaths.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <cstring>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace strova::plugin
{
    namespace
    {
        namespace fs = std::filesystem;

        const char* runtimeStateName(RuntimeState state)
        {
            switch (state)
            {
            case RuntimeState::Discovered: return "Discovered";
            case RuntimeState::Disabled: return "Disabled";
            case RuntimeState::Loaded: return "Loaded";
            case RuntimeState::Faulted: return "Faulted";
            case RuntimeState::Invalid: return "Invalid";
            case RuntimeState::Missing: return "Missing";
            }
            return "Unknown";
        }

        std::string joinErrors(const std::vector<std::string>& errors)
        {
            std::string out;
            for (std::size_t i = 0; i < errors.size(); ++i)
            {
                if (i) out += " | ";
                out += errors[i];
            }
            return out;
        }

        RuntimeRecord* findRuntimeRecord(std::vector<RuntimeRecord>& records, const std::string& pluginId)
        {
            for (RuntimeRecord& record : records)
                if (record.package.manifest.id == pluginId)
                    return &record;
            return nullptr;
        }

        const RuntimeRecord* findRuntimeRecord(const std::vector<RuntimeRecord>& records, const std::string& pluginId)
        {
            for (const RuntimeRecord& record : records)
                if (record.package.manifest.id == pluginId)
                    return &record;
            return nullptr;
        }

        bool runtimeHasPermission(const RuntimeRecord* record, Permission permission)
        {
            return record && hasPermission(record->query.permissionMask, permission);
        }

        bool ensurePluginPermission(const RuntimeRecord* record, Permission permission, const std::string& action, std::string& outErr)
        {
            if (runtimeHasPermission(record, permission))
                return true;
            outErr = "Plugin " + (record ? record->package.manifest.id : std::string("<unknown>")) +
                     " is not permitted to " + action + ". Missing permission: " + permissionName(permission);
            return false;
        }

        bool copyTextBufferOut(const StrovaPluginTextBuffer& buffer, std::string& outText)
        {
            outText.clear();
            if (!buffer.data || buffer.capacity == 0) return buffer.written == 0;
            const std::size_t count = std::min<std::size_t>(buffer.written, buffer.capacity);
            outText.assign(buffer.data, count);
            return true;
        }

        uint32_t mapPanelEventType(const SDL_Event& e)
        {
            switch (e.type)
            {
            case SDL_MOUSEBUTTONDOWN: return STROVA_PLUGIN_PANEL_EVENT_MOUSE_DOWN;
            case SDL_MOUSEBUTTONUP: return STROVA_PLUGIN_PANEL_EVENT_MOUSE_UP;
            case SDL_MOUSEMOTION: return STROVA_PLUGIN_PANEL_EVENT_MOUSE_MOVE;
            case SDL_MOUSEWHEEL: return STROVA_PLUGIN_PANEL_EVENT_MOUSE_WHEEL;
            case SDL_KEYDOWN: return STROVA_PLUGIN_PANEL_EVENT_KEY_DOWN;
            case SDL_KEYUP: return STROVA_PLUGIN_PANEL_EVENT_KEY_UP;
            case SDL_TEXTINPUT: return STROVA_PLUGIN_PANEL_EVENT_TEXT_INPUT;
            default: return STROVA_PLUGIN_PANEL_EVENT_NONE;
            }
        }

        std::string buildLayerTreeJson(const App& app)
        {
            const auto& tree = app.activeFrameLayerTree();
            std::string json = "{\"nodes\":[";
            bool first = true;
            for (const auto& node : tree.getNodes())
            {
                if (!first) json += ",";
                first = false;
                json += "{";
                json += "\"id\":" + std::to_string(node.id);
                json += ",\"parent_id\":" + std::to_string(node.parentId);
                json += ",\"track_id\":" + std::to_string(node.trackId);
                json += ",\"is_group\":" + std::string(node.isGroup ? "true" : "false");
                json += ",\"expanded\":" + std::string(node.expanded ? "true" : "false");
                json += ",\"name\":\"" + strova::iojson::jsonEscape(node.name) + "\"";
                json += "}";
            }
            json += "],\"selection\":[";
            first = true;
            for (int id : tree.getSelection())
            {
                if (!first) json += ",";
                first = false;
                json += std::to_string(id);
            }
            json += "],\"primary_track_id\":" + std::to_string(tree.primarySelectedTrackId());
            json += "}";
            return json;
        }

        std::string buildSelectionJson(const App& app)
        {
            const auto& tree = app.activeFrameLayerTree();
            std::string json = "{\"track_ids\":[";
            bool first = true;
            for (int id : tree.selectedTrackIds())
            {
                if (!first) json += ",";
                first = false;
                json += std::to_string(id);
            }
            json += "],\"primary_track_id\":" + std::to_string(tree.primarySelectedTrackId()) + "}";
            return json;
        }

        std::string extractRawJsonValue(const std::string& objectText, const char* key)
        {
            if (!key || !*key) return {};
            std::size_t pos = 0;
            if (!strova::iojson::findKeyPosAfterColon(objectText, key, pos)) return {};
            strova::iojson::skipWs(objectText, pos);
            if (pos >= objectText.size()) return {};

            const char c = objectText[pos];
            if (c == '{')
            {
                const std::size_t end = strova::iojson::findMatchingBrace(objectText, pos);
                return end == std::string::npos ? std::string() : objectText.substr(pos, end - pos + 1);
            }
            if (c == '[')
            {
                int depth = 0;
                bool inStr = false;
                std::size_t end = pos;
                for (; end < objectText.size(); ++end)
                {
                    const char ch = objectText[end];
                    if (ch == '"' && (end == 0 || objectText[end - 1] != '\\'))
                        inStr = !inStr;
                    if (inStr) continue;
                    if (ch == '[') ++depth;
                    else if (ch == ']')
                    {
                        --depth;
                        if (depth == 0) break;
                    }
                }
                return end >= objectText.size() ? std::string() : objectText.substr(pos, end - pos + 1);
            }
            if (c == '"')
            {
                std::string parsed;
                std::size_t i = pos;
                if (!strova::iojson::parseStringAt(objectText, i, parsed))
                    return {};
                return std::string("\"") + strova::iojson::jsonEscape(parsed) + "\"";
            }

            std::size_t end = pos;
            while (end < objectText.size() && objectText[end] != ',' && objectText[end] != '}')
                ++end;
            return objectText.substr(pos, end - pos);
        }

        bool parseAttachmentOverride(const std::string& resultJson, PluginContentAttachment& attachment)
        {
            std::string attachmentObject = extractRawJsonValue(resultJson, "attachment");
            if (attachmentObject.empty())
                return false;

            attachment.kind = strova::iojson::findStr(attachmentObject, "kind", attachment.kind);
            attachment.trackId = strova::iojson::findStr(attachmentObject, "track_id", attachment.trackId);
            attachment.startFrame = strova::iojson::findInt(attachmentObject, "start_frame", attachment.startFrame);
            attachment.endFrame = strova::iojson::findInt(attachmentObject, "end_frame", attachment.endFrame);
            attachment.frame = strova::iojson::findInt(attachmentObject, "frame", attachment.frame);
            attachment.ownerId = strova::iojson::findStr(attachmentObject, "owner_id", attachment.ownerId);
            return true;
        }

        bool resolveOutputTypeId(const RegistryHub& registries, const std::string& pluginId, const std::string& requestedOutputTypeId,
            const std::string& fallbackOutputTypeId, std::string& outOutputTypeId, std::string& outErr)
        {
            outErr.clear();
            outOutputTypeId.clear();

            auto accepts = [&](const std::string& candidate) -> bool
            {
                if (candidate.empty()) return false;
                const ContentTypeDescriptor* type = registries.contentTypes.findById(candidate);
                return type && type->ownerPluginId == pluginId;
            };

            if (accepts(requestedOutputTypeId))
            {
                outOutputTypeId = requestedOutputTypeId;
                return true;
            }
            if (accepts(fallbackOutputTypeId))
            {
                outOutputTypeId = fallbackOutputTypeId;
                return true;
            }

            outErr = "Processor output type is not registered to the owning plugin.";
            return false;
        }

        bool commitFlowProcessorResult(Project& project, const RegistryHub& registries, const std::string& pluginId,
            const std::string& processorId, const std::string& fallbackOutputTypeId, int fallbackSchemaVersion,
            const std::string& fallbackPluginVersion, const PluginContentAttachment& defaultAttachment,
            const std::string& resultJson, bool isFlowLink, std::string& outInstanceId, std::string& outErr)
        {
            outErr.clear();
            outInstanceId.clear();

            std::string outputTypeId;
            const std::string requestedOutputTypeId = strova::iojson::findStr(resultJson, "output_type_id", fallbackOutputTypeId);
            if (!resolveOutputTypeId(registries, pluginId, requestedOutputTypeId, fallbackOutputTypeId, outputTypeId, outErr))
                return false;

            const int schemaVersion = strova::iojson::findInt(resultJson, "content_schema_version", fallbackSchemaVersion > 0 ? fallbackSchemaVersion : 1);
            const std::string pluginVersionSaved = strova::iojson::findStr(resultJson, "plugin_version_saved", fallbackPluginVersion);

            PluginContentAttachment attachment = defaultAttachment;
            attachment.kind = strova::iojson::findStr(resultJson, "attachment_kind", attachment.kind);
            attachment.trackId = strova::iojson::findStr(resultJson, "attachment_track_id", attachment.trackId);
            attachment.startFrame = strova::iojson::findInt(resultJson, "attachment_start_frame", attachment.startFrame);
            attachment.endFrame = strova::iojson::findInt(resultJson, "attachment_end_frame", attachment.endFrame);
            attachment.frame = strova::iojson::findInt(resultJson, "attachment_frame", attachment.frame);
            attachment.ownerId = strova::iojson::findStr(resultJson, "attachment_owner_id", attachment.ownerId);
            parseAttachmentOverride(resultJson, attachment);

            if (attachment.kind.empty())
                attachment.kind = isFlowLink ? "flowlink_segment" : "flow_segment";
            if (attachment.ownerId.empty())
                attachment.ownerId = processorId;
            if (attachment.endFrame < attachment.startFrame)
                std::swap(attachment.startFrame, attachment.endFrame);

            std::string payloadJson = extractRawJsonValue(resultJson, "payload");
            if (payloadJson.empty())
                payloadJson = "{}";

            std::string fallbackProxyJson = extractRawJsonValue(resultJson, "fallback_proxy");
            if (fallbackProxyJson.empty())
                fallbackProxyJson = "null";

            if (isFlowLink)
            {
                return upsertFlowLinkProcessorAugmentation(project, pluginId, outputTypeId, schemaVersion,
                    pluginVersionSaved, processorId, attachment, payloadJson, fallbackProxyJson, outInstanceId, outErr);
            }

            return upsertFlowProcessorAugmentation(project, pluginId, outputTypeId, schemaVersion,
                pluginVersionSaved, processorId, attachment, payloadJson, fallbackProxyJson, outInstanceId, outErr);
        }
    }

    struct Manager::DynamicLibrary
    {
#ifdef _WIN32
        HMODULE handle = nullptr;
#else
        void* handle = nullptr;
#endif
        DynamicLibrary() = default;
        ~DynamicLibrary() { close(); }

        bool open(const std::filesystem::path& path, std::string& outErr)
        {
#ifdef _WIN32
            handle = ::LoadLibraryA(path.string().c_str());
            if (!handle)
            {
                outErr = "LoadLibraryA failed for plugin binary.";
                return false;
            }
#else
            handle = ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle)
            {
                outErr = ::dlerror() ? ::dlerror() : "dlopen failed for plugin binary.";
                return false;
            }
#endif
            return true;
        }

        void* symbol(const char* name) const
        {
            if (!handle || !name) return nullptr;
#ifdef _WIN32
            return reinterpret_cast<void*>(::GetProcAddress(handle, name));
#else
            return ::dlsym(handle, name);
#endif
        }

        void close()
        {
            if (!handle) return;
#ifdef _WIN32
            ::FreeLibrary(handle);
#else
            ::dlclose(handle);
#endif
            handle = nullptr;
        }
    };

    Manager::Manager() = default;
    Manager::~Manager()
    {
        shutdown();
    }

    void Manager::initialize(const std::string& appVersion, const std::string& platform)
    {
        appVersion_ = appVersion;
        platform_ = platform.empty() ? strova::paths::getPlatformKey() : platform;
        installRoot_ = strova::paths::getAppDataDir() / "plugins" / "installed";
        storageRoot_ = strova::paths::getAppDataDir() / "plugins" / "storage";
        host_.appVersion = appVersion_;
        host_.platform = platform_;
        host_.registries = &registries_;
        host_.refreshHeader();
        host_.refreshServices();
        host_.bindProject(activeProject_);
        host_.bindApp(activeApp_);

        std::error_code ec;
        fs::create_directories(installRoot_, ec);
        fs::create_directories(storageRoot_, ec);
    }

    void Manager::shutdown()
    {
        clearLoaded();
        registries_ = {};
        missingRecords_.clear();
        lastDependencyIssues_.clear();
        indexEntries_.clear();
        records_.clear();
    }

    std::filesystem::path Manager::stateFilePath() const
    {
        return strova::paths::getAppDataDir() / "plugins" / "plugin_state.json";
    }

    std::filesystem::path Manager::indexFilePath() const
    {
        return strova::paths::getAppDataDir() / "plugins" / "plugin_index.json";
    }

    std::filesystem::path Manager::pluginStoragePathFor(const std::string& pluginId) const
    {
        return storageRoot_ / pluginId;
    }

    void Manager::clearLoaded()
    {
        for (auto& it : loaded_)
        {
            LoadedRuntime& runtime = it.second;
            if (runtime.destroyFn && runtime.instance)
            {
                runtime.destroyFn(runtime.instance);
                runtime.instance = nullptr;
            }
        }
        loaded_.clear();
        for (RuntimeRecord& record : records_)
        {
            record.loaded = false;
            if (record.state == RuntimeState::Loaded)
                record.state = record.enabled ? RuntimeState::Discovered : RuntimeState::Disabled;
        }
    }

    bool Manager::loadStateFile(std::unordered_map<std::string, bool>& outEnabledMap, std::string& outErr) const
    {
        outEnabledMap.clear();
        std::string text;
        if (!strova::iojson::readTextFile(stateFilePath(), text))
            return true;

        const std::string arr = strova::iojson::extractArrayText(text, "plugins");
        if (arr.empty())
            return true;

        std::vector<std::string> objs;
        strova::iojson::parseObjectsInArray(arr, objs);
        for (const std::string& obj : objs)
        {
            const std::string id = strova::iojson::findStr(obj, "id", "");
            if (id.empty()) continue;
            bool enabled = true;
            size_t pos = 0;
            if (strova::iojson::findKeyPosAfterColon(obj, "enabled", pos))
                strova::iojson::parseBoolAt(obj, pos, enabled);
            outEnabledMap[id] = enabled;
        }
        (void)outErr;
        return true;
    }

    bool Manager::saveStateFile(std::string& outErr) const
    {
        std::ostringstream out;
        out << "{\n  \"version\": 1,\n  \"plugins\": [\n";
        for (std::size_t i = 0; i < records_.size(); ++i)
        {
            const RuntimeRecord& record = records_[i];
            out << "    {\"id\":\"" << strova::iojson::jsonEscape(record.package.manifest.id)
                << "\",\"enabled\":" << (record.enabled ? "true" : "false") << "}";
            if (i + 1 < records_.size()) out << ',';
            out << '\n';
        }
        out << "  ]\n}\n";
        if (!strova::iojson::writeTextFileAtomic(stateFilePath(), out.str()))
        {
            outErr = "Failed to write plugin_state.json.";
            return false;
        }
        return true;
    }

    bool Manager::loadIndexFile(std::vector<InstallIndexEntry>& outEntries, std::string& outErr) const
    {
        outEntries.clear();
        std::string text;
        if (!strova::iojson::readTextFile(indexFilePath(), text))
            return true;

        const std::string arr = strova::iojson::extractArrayText(text, "plugins");
        if (arr.empty())
            return true;

        std::vector<std::string> objs;
        strova::iojson::parseObjectsInArray(arr, objs);
        for (const std::string& obj : objs)
        {
            InstallIndexEntry entry{};
            entry.pluginId = strova::iojson::findStr(obj, "id", "");
            entry.version = strova::iojson::findStr(obj, "version", "");
            entry.installRoot = strova::iojson::findStr(obj, "install_root", "");
            entry.enabled = true;
            entry.ignoreMissing = false;
            size_t pos = 0;
            if (strova::iojson::findKeyPosAfterColon(obj, "enabled", pos)) strova::iojson::parseBoolAt(obj, pos, entry.enabled);
            if (strova::iojson::findKeyPosAfterColon(obj, "ignore_missing", pos)) strova::iojson::parseBoolAt(obj, pos, entry.ignoreMissing);
            if (!entry.pluginId.empty()) outEntries.push_back(std::move(entry));
        }
        (void)outErr;
        return true;
    }

    bool Manager::saveIndexFile(std::string& outErr) const
    {
        std::ostringstream out;
        out << "{\n  \"version\": 1,\n  \"plugins\": [\n";
        for (std::size_t i = 0; i < indexEntries_.size(); ++i)
        {
            const InstallIndexEntry& entry = indexEntries_[i];
            out << "    {"
                << "\"id\":\"" << strova::iojson::jsonEscape(entry.pluginId) << "\","
                << "\"version\":\"" << strova::iojson::jsonEscape(entry.version) << "\","
                << "\"install_root\":\"" << strova::iojson::jsonEscape(entry.installRoot) << "\","
                << "\"enabled\":" << (entry.enabled ? "true" : "false") << ','
                << "\"ignore_missing\":" << (entry.ignoreMissing ? "true" : "false")
                << "}";
            if (i + 1 < indexEntries_.size()) out << ',';
            out << '\n';
        }
        out << "  ]\n}\n";
        if (!strova::iojson::writeTextFileAtomic(indexFilePath(), out.str()))
        {
            outErr = "Failed to write plugin_index.json.";
            return false;
        }
        return true;
    }

    void Manager::applyEnabledMap(const std::unordered_map<std::string, bool>& enabledMap)
    {
        for (RuntimeRecord& record : records_)
        {
            const auto it = enabledMap.find(record.package.manifest.id);
            if (it != enabledMap.end())
                record.enabled = it->second;
            record.state = record.enabled ? RuntimeState::Discovered : RuntimeState::Disabled;
        }
    }

    void Manager::rebuildIndexFromRecords()
    {
        std::unordered_map<std::string, InstallIndexEntry> byId;
        for (const InstallIndexEntry& entry : indexEntries_)
            byId[entry.pluginId] = entry;

        for (const RuntimeRecord& record : records_)
        {
            InstallIndexEntry& entry = byId[record.package.manifest.id];
            entry.pluginId = record.package.manifest.id;
            entry.version = record.package.manifest.version;
            entry.installRoot = record.package.packageRoot.string();
            entry.enabled = record.enabled;
        }

        indexEntries_.clear();
        for (auto& it : byId)
        {
            if (!it.second.pluginId.empty())
                indexEntries_.push_back(std::move(it.second));
        }
        std::sort(indexEntries_.begin(), indexEntries_.end(), [](const InstallIndexEntry& a, const InstallIndexEntry& b)
            {
                return a.pluginId < b.pluginId;
            });
    }

    void Manager::reconcileIndex(const std::vector<InstallIndexEntry>& entries)
    {
        missingRecords_.clear();
        indexEntries_ = entries;
        for (const InstallIndexEntry& entry : entries)
        {
            if (entry.pluginId.empty() || entry.installRoot.empty())
                continue;
            std::error_code ec;
            if (fs::exists(entry.installRoot, ec))
                continue;
            MissingPluginRecord missing{};
            missing.pluginId = entry.pluginId;
            missing.expectedPath = entry.installRoot;
            missing.savedVersion = entry.version;
            missing.ignored = entry.ignoreMissing;
            missing.resolutionHint = entry.ignoreMissing ? "Ignored until user replaces or re-enables it." : "Replace or ignore this missing plugin path.";
            missingRecords_.push_back(std::move(missing));
        }
    }

    bool Manager::discoverInstalled(std::string& outErr)
    {
        clearLoaded();
        records_.clear();
        missingRecords_.clear();
        outErr.clear();

        std::vector<InstallIndexEntry> indexEntries;
        loadIndexFile(indexEntries, outErr);
        reconcileIndex(indexEntries);

        std::error_code ec;
        fs::create_directories(installRoot_, ec);

        for (fs::directory_iterator it(installRoot_, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_directory()) continue;
            const std::string folderName = it->path().filename().string();
            if (!folderName.empty() && (folderName[0] == '.' || folderName[0] == '_')) continue;
            DiscoveredPackage pkg{};
            std::string err;
            RuntimeRecord record{};
            if (discoverPackageFromDirectory(it->path(), appVersion_, platform_, pkg, err))
            {
                record.package = std::move(pkg);
                record.state = RuntimeState::Discovered;
                strova::debug::log("PluginManager", "Discovered plugin package: " + record.package.manifest.id);
            }
            else
            {
                record.package.packageRoot = it->path();
                record.package.manifest.id = it->path().filename().string();
                record.state = RuntimeState::Invalid;
                record.enabled = false;
                record.lastError = err;
                strova::debug::log("PluginManager", "Invalid plugin package ignored: " + it->path().string() + " error=" + err);
            }
            records_.push_back(std::move(record));
        }

        std::unordered_map<std::string, bool> enabledMap;
        loadStateFile(enabledMap, outErr);
        for (const InstallIndexEntry& entry : indexEntries_)
            enabledMap[entry.pluginId] = entry.enabled;
        applyEnabledMap(enabledMap);
        rebuildIndexFromRecords();
        std::string ignored;
        saveIndexFile(ignored);
        saveStateFile(ignored);

        if (!missingRecords_.empty() && outErr.empty())
            outErr = "Some plugins listed in the install index are missing from disk.";
        return true;
    }

    bool Manager::queryBinary(RuntimeRecord& record, LoadedRuntime& runtime, std::string& outErr)
    {
        runtime.queryFn = reinterpret_cast<StrovaPlugin_QueryFn>(runtime.library->symbol("StrovaPlugin_Query"));
        runtime.createFn = reinterpret_cast<StrovaPlugin_CreateFn>(runtime.library->symbol("StrovaPlugin_Create"));
        runtime.destroyFn = reinterpret_cast<StrovaPlugin_DestroyFn>(runtime.library->symbol("StrovaPlugin_Destroy"));
        runtime.invokeCommandFn = reinterpret_cast<StrovaPlugin_InvokeCommandFn>(runtime.library->symbol("StrovaPlugin_InvokeCommand"));
        runtime.renderDockPanelFn = reinterpret_cast<StrovaPlugin_RenderDockPanelFn>(runtime.library->symbol("StrovaPlugin_RenderDockPanel"));
        runtime.handleDockPanelEventFn = reinterpret_cast<StrovaPlugin_HandleDockPanelEventFn>(runtime.library->symbol("StrovaPlugin_HandleDockPanelEvent"));
        runtime.runImporterFn = reinterpret_cast<StrovaPlugin_RunImporterFn>(runtime.library->symbol("StrovaPlugin_RunImporter"));
        runtime.runExporterFn = reinterpret_cast<StrovaPlugin_RunExporterFn>(runtime.library->symbol("StrovaPlugin_RunExporter"));
        runtime.processFlowFn = reinterpret_cast<StrovaPlugin_ProcessFlowFn>(runtime.library->symbol("StrovaPlugin_ProcessFlow"));
        runtime.processFlowLinkFn = reinterpret_cast<StrovaPlugin_ProcessFlowLinkFn>(runtime.library->symbol("StrovaPlugin_ProcessFlowLink"));
        runtime.renderCanvasOverlayFn = reinterpret_cast<StrovaPlugin_RenderCanvasOverlayFn>(runtime.library->symbol("StrovaPlugin_RenderCanvasOverlay"));
        runtime.runAnalysisFn = reinterpret_cast<StrovaPlugin_RunAnalysisFn>(runtime.library->symbol("StrovaPlugin_RunAnalysis"));
        runtime.applyBrushEffectFn = reinterpret_cast<StrovaPlugin_ApplyBrushEffectFn>(runtime.library->symbol("StrovaPlugin_ApplyBrushEffect"));
        runtime.applyStrokeEffectFn = reinterpret_cast<StrovaPlugin_ApplyStrokeEffectFn>(runtime.library->symbol("StrovaPlugin_ApplyStrokeEffect"));
        runtime.runExportPassFn = reinterpret_cast<StrovaPlugin_RunExportPassFn>(runtime.library->symbol("StrovaPlugin_RunExportPass"));
        runtime.runDocumentValidatorFn = reinterpret_cast<StrovaPlugin_RunDocumentValidatorFn>(runtime.library->symbol("StrovaPlugin_RunDocumentValidator"));

        if (!runtime.queryFn)
        {
            outErr = "Plugin binary is missing StrovaPlugin_Query export.";
            return false;
        }

        StrovaPluginQuery query{};
        query.struct_size = sizeof(query);
        if (runtime.queryFn(&host_.header, &query) == 0)
        {
            outErr = "Plugin query export returned failure.";
            return false;
        }

        record.query.abiVersion = query.abi_version;
        record.query.apiVersion = query.plugin_api_version;
        record.query.pluginId = query.plugin_id ? query.plugin_id : "";
        record.query.pluginVersion = query.plugin_version ? query.plugin_version : "";
        record.query.displayName = query.display_name ? query.display_name : "";
        record.query.capabilityMask = query.capability_mask;
        record.query.permissionMask = query.permission_mask;

        if (record.query.abiVersion != kAbiVersion)
        {
            outErr = "Plugin binary reported an unsupported ABI version.";
            return false;
        }
        if (record.query.pluginId != record.package.manifest.id)
        {
            outErr = "Plugin binary id does not match manifest id.";
            return false;
        }
        if (record.query.pluginVersion != record.package.manifest.version)
        {
            outErr = "Plugin binary version does not match manifest version.";
            return false;
        }
        if (record.query.capabilityMask != record.package.manifest.capabilityMask())
        {
            outErr = "Plugin binary capability mask does not match the manifest capabilities.";
            return false;
        }
        if (record.query.permissionMask != record.package.manifest.permissions.toMask())
        {
            outErr = "Plugin binary permission mask does not match the manifest permissions.";
            return false;
        }

        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_COMMANDS) && !runtime.invokeCommandFn)
        {
            outErr = "Plugin declares commands but is missing StrovaPlugin_InvokeCommand.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_DOCK_PANELS) && !runtime.renderDockPanelFn)
        {
            outErr = "Plugin declares dock panels but is missing StrovaPlugin_RenderDockPanel.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_IMPORTERS) && !runtime.runImporterFn)
        {
            outErr = "Plugin declares importers but is missing StrovaPlugin_RunImporter.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_EXPORTERS) && !runtime.runExporterFn)
        {
            outErr = "Plugin declares exporters but is missing StrovaPlugin_RunExporter.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_FLOW_PROCESSORS) && !runtime.processFlowFn)
        {
            outErr = "Plugin declares flow processors but is missing StrovaPlugin_ProcessFlow.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_FLOWLINK_PROCESSORS) && !runtime.processFlowLinkFn)
        {
            outErr = "Plugin declares flowlink processors but is missing StrovaPlugin_ProcessFlowLink.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_CANVAS_OVERLAYS) && !runtime.renderCanvasOverlayFn)
        {
            outErr = "Plugin declares canvas overlays but is missing StrovaPlugin_RenderCanvasOverlay.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_ANALYSIS_TOOLS) && !runtime.runAnalysisFn)
        {
            outErr = "Plugin declares analysis tools but is missing StrovaPlugin_RunAnalysis.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_BRUSH_EFFECTS) && !runtime.applyBrushEffectFn)
        {
            outErr = "Plugin declares brush effects but is missing StrovaPlugin_ApplyBrushEffect.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_STROKE_EFFECTS) && !runtime.applyStrokeEffectFn)
        {
            outErr = "Plugin declares stroke effects but is missing StrovaPlugin_ApplyStrokeEffect.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_EXPORT_PASSES) && !runtime.runExportPassFn)
        {
            outErr = "Plugin declares export passes but is missing StrovaPlugin_RunExportPass.";
            return false;
        }
        if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_DOCUMENT_VALIDATORS) && !runtime.runDocumentValidatorFn)
        {
            outErr = "Plugin declares document validators but is missing StrovaPlugin_RunDocumentValidator.";
            return false;
        }

        return true;
    }

    bool Manager::loadOne(RuntimeRecord& record, std::string& outErr)
    {
        outErr.clear();
        if (!record.enabled)
        {
            record.state = RuntimeState::Disabled;
            return true;
        }
        if (!record.package.validation.ok)
        {
            record.state = RuntimeState::Invalid;
            record.lastError = record.package.validation.firstMessage();
            outErr = record.lastError;
            return false;
        }

        LoadedRuntime runtime{};
        runtime.library = std::make_unique<DynamicLibrary>();
        if (!runtime.library->open(record.package.entryPath, outErr))
        {
            record.state = RuntimeState::Faulted;
            record.lastError = outErr;
            return false;
        }
        if (!queryBinary(record, runtime, outErr))
        {
            runtime.library.reset();
            record.state = RuntimeState::Faulted;
            record.lastError = outErr;
            return false;
        }

        if (!runtime.createFn)
        {
            outErr = "Plugin binary is missing StrovaPlugin_Create export.";
            record.state = RuntimeState::Faulted;
            record.lastError = outErr;
            return false;
        }

        const bool allowPluginStorage = hasPermission(record.query.permissionMask, Permission::PluginStorage);
        std::error_code ec;
        if (allowPluginStorage)
            fs::create_directories(pluginStoragePathFor(record.package.manifest.id), ec);
        runtime.registrationScope = host_.makeRegistrationScope(record.package.manifest.id);

        StrovaPluginCreateInfo createInfo{};
        createInfo.struct_size = sizeof(createInfo);
        createInfo.host_api_version = kHostApiVersion;
        createInfo.host = &host_.header;
        createInfo.services = &host_.services;
        createInfo.registration_scope = &runtime.registrationScope;
        createInfo.plugin_id = record.package.manifest.id.c_str();
        const std::string rootStr = record.package.packageRoot.string();
        const std::string storageStr = allowPluginStorage ? pluginStoragePathFor(record.package.manifest.id).string() : std::string();
        createInfo.plugin_root = rootStr.c_str();
        createInfo.plugin_storage_root = allowPluginStorage ? storageStr.c_str() : "";

        if (runtime.createFn(&createInfo, &runtime.instance) == 0)
        {
            outErr = "Plugin create export returned failure.";
            record.state = RuntimeState::Faulted;
            record.lastError = outErr;
            registries_.removeAllOwnedBy(record.package.manifest.id);
            return false;
        }

        if (!runtime.registrationScope.registrationErrors.empty())
        {
            outErr = joinErrors(runtime.registrationScope.registrationErrors);
            record.state = RuntimeState::Faulted;
            record.lastError = outErr;
            if (runtime.destroyFn && runtime.instance)
            {
                runtime.destroyFn(runtime.instance);
                runtime.instance = nullptr;
            }
            registries_.removeAllOwnedBy(record.package.manifest.id);
            return false;
        }

        record.lastWarning = runtime.registrationScope.lastWarning;
        loaded_[record.package.manifest.id] = std::move(runtime);
        record.loaded = true;
        record.state = RuntimeState::Loaded;
        record.lastError.clear();
        strova::debug::log("PluginManager", "Loaded plugin: " + record.package.manifest.id + " state=" + runtimeStateName(record.state));
        return true;
    }

    bool Manager::loadEnabledPlugins(std::string& outErr)
    {
        outErr.clear();
        clearLoaded();
        registries_ = {};
        host_.registries = &registries_;
        host_.bindProject(activeProject_);
        host_.bindApp(activeApp_);
        bool allOk = true;
        for (RuntimeRecord& record : records_)
        {
            if (!record.enabled)
            {
                record.state = RuntimeState::Disabled;
                continue;
            }
            std::string err;
            if (!loadOne(record, err))
            {
                allOk = false;
                if (outErr.empty()) outErr = err;
                strova::debug::log("PluginManager", "Failed to load plugin " + record.package.manifest.id + ": " + err);
            }
        }
        return allOk;
    }

    bool Manager::reload(std::string& outErr)
    {
        if (!discoverInstalled(outErr))
            return false;
        return loadEnabledPlugins(outErr);
    }

    bool Manager::invokeCommand(const std::string& commandId, std::string& outErr)
    {
        outErr.clear();
        const CommandDescriptor* desc = registries_.commands.findById(commandId);
        if (!desc)
        {
            outErr = "Plugin command is not registered: " + commandId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.invokeCommandFn)
        {
            outErr = "Plugin command runtime is unavailable for: " + desc->ownerPluginId;
            return false;
        }
        StrovaPluginCommandContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.command_id = desc->id.c_str();
        if (it->second.invokeCommandFn(it->second.instance, &ctx) == 0)
        {
            outErr = "Plugin command returned failure: " + desc->id;
            return false;
        }
        return true;
    }

    bool Manager::renderDockPanel(const std::string& panelId, const std::string& panelTitle, const SDL_Rect& panelRect, int mouseX, int mouseY, bool focused, std::string& outText, std::string& outErr)
    {
        outErr.clear();
        outText.clear();
        const DockPanelDescriptor* desc = registries_.dockPanels.findById(panelId);
        if (!desc)
        {
            outErr = "Plugin dock panel is not registered: " + panelId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.renderDockPanelFn)
        {
            outErr = "Plugin dock panel runtime is unavailable for: " + desc->ownerPluginId;
            return false;
        }

        StrovaPluginDockPanelRenderContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.panel_id = desc->id.c_str();
        ctx.panel_title = panelTitle.c_str();
        ctx.panel_x = panelRect.x;
        ctx.panel_y = panelRect.y;
        ctx.panel_w = panelRect.w;
        ctx.panel_h = panelRect.h;
        ctx.mouse_x = mouseX;
        ctx.mouse_y = mouseY;
        ctx.hovered = (mouseX >= panelRect.x && mouseX < panelRect.x + panelRect.w && mouseY >= panelRect.y && mouseY < panelRect.y + panelRect.h) ? 1 : 0;
        ctx.focused = focused ? 1 : 0;

        std::vector<char> buf(8192, 0);
        StrovaPluginTextBuffer outBuf{};
        outBuf.struct_size = sizeof(outBuf);
        outBuf.data = buf.data();
        outBuf.capacity = static_cast<uint32_t>(buf.size());
        outBuf.written = 0;
        if (it->second.renderDockPanelFn(it->second.instance, &ctx, &outBuf) == 0)
        {
            outErr = "Plugin dock panel render returned failure: " + desc->id;
            return false;
        }
        copyTextBufferOut(outBuf, outText);
        return true;
    }

    bool Manager::handleDockPanelEvent(const std::string& panelId, const SDL_Event& e, int mouseX, int mouseY, std::string& outErr)
    {
        outErr.clear();
        const DockPanelDescriptor* desc = registries_.dockPanels.findById(panelId);
        if (!desc)
        {
            outErr = "Plugin dock panel is not registered: " + panelId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.handleDockPanelEventFn)
            return true;

        StrovaPluginDockPanelEventContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.panel_id = desc->id.c_str();
        ctx.event_type = mapPanelEventType(e);
        ctx.mouse_x = mouseX;
        ctx.mouse_y = mouseY;
        ctx.button = 0;
        ctx.wheel_x = 0;
        ctx.wheel_y = 0;
        ctx.keycode = 0;
        ctx.modifiers = SDL_GetModState();
        ctx.text_utf8[0] = '\0';
        switch (e.type)
        {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            ctx.button = e.button.button;
            break;
        case SDL_MOUSEWHEEL:
            ctx.wheel_x = e.wheel.x;
            ctx.wheel_y = e.wheel.y;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            ctx.keycode = static_cast<int>(e.key.keysym.sym);
            break;
        case SDL_TEXTINPUT:
            std::strncpy(ctx.text_utf8, e.text.text, sizeof(ctx.text_utf8) - 1);
            ctx.text_utf8[sizeof(ctx.text_utf8) - 1] = '\0';
            break;
        default:
            break;
        }
        if (it->second.handleDockPanelEventFn(it->second.instance, &ctx) == 0)
        {
            outErr = "Plugin dock panel event handler returned failure: " + desc->id;
            return false;
        }
        return true;
    }

    bool Manager::runImporter(const std::string& importerId, const std::string& sourcePath, std::string& outErr)
    {
        outErr.clear();
        const ImporterDescriptor* desc = registries_.importers.findById(importerId);
        if (!desc)
        {
            outErr = "Plugin importer is not registered: " + importerId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        const RuntimeRecord* record = find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.runImporterFn)
        {
            outErr = "Plugin importer runtime is unavailable for: " + desc->ownerPluginId;
            return false;
        }
        if (!ensurePluginPermission(record, Permission::ProjectRead, "run importers", outErr))
            return false;

        StrovaPluginFileRunContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.operation_id = desc->id.c_str();
        ctx.source_path = sourcePath.c_str();
        ctx.destination_path = "";
        if (it->second.runImporterFn(it->second.instance, &ctx) == 0)
        {
            outErr = "Plugin importer returned failure: " + desc->id;
            return false;
        }
        return true;
    }

    bool Manager::runExporter(const std::string& exporterId, const std::string& destinationPath, std::string& outErr)
    {
        outErr.clear();
        const ExporterDescriptor* desc = registries_.exporters.findById(exporterId);
        if (!desc)
        {
            outErr = "Plugin exporter is not registered: " + exporterId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        const RuntimeRecord* record = find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.runExporterFn)
        {
            outErr = "Plugin exporter runtime is unavailable for: " + desc->ownerPluginId;
            return false;
        }
        if (!ensurePluginPermission(record, Permission::ProjectRead, "run exporters", outErr))
            return false;

        StrovaPluginFileRunContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.operation_id = desc->id.c_str();
        ctx.source_path = "";
        ctx.destination_path = destinationPath.c_str();
        if (it->second.runExporterFn(it->second.instance, &ctx) == 0)
        {
            outErr = "Plugin exporter returned failure: " + desc->id;
            return false;
        }
        return true;
    }

    bool Manager::installPackage(const fs::path& sourcePath, bool enableAfterInstall, std::string& outErr)
    {
        DiscoveredPackage installed{};
        if (!installPackageFromSource(sourcePath, installRoot_, appVersion_, platform_, installed, outErr))
            return false;

        bool found = false;
        for (InstallIndexEntry& entry : indexEntries_)
        {
            if (entry.pluginId == installed.manifest.id)
            {
                entry.version = installed.manifest.version;
                entry.installRoot = installed.packageRoot.string();
                entry.enabled = enableAfterInstall;
                entry.ignoreMissing = false;
                found = true;
                break;
            }
        }
        if (!found)
        {
            InstallIndexEntry entry{};
            entry.pluginId = installed.manifest.id;
            entry.version = installed.manifest.version;
            entry.installRoot = installed.packageRoot.string();
            entry.enabled = enableAfterInstall;
            indexEntries_.push_back(std::move(entry));
        }

        // Save the index with the correct enabled state before reload picks it up.
        std::string saveErr;
        saveIndexFile(saveErr);

        // Single reload: discover (reads the saved index) then load with correct state.
        return reload(outErr);
    }

    bool Manager::setEnabled(const std::string& pluginId, bool enabled, std::string& outErr)
    {
        bool found = false;
        for (RuntimeRecord& record : records_)
        {
            if (record.package.manifest.id == pluginId)
            {
                record.enabled = enabled;
                record.state = enabled ? RuntimeState::Discovered : RuntimeState::Disabled;
                found = true;
                break;
            }
        }
        for (InstallIndexEntry& entry : indexEntries_)
        {
            if (entry.pluginId == pluginId)
            {
                entry.enabled = enabled;
                found = true;
                break;
            }
        }
        if (!found)
        {
            outErr = "Plugin id not found: " + pluginId;
            return false;
        }
        if (!saveIndexFile(outErr)) return false;
        return saveStateFile(outErr);
    }

    bool Manager::setIgnoreMissing(const std::string& pluginId, bool ignoreMissing, std::string& outErr)
    {
        bool found = false;
        for (InstallIndexEntry& entry : indexEntries_)
        {
            if (entry.pluginId == pluginId)
            {
                entry.ignoreMissing = ignoreMissing;
                found = true;
                break;
            }
        }
        for (MissingPluginRecord& record : missingRecords_)
        {
            if (record.pluginId == pluginId)
            {
                record.ignored = ignoreMissing;
                found = true;
                break;
            }
        }
        if (!found)
        {
            outErr = "Missing plugin id not found in install index: " + pluginId;
            return false;
        }
        return saveIndexFile(outErr);
    }

    bool Manager::isEnabled(const std::string& pluginId) const
    {
        for (const RuntimeRecord& record : records_)
        {
            if (record.package.manifest.id == pluginId)
                return record.enabled;
        }
        for (const InstallIndexEntry& entry : indexEntries_)
        {
            if (entry.pluginId == pluginId)
                return entry.enabled;
        }
        return false;
    }

    const RuntimeRecord* Manager::find(const std::string& pluginId) const
    {
        for (const RuntimeRecord& record : records_)
        {
            if (record.package.manifest.id == pluginId)
                return &record;
        }
        return nullptr;
    }


    void Manager::bindProject(Project* project)
    {
        activeProject_ = project;
        host_.bindProject(project);
        for (auto& it : loaded_)
            it.second.registrationScope.activeProject = project;
        if (activeProject_)
        {
            rebuildRuntimeStoreFromProject(&registries_.contentTypes, activeProject_->pluginContents, activeProject_->pluginRuntime);
            collectPluginDependencyUsage(*activeProject_);
        }
    }

    void Manager::bindApp(App* app)
    {
        activeApp_ = app;
        host_.bindApp(app);
        for (auto& it : loaded_)
            it.second.registrationScope.activeApp = app;
    }

    void Manager::evaluateProjectDependencies(Project& project)
    {
        lastDependencyIssues_.clear();

        collectPluginDependencyUsage(project);
        rebuildRuntimeStoreFromProject(&registries_.contentTypes, project.pluginContents, project.pluginRuntime);

        for (PluginContentRecord& content : project.pluginContents)
        {
            content.unresolved = false;
            content.unresolvedReason.clear();

            const RuntimeRecord* runtime = find(content.pluginId);
            if (!runtime)
            {
                content.unresolved = true;
                content.unresolvedReason = "plugin_missing";
                continue;
            }

            if (!runtime->enabled)
            {
                content.unresolved = true;
                content.unresolvedReason = "plugin_disabled";
                continue;
            }

            if (runtime->state == RuntimeState::Faulted)
            {
                content.unresolved = true;
                content.unresolvedReason = "plugin_faulted";
                continue;
            }

            bool schemaSupported = false;
            for (const ContentTypeDescriptor& desc : registries_.contentTypes.items())
            {
                if (desc.ownerPluginId == content.pluginId && desc.id == content.contentTypeId && desc.schemaVersion == content.contentSchemaVersion)
                {
                    schemaSupported = true;
                    break;
                }
            }
            if (!schemaSupported)
            {
                content.unresolved = true;
                content.unresolvedReason = "schema_unsupported";
            }
        }

        for (const PluginDependency& dep : project.pluginDependencies)
        {
            DependencyIssue issue{};
            issue.pluginId = dep.pluginId;
            issue.displayName = dep.displayName.empty() ? dep.pluginId : dep.displayName;
            issue.savedVersion = dep.savedWithPluginVersion;
            issue.affectedItems = dep.contentInstances;
            issue.fallbackAvailable = dep.fallbackAvailable;
            issue.status = DependencyStatus::Available;
            issue.message = "Plugin dependency available.";

            const RuntimeRecord* runtime = find(dep.pluginId);
            if (!runtime)
            {
                issue.status = DependencyStatus::Missing;
                issue.message = "Plugin is not installed at load time. Content will stay unresolved and preserved.";
            }
            else if (!runtime->enabled)
            {
                issue.status = DependencyStatus::Disabled;
                issue.message = "Plugin is installed but disabled. Content will stay unresolved and preserved.";
            }
            else if (runtime->state == RuntimeState::Faulted)
            {
                issue.status = DependencyStatus::Faulted;
                issue.message = "Plugin faulted during load. Content will stay unresolved and preserved.";
            }
            else if (!dep.savedWithPluginVersion.empty() && !runtime->query.pluginVersion.empty() && runtime->query.pluginVersion != dep.savedWithPluginVersion)
            {
                issue.status = DependencyStatus::IncompatibleVersion;
                issue.message = "Project was saved with a different plugin version. Content is preserved until compatible support is available.";
            }

            if (issue.status == DependencyStatus::Available || issue.status == DependencyStatus::IncompatibleVersion)
            {
                for (int schema : dep.requiredSchemaVersions)
                {
                    bool schemaSupported = false;
                    for (const ContentTypeDescriptor& desc : registries_.contentTypes.items())
                    {
                        if (desc.ownerPluginId == dep.pluginId && desc.schemaVersion == schema)
                        {
                            schemaSupported = true;
                            break;
                        }
                    }
                    if (!schemaSupported)
                    {
                        issue.status = DependencyStatus::SchemaUnsupported;
                        issue.message = "Plugin is present but the saved content schema is not supported. Content is preserved unresolved.";
                        break;
                    }
                }
            }

            if (issue.status != DependencyStatus::Available)
                lastDependencyIssues_.push_back(std::move(issue));
        }
    }


    bool Manager::processFlow(const std::string& processorId, std::string& outInstanceId, std::string& outErr)
    {
        outInstanceId.clear();
        const FlowProcessorDescriptor* desc = registries_.flowProcessors.findById(processorId);
        if (!desc)
        {
            outErr = "Unknown flow processor id: " + processorId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        const RuntimeRecord* record = find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.processFlowFn)
        {
            outErr = "Owning plugin is not loaded or does not expose StrovaPlugin_ProcessFlow.";
            return false;
        }
        if (!ensurePluginPermission(record, Permission::ProjectRead, "process flow content", outErr))
            return false;
        if (!ensurePluginPermission(record, Permission::ProjectWrite, "commit flow content", outErr))
            return false;
        if (!activeProject_ || !activeApp_)
        {
            outErr = "No active project or app is bound for flow processing.";
            return false;
        }
        FlowSnapshotSummary summary{};
        std::vector<FlowSampleView> samples;
        const std::string snapshotJson = captureCurrentFlowSnapshot(*activeApp_, summary, samples) ? buildFlowSnapshotJson(summary, samples) : std::string("{\"available\":false,\"samples\":[]}");
        StrovaPluginFlowProcessContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.processor_id = desc->id.c_str();
        ctx.project_fps = activeApp_->getProjectFPS();
        ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        ctx.input_kind = desc->inputKind.c_str();
        ctx.output_type_id = desc->outputTypeId.c_str();
        ctx.snapshot_json = snapshotJson.c_str();
        char bufferMem[16384]{};
        StrovaPluginTextBuffer buffer{};
        buffer.struct_size = sizeof(buffer);
        buffer.data = bufferMem;
        buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
        buffer.written = 0;
        if (it->second.processFlowFn(it->second.instance, &ctx, &buffer) == 0)
        {
            outErr = "Flow processor callback returned failure.";
            return false;
        }
        std::string resultJson;
        copyTextBufferOut(buffer, resultJson);
        if (resultJson.empty())
        {
            outErr = "Flow processor returned no output payload.";
            return false;
        }
        PluginContentAttachment attachment{};
        attachment.kind = "flow_segment";
        attachment.trackId = std::to_string(activeApp_->getEngine().getActiveTrack());
        attachment.startFrame = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        attachment.endFrame = attachment.startFrame;
        attachment.frame = attachment.startFrame;
        attachment.ownerId = desc->id;
        const RuntimeRecord* ownerRecord = find(desc->ownerPluginId);
        return commitFlowProcessorResult(*activeProject_, registries_, desc->ownerPluginId, desc->id, desc->outputTypeId, desc->schemaVersion, ownerRecord ? ownerRecord->query.pluginVersion : std::string(), attachment, resultJson, false, outInstanceId, outErr);
    }

    bool Manager::processFlowLink(const std::string& processorId, int targetTrackId, std::string& outInstanceId, std::string& outErr)
    {
        outInstanceId.clear();
        const FlowLinkProcessorDescriptor* desc = registries_.flowLinkProcessors.findById(processorId);
        if (!desc)
        {
            outErr = "Unknown flowlink processor id: " + processorId;
            return false;
        }
        auto it = loaded_.find(desc->ownerPluginId);
        const RuntimeRecord* record = find(desc->ownerPluginId);
        if (it == loaded_.end() || !it->second.instance || !it->second.processFlowLinkFn)
        {
            outErr = "Owning plugin is not loaded or does not expose StrovaPlugin_ProcessFlowLink.";
            return false;
        }
        if (!ensurePluginPermission(record, Permission::ProjectRead, "process flowlink content", outErr))
            return false;
        if (!ensurePluginPermission(record, Permission::ProjectWrite, "commit flowlink content", outErr))
            return false;
        if (!activeProject_ || !activeApp_)
        {
            outErr = "No active project or app is bound for flowlink processing.";
            return false;
        }
        FlowSnapshotSummary summary{};
        std::vector<FlowLinkSampleView> samples;
        const std::string snapshotJson = captureCurrentFlowLinkSnapshot(*activeApp_, summary, samples) ? buildFlowLinkSnapshotJson(summary, samples) : std::string("{\"available\":false,\"samples\":[]}");
        std::vector<FlowLinkClipSummary> clips;
        const std::string trackJson = captureFlowLinkTrackClips(*activeApp_, targetTrackId, clips) ? buildFlowLinkTrackJson(targetTrackId, clips) : (std::string("{\"target_track_id\":") + std::to_string(targetTrackId) + ",\"clips\":[]}");
        StrovaPluginFlowLinkProcessContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = desc->ownerPluginId.c_str();
        ctx.processor_id = desc->id.c_str();
        ctx.project_fps = activeApp_->getProjectFPS();
        ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        ctx.target_track_id = targetTrackId;
        ctx.input_kind = desc->inputKind.c_str();
        ctx.output_type_id = desc->outputTypeId.c_str();
        ctx.snapshot_json = snapshotJson.c_str();
        ctx.track_json = trackJson.c_str();
        char bufferMem[16384]{};
        StrovaPluginTextBuffer buffer{};
        buffer.struct_size = sizeof(buffer);
        buffer.data = bufferMem;
        buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
        buffer.written = 0;
        if (it->second.processFlowLinkFn(it->second.instance, &ctx, &buffer) == 0)
        {
            outErr = "FlowLink processor callback returned failure.";
            return false;
        }
        std::string resultJson;
        copyTextBufferOut(buffer, resultJson);
        if (resultJson.empty())
        {
            outErr = "FlowLink processor returned no output payload.";
            return false;
        }
        PluginContentAttachment attachment{};
        attachment.kind = "flowlink_segment";
        attachment.trackId = std::to_string(targetTrackId);
        attachment.startFrame = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        attachment.endFrame = attachment.startFrame;
        attachment.frame = attachment.startFrame;
        attachment.ownerId = desc->id;
        const RuntimeRecord* ownerRecord = find(desc->ownerPluginId);
        return commitFlowProcessorResult(*activeProject_, registries_, desc->ownerPluginId, desc->id, desc->outputTypeId, desc->schemaVersion, ownerRecord ? ownerRecord->query.pluginVersion : std::string(), attachment, resultJson, true, outInstanceId, outErr);
    }

    bool Manager::renderCanvasOverlays(const SDL_Rect& canvasRect, int mouseX, int mouseY, std::vector<std::string>& outDrawCommands, std::string& outErr)
    {
        outErr.clear();
        outDrawCommands.clear();
        if (!activeApp_)
            return true;

        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);
        bool any = false;

        for (const auto& pair : loaded_)
        {
            const auto* runtimeRecord = find(pair.first);
            const LoadedRuntime& rt = pair.second;
            if (!runtimeRecord || !rt.instance || !rt.renderCanvasOverlayFn)
                continue;
            if ((runtimeRecord->query.capabilityMask & STROVA_PLUGIN_CAP_CANVAS_OVERLAYS) == 0 &&
                (runtimeRecord->query.capabilityMask & STROVA_PLUGIN_CAP_LAYER_TOOLS) == 0)
                continue;
            if (!runtimeHasPermission(runtimeRecord, Permission::ProjectRead))
                continue;

            StrovaPluginCanvasOverlayContext ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.plugin_id = pair.first.c_str();
            ctx.canvas_x = canvasRect.x;
            ctx.canvas_y = canvasRect.y;
            ctx.canvas_w = canvasRect.w;
            ctx.canvas_h = canvasRect.h;
            ctx.zoom = activeApp_->canvasScaleValue();
            ctx.pan_x = activeApp_->canvasPanXValue();
            ctx.pan_y = activeApp_->canvasPanYValue();
            ctx.mouse_x = mouseX;
            ctx.mouse_y = mouseY;
            ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
            ctx.active_track_id = activeApp_->activeFrameLayerTree().primarySelectedTrackId();
            ctx.layer_tree_json = layerTreeJson.c_str();
            ctx.selection_json = selectionJson.c_str();

            char bufferMem[16384]{};
            StrovaPluginTextBuffer buffer{};
            buffer.struct_size = sizeof(buffer);
            buffer.data = bufferMem;
            buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
            if (rt.renderCanvasOverlayFn(rt.instance, &ctx, &buffer) == 0)
                continue;
            std::string text;
            copyTextBufferOut(buffer, text);
            if (text.empty())
                continue;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty())
                {
                    outDrawCommands.push_back(line);
                    any = true;
                }
            }
        }
        return any || outErr.empty();
    }

    bool Manager::runAnalysis(const std::string& pluginId, std::string& outText, std::string& outErr)
    {
        outErr.clear();
        outText.clear();
        auto it = loaded_.find(pluginId);
        const RuntimeRecord* record = find(pluginId);
        if (!record || it == loaded_.end() || !it->second.instance || !it->second.runAnalysisFn)
        {
            outErr = "Plugin analysis runtime is unavailable for: " + pluginId;
            return false;
        }
        if (!ensurePluginPermission(record, Permission::ProjectRead, "run analysis", outErr))
            return false;
        if (!activeApp_)
        {
            outErr = "No active app is bound for analysis.";
            return false;
        }

        FlowSnapshotSummary summary{};
        std::vector<FlowSampleView> samples;
        const std::string snapshotJson = captureCurrentFlowSnapshot(*activeApp_, summary, samples) ? buildFlowSnapshotJson(summary, samples) : std::string("{\"available\":false,\"samples\":[]}");
        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);

        StrovaPluginAnalysisContext ctx{};
        ctx.struct_size = sizeof(ctx);
        ctx.plugin_id = pluginId.c_str();
        ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        ctx.active_track_id = activeApp_->activeFrameLayerTree().primarySelectedTrackId();
        ctx.layer_tree_json = layerTreeJson.c_str();
        ctx.selection_json = selectionJson.c_str();
        ctx.snapshot_json = snapshotJson.c_str();

        char bufferMem[16384]{};
        StrovaPluginTextBuffer buffer{};
        buffer.struct_size = sizeof(buffer);
        buffer.data = bufferMem;
        buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
        if (it->second.runAnalysisFn(it->second.instance, &ctx, &buffer) == 0)
        {
            outErr = "Plugin analysis callback returned failure.";
            return false;
        }
        return copyTextBufferOut(buffer, outText);
    }

    bool Manager::applyBrushEffects(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr)
    {
        outErr.clear();
        outEffectJson.clear();
        if (!activeApp_)
            return true;

        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);
        bool any = false;

        for (const auto& pair : loaded_)
        {
            const RuntimeRecord* record = find(pair.first);
            const LoadedRuntime& rt = pair.second;
            if (!record || !rt.instance || !rt.applyBrushEffectFn)
                continue;
            if ((record->query.capabilityMask & STROVA_PLUGIN_CAP_BRUSH_EFFECTS) == 0)
                continue;
            if (!runtimeHasPermission(record, Permission::ProjectRead))
                continue;

            StrovaPluginBrushEffectContext ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.plugin_id = pair.first.c_str();
            ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
            ctx.active_track_id = activeApp_->activeFrameLayerTree().primarySelectedTrackId();
            ctx.canvas_x = canvasRect.x;
            ctx.canvas_y = canvasRect.y;
            ctx.canvas_w = canvasRect.w;
            ctx.canvas_h = canvasRect.h;
            ctx.zoom = activeApp_->canvasScaleValue();
            ctx.layer_tree_json = layerTreeJson.c_str();
            ctx.selection_json = selectionJson.c_str();

            char bufferMem[16384]{};
            StrovaPluginTextBuffer buffer{};
            buffer.struct_size = sizeof(buffer);
            buffer.data = bufferMem;
            buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
            if (rt.applyBrushEffectFn(rt.instance, &ctx, &buffer) == 0)
                continue;
            std::string text;
            if (!copyTextBufferOut(buffer, text) || text.empty())
                continue;
            outEffectJson.push_back(text);
            any = true;
        }
        return any || outErr.empty();
    }

    bool Manager::applyStrokeEffects(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr)
    {
        outErr.clear();
        outEffectJson.clear();
        if (!activeApp_)
            return true;

        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);
        bool any = false;

        for (const auto& pair : loaded_)
        {
            const RuntimeRecord* record = find(pair.first);
            const LoadedRuntime& rt = pair.second;
            if (!record || !rt.instance || !rt.applyStrokeEffectFn)
                continue;
            if ((record->query.capabilityMask & STROVA_PLUGIN_CAP_STROKE_EFFECTS) == 0)
                continue;
            if (!runtimeHasPermission(record, Permission::ProjectRead))
                continue;

            StrovaPluginStrokeEffectContext ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.plugin_id = pair.first.c_str();
            ctx.active_frame_index = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
            ctx.active_track_id = activeApp_->activeFrameLayerTree().primarySelectedTrackId();
            ctx.canvas_x = canvasRect.x;
            ctx.canvas_y = canvasRect.y;
            ctx.canvas_w = canvasRect.w;
            ctx.canvas_h = canvasRect.h;
            ctx.zoom = activeApp_->canvasScaleValue();
            ctx.layer_tree_json = layerTreeJson.c_str();
            ctx.selection_json = selectionJson.c_str();

            char bufferMem[16384]{};
            StrovaPluginTextBuffer buffer{};
            buffer.struct_size = sizeof(buffer);
            buffer.data = bufferMem;
            buffer.capacity = static_cast<uint32_t>(sizeof(bufferMem));
            if (rt.applyStrokeEffectFn(rt.instance, &ctx, &buffer) == 0)
                continue;
            std::string text;
            if (!copyTextBufferOut(buffer, text) || text.empty())
                continue;
            outEffectJson.push_back(text);
            any = true;
        }
        return any || outErr.empty();
    }


    bool Manager::runExportPasses(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr)
    {
        outErr.clear();
        outEffectJson.clear();
        if (!activeProject_ || !activeApp_)
            return true;

        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);
        const int activeFrame = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());

        for (const RuntimeRecord& record : records_)
        {
            if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_EXPORT_PASSES) == 0)
                continue;
            if (!runtimeHasPermission(&record, Permission::ProjectRead))
                continue;

            auto it = loaded_.find(record.package.manifest.id);
            if (it == loaded_.end() || !it->second.runExportPassFn || !it->second.instance)
                continue;

            StrovaPluginExportPassContext ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.plugin_id = record.package.manifest.id.c_str();
            ctx.pass_id = record.package.manifest.id.c_str();
            ctx.active_frame_index = activeFrame;
            ctx.canvas_w = canvasRect.w;
            ctx.canvas_h = canvasRect.h;
            ctx.layer_tree_json = layerTreeJson.c_str();
            ctx.selection_json = selectionJson.c_str();
            ctx.export_context_json = "{\"mode\":\"editor_preview\"}";

            char outData[16384]{};
            StrovaPluginTextBuffer out{};
            out.struct_size = sizeof(out);
            out.data = outData;
            out.capacity = static_cast<uint32_t>(sizeof(outData));
            out.written = 0;

            if (it->second.runExportPassFn(it->second.instance, &ctx, &out) == 0)
            {
                outErr = "Plugin export pass failed: " + record.package.manifest.id;
                return false;
            }

            outEffectJson.emplace_back(out.data && out.written ? std::string(out.data, out.written) : std::string("{}"));
        }
        return true;
    }

    bool Manager::runDocumentValidators(std::vector<std::string>& outReports, std::string& outErr)
    {
        outErr.clear();
        outReports.clear();
        if (!activeProject_ || !activeApp_)
            return true;

        const std::string layerTreeJson = buildLayerTreeJson(*activeApp_);
        const std::string selectionJson = buildSelectionJson(*activeApp_);
        FlowSnapshotSummary summary{};
        std::vector<FlowSampleView> samples;
        const std::string snapshotJson = captureCurrentFlowSnapshot(*activeApp_, summary, samples)
            ? buildFlowSnapshotJson(summary, samples)
            : std::string("{\"available\":false,\"samples\":[]}");
        const int activeFrame = static_cast<int>(activeApp_->getEngine().getCurrentFrameIndex());
        const int activeTrack = activeApp_->getEngine().getActiveTrack();

        for (const RuntimeRecord& record : records_)
        {
            if ((record.query.capabilityMask & STROVA_PLUGIN_CAP_DOCUMENT_VALIDATORS) == 0)
                continue;
            if (!runtimeHasPermission(&record, Permission::ProjectRead))
                continue;

            auto it = loaded_.find(record.package.manifest.id);
            if (it == loaded_.end() || !it->second.runDocumentValidatorFn || !it->second.instance)
                continue;

            StrovaPluginDocumentValidatorContext ctx{};
            ctx.struct_size = sizeof(ctx);
            ctx.plugin_id = record.package.manifest.id.c_str();
            ctx.active_frame_index = activeFrame;
            ctx.active_track_id = activeTrack;
            ctx.layer_tree_json = layerTreeJson.c_str();
            ctx.selection_json = selectionJson.c_str();
            ctx.snapshot_json = snapshotJson.c_str();

            char outData[16384]{};
            StrovaPluginTextBuffer out{};
            out.struct_size = sizeof(out);
            out.data = outData;
            out.capacity = static_cast<uint32_t>(sizeof(outData));
            out.written = 0;

            if (it->second.runDocumentValidatorFn(it->second.instance, &ctx, &out) == 0)
            {
                outErr = "Plugin document validator failed: " + record.package.manifest.id;
                return false;
            }

            outReports.emplace_back(out.data && out.written ? std::string(out.data, out.written) : std::string());
        }
        return true;
    }


}
