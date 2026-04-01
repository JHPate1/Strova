/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Timeline.h
   Module:      Ui
   Purpose:     Timeline state, geometry, and public helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <vector>
#include <string>
#include <functional>
#include "../core/StrovaLimits.h"

namespace strova
{
    using TrackId = int;
    using ClipId = int;

    enum class TrackKind : int
    {
        FlowLink = 0,
        Draw = 1,
        Flow = 2,
        Audio = 3
    };

    struct TimelineClip
    {
        ClipId  id = 0;
        TrackId trackId = 0;

        int startFrame = 0;
        int lengthFrames = 1;

        bool selected = false;
        std::string label;
    };

    struct TimelineTrack
    {
        TrackId id = 0;
        TrackKind kind = TrackKind::Draw;
        std::string name;

        bool visible = true;  
        bool muted = false;
        bool locked = false;

        
        int engineTrackId = 0;
    };

    struct TimelineState
    {
        int rulerH = 32;
        int trackH = 46;
        int trackHeaderW = 220;

        int scrollX = 0;
        int scrollY = 0;

        float pxPerFrame = 18.0f;
        float minPxPerFrame = 6.0f;
        float maxPxPerFrame = 80.0f;

        int fps = 24;
        int totalFrames = 1;

        int nextTrackId = 1;
        int nextClipId = 1;

        std::vector<TimelineTrack> tracks;
        std::vector<TimelineClip>  clips;
    };

    class TimelineWidget
    {
    public:
        TimelineWidget() = default;

        
        
        
        void setRect(const SDL_Rect& r) { rect = r; }
        void setFont(TTF_Font* f) { font = f; }

        const SDL_Rect& getRect() const { return rect; }
        TTF_Font* getFont() const { return font; }

        TimelineState& state() { return st; }
        const TimelineState& state() const { return st; }

        
        
        
        void setFps(int fps);
        void setTotalFrames(int frames);
        void setPlayheadFrame(int frame);

        int  getPlayheadFrame() const { return playheadFrame; }

        
        
        
        TrackId addTrack(TrackKind kind, const char* name);
        void    clearTracks();

        TimelineTrack* findTrack(TrackId id);
        const TimelineTrack* findTrack(TrackId id) const;

        ClipId addClip(TrackId trackId, int startFrame, int lengthFrames, const char* label);
        ClipId addClipOnNewTrack(
            TrackKind kind,
            const char* trackName,
            int startFrame,
            int lengthFrames,
            const char* clipLabel,
            TrackId* outTrackId = nullptr
        );

        void          clearClips();
        TimelineClip* findClip(ClipId id);

        
        
        
        bool    isFocused() const { return focus.enabled; }
        TrackId focusedTrackId() const { return focus.trackId; }

        void focusTrack(TrackId trackId);
        void clearFocus();

        
        
        
        std::function<void(ClipId clipId, int oldStart, int newStart)> onClipMoved;

        std::function<void(TrackId uiTrackId)> onTrackHeaderDoubleClick;

        std::function<void(TrackId uiTrackId)> onFocusEnter;
        std::function<void()>                  onFocusExit;

        
        std::function<void(int atFrame)> onFocusInsertFrame;
        std::function<void(int atFrame)> onFocusDeleteFrame;
        std::function<void(int atFrame)> onFocusDuplicateFrame;

        
        
        
        std::function<void(int fromFrame, int toFrame)> onFocusMoveFrame;
        std::function<void(TrackId uiTrackId, int frameIndex)> onFocusFrameLeftClick;
        std::function<bool(TrackId uiTrackId, int frameIndex)> onFocusFrameRightClick;

        
        std::function<bool(TrackId uiTrackId, int frameIndex)> onFocusFrameHasContent;
        std::function<SDL_Texture* (TrackId uiTrackId, int frameIndex)> onFocusFrameThumbnail;

        
        std::function<void(TrackId uiTrackId)> onTrackSelected;
        std::function<void(TrackId uiTrackId, bool visible)> onTrackVisibilityToggled;

        
        std::function<void(TrackId uiTrackId)> onTrackRenameRequested;
        std::function<void(TrackId uiTrackId)> onTrackDuplicateRequested;
        std::function<void(TrackId uiTrackId)> onTrackDeleteRequested;

        std::function<void(TrackId uiTrackId, bool locked)> onTrackLockToggled;
        std::function<void(TrackId uiTrackId, bool muted)>  onTrackMuteToggled;
        std::function<void(TrackId uiTrackId, bool render)> onTrackRenderToggled;

        
        
        
        bool handleEvent(SDL_Event& e, int mx, int my);
        void draw(SDL_Renderer* r);

        
        
        
        void drawTextPublic(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color c)
        {
            drawText(r, s, x, y, c);
        }

        int textWPublic(const std::string& s) const { return textW(s); }

        
        static int fontH(TTF_Font* f);

        static void drawTextCenteredY(
            TTF_Font* font,
            SDL_Renderer* r,
            const std::function<void(SDL_Renderer*, const std::string&, int, int, SDL_Color)>& drawTextFn,
            const std::string& s,
            const SDL_Rect& box,
            int x,
            SDL_Color c
        );

    private:
        
        int lastMouseX = 0;
        int lastMouseY = 0;

        
        SDL_Rect rulerRect() const;
        SDL_Rect tracksRect() const;

        bool pointIn(const SDL_Rect& r, int x, int y) const;

        int trackRowIndexById(TrackId id) const;
        SDL_Rect trackHeaderRectOnScreen(int rowIndex) const;

        int frameToX(int frame) const;
        int xToFrame(int x) const;

        SDL_Rect clipRectOnScreen(const TimelineClip& c) const;

        
        int  textW(const std::string& s) const;
        void drawText(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color c);

        
        void clampScroll();

        
        void drawRuler(SDL_Renderer* r);
        void drawTracks(SDL_Renderer* r);

        
        void drawFocusHeader(SDL_Renderer* r, const SDL_Rect& rr);
        void drawFocusFrames(SDL_Renderer* r, const SDL_Rect& tr);
        bool handleFocusEvent(SDL_Event& e, int mx, int my);

        
        float focusPx() const;
        int   focusFrameToX(int frame) const;
        int   focusXToFrame(int x) const;

        
        int   focusXToFrameSnap(int x) const;

    private:
        SDL_Rect rect{ 0,0,0,0 };
        TTF_Font* font = nullptr;

        TimelineState st{};
        int playheadFrame = 0;

        ClipId hotClip = 0;
        ClipId dragClip = 0;
        int dragMode = 0;
        int dragMouseStartX = 0;
        int dragClipStartFrame0 = 0;
        int dragClipLen0 = 1;

        bool panning = false;
        int panStartX = 0;
        int panStartY = 0;
        int panScrollX0 = 0;
        int panScrollY0 = 0;

        struct FocusState
        {
            bool enabled = false;
            TrackId trackId = 0;
            bool scrubbing = false;
        } focus;

        struct FocusMoveState
        {
            bool active = false;
            int fromFrame = 0;
            int toFrame = 0;

            bool shiftAtStart = false;
        } focusMove;

        struct DblClickState
        {
            TrackId lastTrackId = 0;
            Uint32 lastTicks = 0;
        } dbl;
    };
} 
