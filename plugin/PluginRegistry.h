#pragma once

#include <string>
#include <vector>

namespace strova::plugin
{
    struct CommandDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string title;
        std::string category;
        std::string iconPath;
    };

    struct DockPanelDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string title;
        std::string defaultDockZone;
        int minWidth = 160;
        int minHeight = 120;
        int preferredWidth = 320;
        int preferredHeight = 240;
    };

    struct ImporterDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string displayName;
        std::vector<std::string> extensions;
    };

    struct ExporterDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string displayName;
        std::vector<std::string> extensions;
    };

    struct ContentTypeDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string displayName;
        int schemaVersion = 1;
    };

    struct FlowProcessorDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string title;
        std::string inputKind;
        std::string outputTypeId;
        int schemaVersion = 1;
    };

    struct FlowLinkProcessorDescriptor
    {
        std::string id;
        std::string ownerPluginId;
        std::string title;
        std::string inputKind;
        std::string outputTypeId;
        int schemaVersion = 1;
    };

    class CommandRegistry
    {
    public:
        bool add(const CommandDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<CommandDescriptor>& items() const { return items_; }
        const CommandDescriptor* findById(const std::string& id) const;
    private:
        std::vector<CommandDescriptor> items_{};
    };

    class DockPanelRegistry
    {
    public:
        bool add(const DockPanelDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<DockPanelDescriptor>& items() const { return items_; }
        const DockPanelDescriptor* findById(const std::string& id) const;
    private:
        std::vector<DockPanelDescriptor> items_{};
    };

    class ImporterRegistry
    {
    public:
        bool add(const ImporterDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<ImporterDescriptor>& items() const { return items_; }
        const ImporterDescriptor* findById(const std::string& id) const;
    private:
        std::vector<ImporterDescriptor> items_{};
    };

    class ExporterRegistry
    {
    public:
        bool add(const ExporterDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<ExporterDescriptor>& items() const { return items_; }
        const ExporterDescriptor* findById(const std::string& id) const;
    private:
        std::vector<ExporterDescriptor> items_{};
    };

    class ContentTypeRegistry
    {
    public:
        bool add(const ContentTypeDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<ContentTypeDescriptor>& items() const { return items_; }
        const ContentTypeDescriptor* findById(const std::string& id) const;
    private:
        std::vector<ContentTypeDescriptor> items_{};
    };

    class FlowProcessorRegistry
    {
    public:
        bool add(const FlowProcessorDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<FlowProcessorDescriptor>& items() const { return items_; }
        const FlowProcessorDescriptor* findById(const std::string& id) const;
    private:
        std::vector<FlowProcessorDescriptor> items_{};
    };

    class FlowLinkProcessorRegistry
    {
    public:
        bool add(const FlowLinkProcessorDescriptor& desc, std::string& outErr);
        void removeByPlugin(const std::string& pluginId);
        const std::vector<FlowLinkProcessorDescriptor>& items() const { return items_; }
        const FlowLinkProcessorDescriptor* findById(const std::string& id) const;
    private:
        std::vector<FlowLinkProcessorDescriptor> items_{};
    };

    struct RegistryHub
    {
        CommandRegistry commands{};
        DockPanelRegistry dockPanels{};
        ImporterRegistry importers{};
        ExporterRegistry exporters{};
        ContentTypeRegistry contentTypes{};
        FlowProcessorRegistry flowProcessors{};
        FlowLinkProcessorRegistry flowLinkProcessors{};

        void removeAllOwnedBy(const std::string& pluginId);
    };
}
