#include "PluginRegistry.h"

#include <algorithm>

namespace strova::plugin
{
    namespace
    {
        template <typename T>
        bool validateCommon(const T& desc, std::string& outErr)
        {
            if (desc.ownerPluginId.empty())
            {
                outErr = "Descriptor owner plugin id is empty.";
                return false;
            }
            if (desc.id.empty())
            {
                outErr = "Descriptor id is empty.";
                return false;
            }
            return true;
        }

        template <typename T>
        bool addUnique(std::vector<T>& items, const T& desc, std::string& outErr)
        {
            if (!validateCommon(desc, outErr))
                return false;
            const auto it = std::find_if(items.begin(), items.end(), [&](const T& existing) { return existing.id == desc.id; });
            if (it != items.end())
            {
                outErr = "Descriptor id already registered: " + desc.id;
                return false;
            }
            items.push_back(desc);
            return true;
        }


        template <typename T>
        const T* findByIdImpl(const std::vector<T>& items, const std::string& id)
        {
            const auto it = std::find_if(items.begin(), items.end(), [&](const T& existing) { return existing.id == id; });
            return it != items.end() ? &(*it) : nullptr;
        }

        template <typename T>
        void removeOwned(std::vector<T>& items, const std::string& pluginId)
        {
            items.erase(std::remove_if(items.begin(), items.end(), [&](const T& item)
            {
                return item.ownerPluginId == pluginId;
            }), items.end());
        }
    }

    bool CommandRegistry::add(const CommandDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void CommandRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const CommandDescriptor* CommandRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool DockPanelRegistry::add(const DockPanelDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void DockPanelRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const DockPanelDescriptor* DockPanelRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool ImporterRegistry::add(const ImporterDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void ImporterRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const ImporterDescriptor* ImporterRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool ExporterRegistry::add(const ExporterDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void ExporterRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const ExporterDescriptor* ExporterRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool ContentTypeRegistry::add(const ContentTypeDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void ContentTypeRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const ContentTypeDescriptor* ContentTypeRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool FlowProcessorRegistry::add(const FlowProcessorDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void FlowProcessorRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const FlowProcessorDescriptor* FlowProcessorRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }
    bool FlowLinkProcessorRegistry::add(const FlowLinkProcessorDescriptor& desc, std::string& outErr) { return addUnique(items_, desc, outErr); }
    void FlowLinkProcessorRegistry::removeByPlugin(const std::string& pluginId) { removeOwned(items_, pluginId); }
    const FlowLinkProcessorDescriptor* FlowLinkProcessorRegistry::findById(const std::string& id) const { return findByIdImpl(items_, id); }

    void RegistryHub::removeAllOwnedBy(const std::string& pluginId)
    {
        commands.removeByPlugin(pluginId);
        dockPanels.removeByPlugin(pluginId);
        importers.removeByPlugin(pluginId);
        exporters.removeByPlugin(pluginId);
        contentTypes.removeByPlugin(pluginId);
        flowProcessors.removeByPlugin(pluginId);
        flowLinkProcessors.removeByPlugin(pluginId);
    }
}
