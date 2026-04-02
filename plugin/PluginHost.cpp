#include "PluginHost.h"

#include "PluginDocumentModel.h"
#include "PluginFlowBridge.h"
#include "../core/DebugLog.h"
#include "../core/Project.h"
#include "../core/SerializationUtils.h"
#include "../app/App.h"
#include "../platform/AppPaths.h"
#include "../platform/FileDialog.h"
#include "../ui/UILayout.h"
#include <SDL.h>

#include <sstream>
#include <cstring>

namespace strova::plugin
{
    namespace
    {
        void hostLog(const char* scope, const char* message)
        {
            strova::debug::log(scope ? scope : "Plugin", message ? message : "");
        }

        std::vector<std::string> splitCsv(const char* csv)
        {
            std::vector<std::string> out;
            if (!csv || !*csv) return out;
            std::string part;
            std::istringstream stream(csv);
            while (std::getline(stream, part, ','))
            {
                std::string trimmed;
                for (char c : part)
                {
                    if (c != '\r' && c != '\n' && c != '\t') trimmed.push_back(c);
                }
                size_t start = 0;
                while (start < trimmed.size() && trimmed[start] == ' ') ++start;
                size_t end = trimmed.size();
                while (end > start && trimmed[end - 1] == ' ') --end;
                if (end > start) out.push_back(trimmed.substr(start, end - start));
            }
            return out;
        }

        std::string buildLayerTreeJson(const RegistrationScope& scope)
        {
            if (!scope.activeApp)
                return "{\"nodes\":[],\"selection\":[]}";
            const auto& tree = scope.activeApp->activeFrameLayerTree();
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

        std::string buildActiveSelectionJson(const RegistrationScope& scope)
        {
            if (!scope.activeApp)
                return "{\"track_ids\":[],\"primary_track_id\":0}";
            const auto& tree = scope.activeApp->activeFrameLayerTree();
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

        int copyJsonToBuffer(const std::string& json, StrovaPluginJsonBuffer* outJson)
        {
            if (!outJson) return -1;
            outJson->written = static_cast<uint32_t>(json.size());
            if (!outJson->data || outJson->capacity == 0)
                return 0;
            const std::size_t copyCount = std::min<std::size_t>(json.size(), outJson->capacity > 0 ? static_cast<std::size_t>(outJson->capacity - 1) : 0);
            if (copyCount > 0)
                std::memcpy(outJson->data, json.data(), copyCount);
            if (outJson->capacity > 0)
                outJson->data[copyCount] = '\0';
            return (copyCount == json.size()) ? 0 : 1;
        }


        int queueNotification(void* scopePtr, const char* title, const char* message)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (scope.activeApp && scope.activeApp->windowHandle())
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title ? title : "Plugin", message ? message : "", scope.activeApp->windowHandle());
            }
            else
            {
                strova::debug::log("PluginNotify", std::string(title ? title : "Plugin") + ": " + (message ? message : ""));
            }
            return 0;
        }

        static SDL_Rect hostWorkspaceRect(const App& app)
        {
            SDL_Window* window = app.windowHandle();
            int w = 1280;
            int h = 720;
            if (window) SDL_GetWindowSize(window, &w, &h);
            const UILayout& ui = app.getUILayout();
            const int top = ui.topBar.y + ui.topBar.h + 4;
            const int left = ui.leftBar.x + ui.leftBar.w + 4;
            const int rightPad = 8;
            const int bottomPad = 8;
            return SDL_Rect{ left, top, std::max(80, w - left - rightPad), std::max(80, h - top - bottomPad) };
        }

