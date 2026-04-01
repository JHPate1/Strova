#pragma once

#include "PluginHost.h"
#include "PluginPackage.h"
#include "PluginProjectData.h"

struct Project;
class App;

#include <SDL.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace strova::plugin
{
    struct PluginQueryInfo
    {
        std::uint32_t abiVersion = 0;
        std::uint32_t apiVersion = 0;
        std::string pluginId;
        std::string pluginVersion;
        std::string displayName;
        std::uint64_t capabilityMask = 0;
        std::uint64_t permissionMask = 0;
    };

    enum class RuntimeState
    {
        Discovered,
        Disabled,
        Loaded,
        Faulted,
        Invalid,
        Missing
    };

    struct InstallIndexEntry
    {
        std::string pluginId;
        std::string version;
        std::string installRoot;
        bool enabled = true;
        bool ignoreMissing = false;
    };

    struct MissingPluginRecord
    {
        std::string pluginId;
        std::string expectedPath;
        std::string savedVersion;
        bool ignored = false;
        std::string resolutionHint;
    };

    struct RuntimeRecord
    {
        DiscoveredPackage package{};
        RuntimeState state = RuntimeState::Discovered;
        bool enabled = true;
        bool loaded = false;
        std::string lastError;
        std::string lastWarning;
        PluginQueryInfo query{};
    };

    class Manager
    {
    public:
        Manager();
        ~Manager();

        void initialize(const std::string& appVersion, const std::string& platform);
        void shutdown();

        bool discoverInstalled(std::string& outErr);
        bool loadEnabledPlugins(std::string& outErr);
        bool reload(std::string& outErr);

        bool installPackage(const std::filesystem::path& sourcePath, bool enableAfterInstall, std::string& outErr);
        bool setEnabled(const std::string& pluginId, bool enabled, std::string& outErr);
        bool setIgnoreMissing(const std::string& pluginId, bool ignoreMissing, std::string& outErr);
        bool isEnabled(const std::string& pluginId) const;
        const RuntimeRecord* find(const std::string& pluginId) const;
        const std::vector<RuntimeRecord>& records() const { return records_; }
        const std::vector<MissingPluginRecord>& missingRecords() const { return missingRecords_; }
        const std::vector<DependencyIssue>& lastDependencyIssues() const { return lastDependencyIssues_; }
        const RegistryHub& registries() const { return registries_; }
        RegistryHub& registries() { return registries_; }

        const std::filesystem::path& pluginInstallRoot() const { return installRoot_; }
        const std::filesystem::path& pluginStorageRoot() const { return storageRoot_; }

        void evaluateProjectDependencies(Project& project);
        void bindProject(Project* project);
        void bindApp(App* app);
        Project* activeProject() const { return activeProject_; }
        bool invokeCommand(const std::string& commandId, std::string& outErr);
        bool renderDockPanel(const std::string& panelId, const std::string& panelTitle, const SDL_Rect& panelRect, int mouseX, int mouseY, bool focused, std::string& outText, std::string& outErr);
        bool handleDockPanelEvent(const std::string& panelId, const SDL_Event& e, int mouseX, int mouseY, std::string& outErr);
        bool runImporter(const std::string& importerId, const std::string& sourcePath, std::string& outErr);
        bool runExporter(const std::string& exporterId, const std::string& destinationPath, std::string& outErr);
        bool processFlow(const std::string& processorId, std::string& outInstanceId, std::string& outErr);
        bool processFlowLink(const std::string& processorId, int targetTrackId, std::string& outInstanceId, std::string& outErr);
        bool renderCanvasOverlays(const SDL_Rect& canvasRect, int mouseX, int mouseY, std::vector<std::string>& outDrawCommands, std::string& outErr);
        bool runAnalysis(const std::string& pluginId, std::string& outText, std::string& outErr);
        bool applyBrushEffects(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr);
        bool applyStrokeEffects(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr);
        bool runExportPasses(const SDL_Rect& canvasRect, std::vector<std::string>& outEffectJson, std::string& outErr);
        bool runDocumentValidators(std::vector<std::string>& outReports, std::string& outErr);

    private:
        struct DynamicLibrary;
        struct LoadedRuntime
        {
            std::unique_ptr<DynamicLibrary> library;
            StrovaPlugin_QueryFn queryFn = nullptr;
            StrovaPlugin_CreateFn createFn = nullptr;
            StrovaPlugin_DestroyFn destroyFn = nullptr;
            StrovaPlugin_InvokeCommandFn invokeCommandFn = nullptr;
            StrovaPlugin_RenderDockPanelFn renderDockPanelFn = nullptr;
            StrovaPlugin_HandleDockPanelEventFn handleDockPanelEventFn = nullptr;
            StrovaPlugin_RunImporterFn runImporterFn = nullptr;
            StrovaPlugin_RunExporterFn runExporterFn = nullptr;
            StrovaPlugin_ProcessFlowFn processFlowFn = nullptr;
            StrovaPlugin_ProcessFlowLinkFn processFlowLinkFn = nullptr;
            StrovaPlugin_RenderCanvasOverlayFn renderCanvasOverlayFn = nullptr;
            StrovaPlugin_RunAnalysisFn runAnalysisFn = nullptr;
            StrovaPlugin_ApplyBrushEffectFn applyBrushEffectFn = nullptr;
            StrovaPlugin_ApplyStrokeEffectFn applyStrokeEffectFn = nullptr;
            StrovaPlugin_RunExportPassFn runExportPassFn = nullptr;
            StrovaPlugin_RunDocumentValidatorFn runDocumentValidatorFn = nullptr;
            StrovaPluginInstance* instance = nullptr;
            RegistrationScope registrationScope{};
        };

        std::filesystem::path stateFilePath() const;
        std::filesystem::path indexFilePath() const;
        void clearLoaded();
        bool loadStateFile(std::unordered_map<std::string, bool>& outEnabledMap, std::string& outErr) const;
        bool saveStateFile(std::string& outErr) const;
        bool loadIndexFile(std::vector<InstallIndexEntry>& outEntries, std::string& outErr) const;
        bool saveIndexFile(std::string& outErr) const;
        bool loadOne(RuntimeRecord& record, std::string& outErr);
        bool queryBinary(RuntimeRecord& record, LoadedRuntime& runtime, std::string& outErr);
        void applyEnabledMap(const std::unordered_map<std::string, bool>& enabledMap);
        void rebuildIndexFromRecords();
        void reconcileIndex(const std::vector<InstallIndexEntry>& entries);
        std::filesystem::path pluginStoragePathFor(const std::string& pluginId) const;

        std::string appVersion_;
        std::string platform_;
        HostContext host_{};
        RegistryHub registries_{};
        std::filesystem::path installRoot_;
        std::filesystem::path storageRoot_;
        std::vector<InstallIndexEntry> indexEntries_{};
        std::vector<MissingPluginRecord> missingRecords_{};
        std::vector<DependencyIssue> lastDependencyIssues_{};
        std::vector<RuntimeRecord> records_;
        std::unordered_map<std::string, LoadedRuntime> loaded_;
        Project* activeProject_ = nullptr;
        App* activeApp_ = nullptr;
    };
}
