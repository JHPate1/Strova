#pragma once

#include "PluginAPI.h"
#include "PluginRegistry.h"

#include <string>
#include <vector>

struct Project;
class App;

namespace strova::plugin
{
    struct RegistrationScope
    {
        RegistryHub* registries = nullptr;
        std::string ownerPluginId;
        std::string lastWarning;
        std::vector<std::string> registrationErrors;
        Project* activeProject = nullptr;
        App* activeApp = nullptr;
    };

    struct HostContext
    {
        std::string appVersion;
        std::string platform;
        StrovaHostApiHeader header{};
        StrovaPluginHostServices services{};
        RegistryHub* registries = nullptr;
        Project* activeProject = nullptr;
        App* activeApp = nullptr;

        HostContext();
        void refreshHeader();
        void refreshServices();
        RegistrationScope makeRegistrationScope(const std::string& pluginId) const;
        void bindProject(Project* project);
        void bindApp(App* app);
    };
}
