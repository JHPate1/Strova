/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Launcher.h
   Module:      Ui
   Purpose:     Launcher UI state and public hooks.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class Launcher {
public:
    Launcher() = default;
    ~Launcher();

    void init(TTF_Font* font);
    void refreshProjects();
    void handleEvent(SDL_Event& e, int mx, int my);
    void render(SDL_Renderer* r, int w, int h);

    void setUpdateChecksEnabled(bool enabled) { updateChecksEnabled = enabled; }
    void setPersistentDockingEnabled(bool enabled) { persistentDockingEnabled = enabled; }
    bool consumeUpdateChecksChanged(bool& outEnabled);
    bool consumePersistentDockingChanged(bool& outEnabled);

    bool requestNewProject = false;
    bool requestOpenProject = false;
    bool requestOpenFolder = false;
    bool requestOpenBrushCreator = false;
    bool requestOpenBrushProject = false;
    std::string requestedProjectPath;
    std::string requestedBrushProjectPath;

    std::string requestedNewName;
    int requestedWidth = 1920;
    int requestedHeight = 1080;
    int requestedFPS = 30;

private:
    enum class SortMode { Recent, Name, FPS, Frames };
    enum class ViewMode { Home, DeveloperTools, Settings };
    enum class MenuAction { Rename, Delete, Reveal };
    bool createNewProjectOnDisk(const std::string& name, int w, int h, int fps, std::string& outProjectPath);
    static std::string sanitizeProjectFolderName(const std::string& s);
    static std::string makeUniqueProjectFolder(const std::string& baseName);

    struct ProjectItem {
        std::string name;
        std::string path;
        uint64_t lastWriteKey = 0;
        int fps = 30;
        int frameCount = 1;
    };

    struct PreviewCache {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
        uint64_t lastWriteKey = 0;
        bool attempted = false;
        std::string frameFileUsed;
        std::string diskBmpPath;
        std::string diskKeyPath;
        bool diskReady = false;
    };

    SDL_Texture* makeText(SDL_Renderer* r, const std::string& text, SDL_Color col, int* outW, int* outH);
    bool pointInRect(int x, int y, const SDL_Rect& rc) const;
    void clearCardRects();
    void sortProjects();

    bool readTextFile(const std::string& path, std::string& out);
    int findIntInJson(const std::string& j, const char* key, int def);
    std::string findStrInJson(const std::string& j, const char* key, const std::string& def);
    uint64_t getFolderWriteKey(const std::string& folderPath);

    void refreshProjectMeta(ProjectItem& it);
    int countFramesOnDisk(const std::string& projectPath);
    std::string findFirstFrameJson(const std::string& projectPath);

    std::string greetingFromLocalTime();

    bool parseFrameStrokesFromFile(
        const std::string& frameFilePath,
        std::vector<SDL_FPoint>& outAllPointsWithSeparators,
        std::vector<SDL_Color>& outStrokeColors,
        std::vector<float>& outStrokeThickness
    );

    SDL_Texture* buildPreviewTexture(SDL_Renderer* r, const std::string& frameFilePath, int outW, int outH);
    bool loadPreviewFromDisk(SDL_Renderer* r, ProjectItem& it);
    bool savePreviewToDisk(SDL_Renderer* r, const PreviewCache& pc);
    void ensurePreview(SDL_Renderer* r, ProjectItem& it);

    void openActionsMenu(int projectIndex, int mx, int my);
    void closeActionsMenu();
    void performMenuAction(MenuAction a);
    bool deleteProjectFolder(const std::string& folderPath);
    void renameCancel();
    bool renameCommit();

    void ensureLogo(SDL_Renderer* r);
    void destroyAllPreviews();

    void openNewProjectModal();
    void closeNewProjectModal();
    void commitNewProjectModal();
    void drawNewProjectModal(SDL_Renderer* r, int w, int h);
    bool isDigitString(const std::string& s) const;

private:
    TTF_Font* font = nullptr;
    std::vector<ProjectItem> projects;

    std::string searchQuery;
    bool searchFocused = false;

    SortMode sortMode = SortMode::Recent;

    bool renaming = false;
    int renameProjectIndex = -1;
    std::string renameBuffer;

    bool menuOpen = false;
    int menuProjectIndex = -1;
    SDL_Rect menuRect{};
    SDL_Rect menuItemRename{};
    SDL_Rect menuItemDelete{};
    SDL_Rect menuItemReveal{};

    int hoverIndex = -1;

    SDL_Rect btnNew{};
    SDL_Rect btnOpen{};
    SDL_Rect btnFolder{};
    SDL_Rect btnBrushCreator{};
    SDL_Rect btnOpenBrushProject{};
    SDL_Rect searchRect{};
    std::vector<SDL_Rect> cardRects;

    ViewMode viewMode = ViewMode::Home;
    bool updateChecksEnabled = true;
    bool persistentDockingEnabled = true;
    bool updateChecksDirty = false;
    bool persistentDockingDirty = false;

    SDL_Rect sideProjectsBtn{};
    SDL_Rect sideRecentBtn{};
    SDL_Rect sideTemplatesBtn{};
    SDL_Rect sideDeveloperToolsBtn{};
    SDL_Rect sideSettingsBtn{};
    SDL_Rect settingsBackBtn{};
    SDL_Rect updateToggleTrack{};
    SDL_Rect updateToggleKnob{};
    SDL_Rect dockingToggleTrack{};
    SDL_Rect dockingToggleKnob{};

    SDL_Renderer* lastRenderer = nullptr;
    int lastW = 0;
    int lastH = 0;

    std::unordered_map<std::string, PreviewCache> previews;

    SDL_Texture* logoTex = nullptr;
    int logoW = 0;
    int logoH = 0;
    bool triedLogo = false;

    bool newModalOpen = false;
    enum class NewField { None, Name, Width, Height, FPS };
    NewField newFocus = NewField::None;

    std::string newName = "Untitled";
    std::string newWidthStr = "1920";
    std::string newHeightStr = "1080";
    std::string newFpsStr = "30";

    SDL_Rect newNameRect{};
    SDL_Rect newWRect{};
    SDL_Rect newHRect{};
    SDL_Rect newFpsRect{};
    SDL_Rect newCreateBtn{};
    SDL_Rect newCancelBtn{};
};
