#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

#include "../core/Tool.h"
#include "../core/BrushSystem.h"

class ToolOptionsPanel
{
public:
    enum class PanelAction
    {
        None = 0,
        OpenBrushCreator,
        ManageBrushes,
        InstallBrush,
        ExportSelectedBrush,
        RefreshBrushes,
        SaveBrushProject
    };

    void layout(const SDL_Rect& area);
    void draw(SDL_Renderer* r, TTF_Font* font, ToolType activeTool, const ToolSettings& settings);
    bool handleEvent(const SDL_Event& e, int mx, int my, ToolType activeTool, const ToolSettings& currentSettings, ToolSettings& outSettings);
    bool consumeAction(PanelAction& outAction, std::string& outBrushId);

private:
    enum class SourceFilter
    {
        All = 0,
        BuiltIn,
        Installed
    };

    SDL_Rect root{};
    PanelAction pendingAction = PanelAction::None;
    std::string pendingBrushId;

    SourceFilter sourceFilter = SourceFilter::All;
    std::string categoryFilter = "All";
    std::string searchText;
    bool searchFocused = false;
    int contentScroll = 0;
    int contentHeight = 0;
    std::vector<std::string> filteredBrushIds;

    bool in(const SDL_Rect& rc, int x, int y) const;
    void drawBox(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color fill, SDL_Color border);
    void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& s, int x, int y, SDL_Color c);
    void drawTextFit(SDL_Renderer* r, TTF_Font* font, const std::string& s, const SDL_Rect& rc, SDL_Color c);
    void drawSlider(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const char* label, float v01);
    bool sliderEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& rc, float& v01);
    void drawStepper(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const char* label, float v, float step, float minv, float maxv);
    bool stepperEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& rc, float& v, float step, float minv, float maxv);

    bool brushVisible(const strova::brush::BrushPackage& pkg) const;
    void rebuildFilteredBrushes();
    std::vector<std::string> availableCategories() const;
    const strova::brush::BrushPackage* selectedBrushPackage(const ToolSettings& settings) const;
    void drawBrushBrowser(SDL_Renderer* r, TTF_Font* font, const ToolSettings& settings, int& y);
    void drawBrushPreview(SDL_Renderer* r, const SDL_Rect& rc, const strova::brush::BrushPackage* pkg);
};
