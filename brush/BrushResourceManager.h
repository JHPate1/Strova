#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

class App;

class BrushResourceManager
{
public:
    enum class Action
    {
        None = 0,
        OpenCreator,
        Refresh,
        Install,
        RemoveLocal,
        ApplySelection,
        OpenFolder
    };

    void open(const std::string& selectedBrushId);
    void close();
    bool isOpen() const { return visible; }

    void handleEvent(App& app, const SDL_Event& e);
    void render(App& app, SDL_Renderer* r, TTF_Font* font, int w, int h);

private:
    enum class SourceFilter
    {
        All = 0,
        BuiltIn,
        Installed
    };

    bool visible = false;
    bool searchFocused = false;
    std::string searchText;
    std::string selectedBrushId;
    SourceFilter sourceFilter = SourceFilter::All;
    int scrollOffset = 0;

    bool brushMatches(const std::string& id, const std::string& name, bool builtIn, const std::string& category) const;
};