        int requestPanelOpen(void* scopePtr, const char* panelId)
        {
            if (!scopePtr || !panelId || !*panelId) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeApp) return -1;
            scope.activeApp->dockManager().restorePanel(panelId, hostWorkspaceRect(*scope.activeApp));
            scope.activeApp->saveDockLayoutForCurrentContext();
            return 0;
        }

        int requestPanelFocus(void* scopePtr, const char* panelId)
        {
            return requestPanelOpen(scopePtr, panelId);
        }

        int requestRedraw(void* scopePtr)
        {
            (void)scopePtr;
            return 0;
        }

        int openFileDialog(void* scopePtr, StrovaPluginTextBuffer* outPath)
        {
            if (!scopePtr) return -1;
            std::string path;
            if (!platform::pickOpenAnyFile(path))
                return 1;
            return copyJsonToBuffer(path, reinterpret_cast<StrovaPluginJsonBuffer*>(outPath));
        }

        int saveFileDialog(void* scopePtr, const char* suggestedName, StrovaPluginTextBuffer* outPath)
        {
            if (!scopePtr) return -1;
            std::string path;
            if (!platform::pickSaveAnyFile(path, suggestedName ? suggestedName : ""))
                return 1;
            return copyJsonToBuffer(path, reinterpret_cast<StrovaPluginJsonBuffer*>(outPath));
        }

        int copyLayerTreeJson(void* scopePtr, StrovaPluginJsonBuffer* outJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            return copyJsonToBuffer(buildLayerTreeJson(scope), outJson);
        }

        int copyActiveSelectionJson(void* scopePtr, StrovaPluginJsonBuffer* outJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            return copyJsonToBuffer(buildActiveSelectionJson(scope), outJson);
        }

        int registerCommand(void* scopePtr, const StrovaPluginCommandDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->title) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            CommandDescriptor cmd{};
            cmd.id = desc->id;
            cmd.ownerPluginId = scope.ownerPluginId;
            cmd.title = desc->title;
            cmd.category = desc->category ? desc->category : "Plugin";
            cmd.iconPath = desc->icon_path ? desc->icon_path : "";
            std::string err;
            if (!scope.registries || !scope.registries->commands.add(cmd, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register command." : err);
                return -1;
            }
            return 0;
        }

        int registerDockPanel(void* scopePtr, const StrovaPluginDockPanelDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->title) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            DockPanelDescriptor panel{};
            panel.id = desc->id;
            panel.ownerPluginId = scope.ownerPluginId;
            panel.title = desc->title;
            panel.defaultDockZone = desc->default_dock_zone ? desc->default_dock_zone : "CENTER_RIGHT";
            panel.minWidth = desc->min_width;
            panel.minHeight = desc->min_height;
            panel.preferredWidth = desc->preferred_width;
            panel.preferredHeight = desc->preferred_height;
            std::string err;
            if (!scope.registries || !scope.registries->dockPanels.add(panel, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register dock panel." : err);
                return -1;
            }
            return 0;
        }

        int registerImporter(void* scopePtr, const StrovaPluginImportExportDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->display_name) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            ImporterDescriptor item{};
            item.id = desc->id;
            item.ownerPluginId = scope.ownerPluginId;
            item.displayName = desc->display_name;
            item.extensions = splitCsv(desc->extensions_csv);
            std::string err;
            if (!scope.registries || !scope.registries->importers.add(item, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register importer." : err);
                return -1;
            }
            return 0;
        }

        int registerExporter(void* scopePtr, const StrovaPluginImportExportDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->display_name) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            ExporterDescriptor item{};
            item.id = desc->id;
            item.ownerPluginId = scope.ownerPluginId;
            item.displayName = desc->display_name;
            item.extensions = splitCsv(desc->extensions_csv);
            std::string err;
            if (!scope.registries || !scope.registries->exporters.add(item, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register exporter." : err);
                return -1;
            }
            return 0;
        }

        int registerContentType(void* scopePtr, const StrovaPluginContentTypeDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->display_name) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            ContentTypeDescriptor item{};
            item.id = desc->id;
            item.ownerPluginId = scope.ownerPluginId;
            item.displayName = desc->display_name;
            item.schemaVersion = desc->schema_version <= 0 ? 1 : desc->schema_version;
            std::string err;
            if (!scope.registries || !scope.registries->contentTypes.add(item, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register content type." : err);
                return -1;
            }
            return 0;
        }

        int registerFlowProcessor(void* scopePtr, const StrovaPluginProcessorDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->title) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            FlowProcessorDescriptor item{};
            item.id = desc->id;
            item.ownerPluginId = scope.ownerPluginId;
            item.title = desc->title;
            item.inputKind = desc->input_kind ? desc->input_kind : "";
            item.outputTypeId = desc->output_type_id ? desc->output_type_id : "";
            item.schemaVersion = desc->schema_version <= 0 ? 1 : desc->schema_version;
            std::string err;
            if (!scope.registries || !scope.registries->flowProcessors.add(item, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register flow processor." : err);
                return -1;
            }
            return 0;
        }

        int registerFlowLinkProcessor(void* scopePtr, const StrovaPluginProcessorDesc* desc)
        {
            if (!scopePtr || !desc || !desc->id || !desc->title) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            FlowLinkProcessorDescriptor item{};
            item.id = desc->id;
            item.ownerPluginId = scope.ownerPluginId;
            item.title = desc->title;
            item.inputKind = desc->input_kind ? desc->input_kind : "";
            item.outputTypeId = desc->output_type_id ? desc->output_type_id : "";
            item.schemaVersion = desc->schema_version <= 0 ? 1 : desc->schema_version;
            std::string err;
            if (!scope.registries || !scope.registries->flowLinkProcessors.add(item, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to register flowlink processor." : err);
                return -1;
            }
            return 0;
        }

        int setWarning(void* scopePtr, const char* warningMessage)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            scope.lastWarning = warningMessage ? warningMessage : "";
            return 0;
        }

        int upsertProjectContent(void* scopePtr, const StrovaPluginContentMutationDesc* desc)
        {
            if (!scopePtr || !desc || !desc->instance_id || !desc->content_type_id) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeProject)
            {
                scope.registrationErrors.push_back("No active project is bound for plugin content mutation.");
                return -1;
            }

            PluginContentRecord record{};
            record.pluginId = scope.ownerPluginId;
            record.contentTypeId = desc->content_type_id ? desc->content_type_id : "";
            record.pluginVersionSaved = desc->plugin_version_saved ? desc->plugin_version_saved : "";
            record.contentSchemaVersion = desc->content_schema_version <= 0 ? 1 : desc->content_schema_version;
            record.instanceId = desc->instance_id ? desc->instance_id : "";
            record.attachment.kind = desc->attachment.kind ? desc->attachment.kind : "";
            record.attachment.trackId = desc->attachment.track_id ? desc->attachment.track_id : "";
            record.attachment.startFrame = desc->attachment.start_frame;
            record.attachment.endFrame = desc->attachment.end_frame;
            record.attachment.frame = desc->attachment.frame;
            record.attachment.ownerId = desc->attachment.owner_id ? desc->attachment.owner_id : "";
            record.payloadJson = desc->payload_json ? desc->payload_json : "{}";
            record.fallbackProxyJson = desc->fallback_proxy_json ? desc->fallback_proxy_json : "null";
            record.unresolved = (desc->unresolved != 0);
            record.unresolvedReason = desc->unresolved_reason ? desc->unresolved_reason : "";

            std::string err;
            if (!upsertRuntimeObject(scope.activeProject->pluginRuntime, record, scope.registries ? &scope.registries->contentTypes : nullptr, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to upsert plugin project content." : err);
                return -1;
            }
            syncRuntimeStoreToProject(scope.activeProject->pluginRuntime, scope.activeProject->pluginContents);
            collectPluginDependencyUsage(*scope.activeProject);
            return 0;
        }

        int removeProjectContent(void* scopePtr, const char* instanceId)
        {
            if (!scopePtr || !instanceId || !*instanceId) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeProject)
            {
                scope.registrationErrors.push_back("No active project is bound for plugin content removal.");
                return -1;
            }
            std::string err;
            if (!removeRuntimeObject(scope.activeProject->pluginRuntime, instanceId, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to remove plugin project content." : err);
                return -1;
            }
            syncRuntimeStoreToProject(scope.activeProject->pluginRuntime, scope.activeProject->pluginContents);
            collectPluginDependencyUsage(*scope.activeProject);
            return 0;
        }

        int setProjectState(void* scopePtr, const char* stateJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeProject)
            {
                scope.registrationErrors.push_back("No active project is bound for plugin project state mutation.");
                return -1;
            }
            std::string err;
            if (!setPluginProjectStateEntry(scope.activeProject->pluginProjectStates, scope.ownerPluginId, stateJson ? stateJson : "{}", err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to set plugin project state." : err);
                return -1;
            }
            return 0;
        }
    }


        int copyFlowSnapshotJson(void* scopePtr, StrovaPluginJsonBuffer* outJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeApp)
            {
                scope.registrationErrors.push_back("No active app is bound for Flow snapshot access.");
                return -1;
            }
            FlowSnapshotSummary summary{};
            std::vector<FlowSampleView> samples;
            const std::string json = captureCurrentFlowSnapshot(*scope.activeApp, summary, samples) ? buildFlowSnapshotJson(summary, samples) : std::string("{\"available\":false,\"samples\":[]}");
            return copyJsonToBuffer(json, outJson);
        }

        int copyFlowLinkSnapshotJson(void* scopePtr, StrovaPluginJsonBuffer* outJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeApp)
            {
                scope.registrationErrors.push_back("No active app is bound for FlowLink snapshot access.");
                return -1;
            }
            FlowSnapshotSummary summary{};
            std::vector<FlowLinkSampleView> samples;
            const std::string json = captureCurrentFlowLinkSnapshot(*scope.activeApp, summary, samples) ? buildFlowLinkSnapshotJson(summary, samples) : std::string("{\"available\":false,\"samples\":[]}");
            return copyJsonToBuffer(json, outJson);
        }

        int copyFlowLinkTrackJson(void* scopePtr, int targetTrackId, StrovaPluginJsonBuffer* outJson)
        {
            if (!scopePtr) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeApp)
            {
                scope.registrationErrors.push_back("No active app is bound for FlowLink track access.");
                return -1;
            }
            std::vector<FlowLinkClipSummary> clips;
            const std::string json = captureFlowLinkTrackClips(*scope.activeApp, targetTrackId, clips) ? buildFlowLinkTrackJson(targetTrackId, clips) : (std::string("{\"target_track_id\":") + std::to_string(targetTrackId) + ",\"clips\":[]}");
            return copyJsonToBuffer(json, outJson);
        }

        int upsertFlowAugmentation(void* scopePtr, const StrovaPluginFlowBridgeRequest* request, StrovaPluginJsonBuffer* outInstanceJson)
        {
            if (!scopePtr || !request || !request->processor_id || !request->output_type_id) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeProject)
            {
                scope.registrationErrors.push_back("No active project is bound for Flow augmentation mutation.");
                return -1;
            }
            const FlowProcessorDescriptor* desc = scope.registries ? scope.registries->flowProcessors.findById(request->processor_id) : nullptr;
            if (!desc || desc->ownerPluginId != scope.ownerPluginId)
            {
                scope.registrationErrors.push_back("Flow processor is not registered for this plugin.");
                return -1;
            }
            PluginContentAttachment attachment{};
            attachment.kind = request->attachment.kind ? request->attachment.kind : "flow_segment";
            attachment.trackId = request->attachment.track_id ? request->attachment.track_id : "";
            attachment.startFrame = request->attachment.start_frame;
            attachment.endFrame = request->attachment.end_frame;
            attachment.frame = request->attachment.frame;
            attachment.ownerId = request->attachment.owner_id ? request->attachment.owner_id : request->processor_id;
            std::string instanceId;
            std::string err;
            if (!upsertFlowProcessorAugmentation(*scope.activeProject, scope.ownerPluginId, request->output_type_id, request->content_schema_version, request->plugin_version_saved ? request->plugin_version_saved : "", request->processor_id, attachment, request->payload_json ? request->payload_json : "{}", request->fallback_proxy_json ? request->fallback_proxy_json : "null", instanceId, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to upsert Flow augmentation." : err);
                return -1;
            }
            return copyJsonToBuffer(std::string("{\"instance_id\":\"") + strova::iojson::jsonEscape(instanceId) + "\"}", outInstanceJson);
        }

        int upsertFlowLinkAugmentation(void* scopePtr, const StrovaPluginFlowBridgeRequest* request, StrovaPluginJsonBuffer* outInstanceJson)
        {
            if (!scopePtr || !request || !request->processor_id || !request->output_type_id) return -1;
            RegistrationScope& scope = *static_cast<RegistrationScope*>(scopePtr);
            if (!scope.activeProject)
            {
                scope.registrationErrors.push_back("No active project is bound for FlowLink augmentation mutation.");
                return -1;
            }
            const FlowLinkProcessorDescriptor* desc = scope.registries ? scope.registries->flowLinkProcessors.findById(request->processor_id) : nullptr;
            if (!desc || desc->ownerPluginId != scope.ownerPluginId)
            {
                scope.registrationErrors.push_back("FlowLink processor is not registered for this plugin.");
                return -1;
            }
            PluginContentAttachment attachment{};
            attachment.kind = request->attachment.kind ? request->attachment.kind : "flowlink_segment";
            attachment.trackId = request->attachment.track_id ? request->attachment.track_id : "";
            attachment.startFrame = request->attachment.start_frame;
            attachment.endFrame = request->attachment.end_frame;
            attachment.frame = request->attachment.frame;
            attachment.ownerId = request->attachment.owner_id ? request->attachment.owner_id : request->processor_id;
            std::string instanceId;
            std::string err;
            if (!upsertFlowLinkProcessorAugmentation(*scope.activeProject, scope.ownerPluginId, request->output_type_id, request->content_schema_version, request->plugin_version_saved ? request->plugin_version_saved : "", request->processor_id, attachment, request->payload_json ? request->payload_json : "{}", request->fallback_proxy_json ? request->fallback_proxy_json : "null", instanceId, err))
            {
                scope.registrationErrors.push_back(err.empty() ? "Failed to upsert FlowLink augmentation." : err);
                return -1;
            }
            return copyJsonToBuffer(std::string("{\"instance_id\":\"") + strova::iojson::jsonEscape(instanceId) + "\"}", outInstanceJson);
        }

    HostContext::HostContext()
    {
        platform = strova::paths::getPlatformKey();
        refreshHeader();
        refreshServices();
    }

    void HostContext::refreshHeader()
    {
        header = {};
        header.struct_size = sizeof(StrovaHostApiHeader);
        header.host_api_version = kHostApiVersion;
        header.abi_version = kAbiVersion;
        header.app_version = appVersion.c_str();
        header.platform = platform.c_str();
        header.log_fn = &hostLog;
    }

    void HostContext::refreshServices()
    {
        services = {};
        services.struct_size = sizeof(StrovaPluginHostServices);
        services.host_api_version = kHostApiVersion;
        services.register_command_fn = &registerCommand;
        services.register_dock_panel_fn = &registerDockPanel;
        services.register_importer_fn = &registerImporter;
        services.register_exporter_fn = &registerExporter;
        services.register_content_type_fn = &registerContentType;
        services.register_flow_processor_fn = &registerFlowProcessor;
        services.register_flowlink_processor_fn = &registerFlowLinkProcessor;
        services.set_warning_fn = &setWarning;
        services.upsert_project_content_fn = &upsertProjectContent;
        services.remove_project_content_fn = &removeProjectContent;
        services.set_project_state_fn = &setProjectState;
        services.copy_flow_snapshot_json_fn = &copyFlowSnapshotJson;
        services.copy_flowlink_snapshot_json_fn = &copyFlowLinkSnapshotJson;
        services.copy_flowlink_track_json_fn = &copyFlowLinkTrackJson;
        services.copy_layer_tree_json_fn = &copyLayerTreeJson;
        services.copy_active_selection_json_fn = &copyActiveSelectionJson;
        services.upsert_flow_augmentation_fn = &upsertFlowAugmentation;
        services.upsert_flowlink_augmentation_fn = &upsertFlowLinkAugmentation;
        services.queue_notification_fn = &queueNotification;
        services.request_panel_open_fn = &requestPanelOpen;
        services.request_panel_focus_fn = &requestPanelFocus;
        services.request_redraw_fn = &requestRedraw;
        services.open_file_dialog_fn = &openFileDialog;
        services.save_file_dialog_fn = &saveFileDialog;
    }

    RegistrationScope HostContext::makeRegistrationScope(const std::string& pluginId) const
    {
        RegistrationScope scope{};
        scope.registries = registries;
        scope.ownerPluginId = pluginId;
        scope.activeProject = activeProject;
        scope.activeApp = activeApp;
        return scope;
    }

    void HostContext::bindProject(Project* project)
    {
        activeProject = project;
    }

    void HostContext::bindApp(App* app)
    {
        activeApp = app;
    }

    const char* capabilityName(Capability cap)
    {
        switch (cap)
        {
        case Capability::Commands: return "commands";
        case Capability::DockPanels: return "dock_panels";
        case Capability::Importers: return "importers";
        case Capability::Exporters: return "exporters";
        case Capability::ProjectContent: return "project_content";
        case Capability::FlowProcessors: return "flow_processors";
        case Capability::FlowLinkProcessors: return "flowlink_processors";
        case Capability::CanvasOverlays: return "canvas_overlays";
        case Capability::BrushEffects: return "brush_effects";
        case Capability::StrokeEffects: return "stroke_effects";
        case Capability::AnalysisTools: return "analysis_tools";
        case Capability::LayerTools: return "layer_tools";
        case Capability::ExportPasses: return "export_passes";
        case Capability::DocumentValidators: return "document_validators";
        }
        return "unknown";
    }

    const char* permissionName(Permission perm)
    {
        switch (perm)
        {
        case Permission::ProjectRead: return "project_read";
        case Permission::ProjectWrite: return "project_write";
        case Permission::PluginStorage: return "plugin_storage";
        case Permission::BulkDeleteProjectFiles: return "bulk_delete_project_files";
        case Permission::OutsideProjectFs: return "outside_project_fs";
        }
        return "unknown";
    }

    std::uint64_t capabilityMaskFromString(const std::string& value)
    {
        if (value == "commands") return capabilityBit(Capability::Commands);
        if (value == "dock_panels") return capabilityBit(Capability::DockPanels);
        if (value == "importers") return capabilityBit(Capability::Importers);
        if (value == "exporters") return capabilityBit(Capability::Exporters);
        if (value == "project_content") return capabilityBit(Capability::ProjectContent);
        if (value == "flow_processors") return capabilityBit(Capability::FlowProcessors);
        if (value == "flowlink_processors") return capabilityBit(Capability::FlowLinkProcessors);
        if (value == "canvas_overlays") return capabilityBit(Capability::CanvasOverlays);
        if (value == "brush_effects") return capabilityBit(Capability::BrushEffects);
        if (value == "stroke_effects") return capabilityBit(Capability::StrokeEffects);
        if (value == "analysis_tools") return capabilityBit(Capability::AnalysisTools);
        if (value == "layer_tools") return capabilityBit(Capability::LayerTools);
        if (value == "export_passes") return capabilityBit(Capability::ExportPasses);
        if (value == "document_validators") return capabilityBit(Capability::DocumentValidators);
        return 0;
    }

    std::uint64_t permissionMaskFromString(const std::string& value)
    {
        if (value == "project_read") return permissionBit(Permission::ProjectRead);
        if (value == "project_write") return permissionBit(Permission::ProjectWrite);
        if (value == "plugin_storage") return permissionBit(Permission::PluginStorage);
        if (value == "bulk_delete_project_files") return permissionBit(Permission::BulkDeleteProjectFiles);
        if (value == "outside_project_fs") return permissionBit(Permission::OutsideProjectFs);
        return 0;
    }
}
