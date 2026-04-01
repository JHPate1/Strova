std::filesystem::path App::currentDockLayoutPath() const
{
    if (!appSettings.persistentDocking && !currentProject.folderPath.empty())
        return std::filesystem::path(currentProject.folderPath) / "ui_layout.json";
    return strova::paths::getAppDataDir() / "ui_layout.json";
}

void App::saveDockLayoutForCurrentContext() const
{
    const std::filesystem::path path = currentDockLayoutPath();
    if (!path.empty())
        dockUi.saveLayout(path);
}

void App::loadColorPickerWindowState()
{
    const std::filesystem::path path = strova::paths::getAppDataDir() / "ui_colorpicker.json";
    std::string text;
    if (!readTextFileSimple(path, text))
        return;

    int x = runtimeState.ui.colorPickerWindow.rect.x;
    int y = runtimeState.ui.colorPickerWindow.rect.y;
    int w = runtimeState.ui.colorPickerWindow.rect.w;
    int h = runtimeState.ui.colorPickerWindow.rect.h;
    bool visible = runtimeState.ui.colorPickerWindow.visible;
    (void)findJsonIntFieldSimple(text, "x", x);
    (void)findJsonIntFieldSimple(text, "y", y);
    (void)findJsonIntFieldSimple(text, "w", w);
    (void)findJsonIntFieldSimple(text, "h", h);
    (void)findJsonBoolFieldSimple(text, "visible", visible);
    runtimeState.ui.colorPickerWindow.rect = SDL_Rect{ x, y, std::max(220, w), std::max(240, h) };
    runtimeState.ui.colorPickerWindow.visible = visible;
}

void App::saveColorPickerWindowState() const
{
    const std::filesystem::path path = strova::paths::getAppDataDir() / "ui_colorpicker.json";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file) return;
    file << "{\n";
    file << "  \"x\": " << runtimeState.ui.colorPickerWindow.rect.x << ",\n";
    file << "  \"y\": " << runtimeState.ui.colorPickerWindow.rect.y << ",\n";
    file << "  \"w\": " << runtimeState.ui.colorPickerWindow.rect.w << ",\n";
    file << "  \"h\": " << runtimeState.ui.colorPickerWindow.rect.h << ",\n";
    file << "  \"visible\": " << (runtimeState.ui.colorPickerWindow.visible ? "true" : "false") << "\n";
    file << "}\n";
}

void App::initializeDockingUiState(int windowW, int windowH)
{
    const SDL_Rect workspace{ 0, ui.topBar.h, windowW, std::max(1, windowH - ui.topBar.h) };
    dockUi.ensureDefaultPanels(workspace);
    dockUi.syncPluginPanels(pluginManagerStore.registries().dockPanels, workspace);
    if (!dockUi.loadLayout(strova::paths::getAppDataDir() / "ui_layout.json", workspace))
        dockUi.saveLayout(strova::paths::getAppDataDir() / "ui_layout.json");
    loadColorPickerWindowState();
}

void App::loadDockLayoutForCurrentContext(int windowW, int windowH)
{
    const SDL_Rect workspace{ 0, ui.topBar.h, windowW, std::max(1, windowH - ui.topBar.h) };
    dockUi.ensureDefaultPanels(workspace);
    dockUi.syncPluginPanels(pluginManagerStore.registries().dockPanels, workspace);
    if (appSettings.persistentDocking)
    {
        dockUi.update(workspace);
        return;
    }
    const std::filesystem::path path = currentDockLayoutPath();
    if (!dockUi.loadLayout(path, workspace))
        dockUi.saveLayout(path);
}

void App::requestSaveProjectNow()
{
    runtimeState.history.saveRequested = true;
    std::string err;
    saveProjectNow(err);
}

