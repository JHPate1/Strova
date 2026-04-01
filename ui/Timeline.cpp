/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Timeline.cpp
   Module:      Ui
   Purpose:     Timeline rendering, hit testing, and thumbnail flow.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../ui/Timeline.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace strova
{
    
    
    
    static void fillRoundedRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c)
    {
        
        
        
        
        if (rc.w <= 0 || rc.h <= 0) return;
        rad = std::max(0, std::min(rad, std::min(rc.w, rc.h) / 2));

        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);

        if (rad <= 0)
        {
            SDL_RenderFillRect(r, &rc);
            return;
        }

        
        
        const double rr = (double)rad;
        const double cy = rr - 0.5; 

        for (int iy = 0; iy < rc.h; ++iy)
        {
            int inset = 0;

            if (iy < rad)
            {
                double y = cy - (double)iy;
                double x = std::sqrt(std::max(0.0, rr * rr - y * y));
                int dx = (int)std::floor(x);
                inset = rad - dx - 1;
                if (inset < 0) inset = 0;
            }
            else if (iy >= rc.h - rad)
            {
                int by = (rc.h - 1) - iy;
                double y = cy - (double)by;
                double x = std::sqrt(std::max(0.0, rr * rr - y * y));
                int dx = (int)std::floor(x);
                inset = rad - dx - 1;
                if (inset < 0) inset = 0;
            }

            int x0 = rc.x + inset;
            int x1 = rc.x + rc.w - 1 - inset;
            if (x1 < x0) std::swap(x0, x1);
            SDL_RenderDrawLine(r, x0, rc.y + iy, x1, rc.y + iy);
        }
    }

    static void drawRoundedRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        rad = std::max(0, std::min(rad, std::min(rc.w, rc.h) / 2));

        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);

        
        SDL_RenderDrawLine(r, rc.x + rad, rc.y, rc.x + rc.w - rad - 1, rc.y);
        SDL_RenderDrawLine(r, rc.x + rad, rc.y + rc.h - 1, rc.x + rc.w - rad - 1, rc.y + rc.h - 1);
        SDL_RenderDrawLine(r, rc.x, rc.y + rad, rc.x, rc.y + rc.h - rad - 1);
        SDL_RenderDrawLine(r, rc.x + rc.w - 1, rc.y + rad, rc.x + rc.w - 1, rc.y + rc.h - rad - 1);

        
        const int cx0 = rc.x + rad;
        const int cx1 = rc.x + rc.w - rad - 1;
        const int cy0 = rc.y + rad;
        const int cy1 = rc.y + rc.h - rad - 1;

        for (int y = -rad; y <= rad; ++y)
        {
            int yy = y;
            int xx = (int)std::floor(std::sqrt((double)rad * (double)rad - (double)yy * (double)yy));

            
            SDL_RenderDrawPoint(r, cx0 - xx, cy0 + yy);
            SDL_RenderDrawPoint(r, cx0 - xx, cy1 + yy);
            
            SDL_RenderDrawPoint(r, cx1 + xx, cy0 + yy);
            SDL_RenderDrawPoint(r, cx1 + xx, cy1 + yy);
        }
    }

    
    static void strokeRoundedRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, int thickness, SDL_Color border, SDL_Color inner)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        if (thickness <= 0) return;

        fillRoundedRect(r, rc, rad, border);

        SDL_Rect in = rc;
        in.x += thickness;
        in.y += thickness;
        in.w -= thickness * 2;
        in.h -= thickness * 2;
        if (in.w <= 0 || in.h <= 0) return;

        int inRad = std::max(0, rad - thickness);
        fillRoundedRect(r, in, inRad, inner);
    }

    static int clampi(int v, int a, int b)
    {
        if (v < a) return a;
        if (v > b) return b;
        return v;
    }

    static float clampf(float v, float a, float b)
    {
        if (v < a) return a;
        if (v > b) return b;
        return v;
    }

    static void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(r, &rc);
    }

    static void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawRect(r, &rc);
    }

    static void drawLine(SDL_Renderer* r, int x0, int y0, int x1, int y1, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
    }

    static int safeRadius(const SDL_Rect& rc, int wanted)
    {
        if (rc.w <= 0 || rc.h <= 0) return 0;
        int maxR = std::min(rc.w, rc.h) / 2;
        if (maxR < 0) maxR = 0;
        return std::min(wanted, maxR);
    }

    static const char* kindLabel(TrackKind k)
    {
        switch (k)
        {
        case TrackKind::FlowLink: return "FLOWLINK";
        case TrackKind::Draw:     return "DRAW";
        case TrackKind::Flow:     return "FLOW";
        case TrackKind::Audio:    return "AUDIO";
        default:                  return "TRACK";
        }
    }

    static int maxTracksForKind(TrackKind kind)
    {
        switch (kind)
        {
        case TrackKind::Draw: return strova::limits::kMaxDrawTracks;
        case TrackKind::Flow: return strova::limits::kMaxFlowTracks;
        case TrackKind::FlowLink: return strova::limits::kMaxFlowLinkTracks;
        case TrackKind::Audio: return strova::limits::kMaxAudioTracks;
        default: return strova::limits::kMaxTimelineTracks;
        }
    }

    static int countTracksOfKind(const TimelineState& st, TrackKind kind)
    {
        int count = 0;
        for (const auto& track : st.tracks)
            if (track.kind == kind)
                ++count;
        return count;
    }

    static bool sdlPointInRect(const SDL_Rect& r, int x, int y)
    {
        return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
    }

    static int measureTextW(TTF_Font* font, const std::string& s)
    {
        if (!font || s.empty()) return 0;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, s.c_str(), &w, &h) != 0) return 0;
        return w;
    }

    static std::string ellipsizeToWidth(TTF_Font* font, const std::string& s, int maxW)
    {
        if (!font) return s;
        if (maxW <= 0) return std::string();
        if (measureTextW(font, s) <= maxW) return s;

        std::string base = s;
        while (!base.empty() && measureTextW(font, base + "...") > maxW)
            base.pop_back();

        if (base.empty()) return "...";
        return base + "...";
    }

    static SDL_Rect inflateRect(const SDL_Rect& r, int pad)
    {
        return SDL_Rect{ r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2 };
    }

    
    static bool hitRectInflated(const SDL_Rect& r, int x, int y, int pad)
    {
        SDL_Rect rr = inflateRect(r, pad);
        if (rr.w < 1) rr.w = 1;
        if (rr.h < 1) rr.h = 1;
        return sdlPointInRect(rr, x, y);
    }

    
    
    
    static SDL_Rect kindPillRectForHeader(const SDL_Rect& headerRect)
    {
        const int w = 60;
        const int h = 22;
        return SDL_Rect{
            headerRect.x + 12,
            headerRect.y + (headerRect.h - h) / 2,
            w, h
        };
    }

    
    
    static std::string buildStatusFlags(const TimelineTrack& t)
    {
        std::string flags;
        if (!t.visible) flags += " HIDDEN";
        if (t.kind == TrackKind::Audio && t.muted) flags += " MUTED";
        if (t.locked) flags += " LOCKED";
        if (!flags.empty()) flags.erase(flags.begin()); 
        return flags;
    }

    static SDL_Rect nameRectForHeader(TTF_Font* font, const SDL_Rect& headerRect, int flagsW)
    {
        (void)font;

        const int leftPad = 12 + 60 + 12;
        const int rightPad = 12;
        const int flagsGap = (flagsW > 0) ? 10 : 0;

        int rightReserve = rightPad + flagsGap + flagsW;
        int w = headerRect.w - leftPad - rightReserve;
        if (w < 10) w = 10;

        return SDL_Rect{
            headerRect.x + leftPad,
            headerRect.y,
            w,
            headerRect.h
        };
    }

    static SDL_Rect headerBodyRectForHeader(TTF_Font* font, const SDL_Rect& headerRect, int flagsW)
    {
        SDL_Rect pill = kindPillRectForHeader(headerRect);
        SDL_Rect name = nameRectForHeader(font, headerRect, flagsW);
        int x0 = pill.x;
        int x1 = name.x + name.w;

        return SDL_Rect{
            x0 - 4,
            headerRect.y,
            (x1 - x0) + 8,
            headerRect.h
        };
    }

    
    
    
    struct FocusHeaderLayout
    {
        SDL_Rect left;
        SDL_Rect back;
        SDL_Rect title;
        SDL_Rect info;
    };

    static FocusHeaderLayout calcFocusHeaderLayout(const SDL_Rect& rr, int trackHeaderW)
    {
        FocusHeaderLayout L{};
        L.left = SDL_Rect{ rr.x, rr.y, trackHeaderW, rr.h };

        const int backH = (std::max)(28, rr.h - 16);
        const int backW = 96; 
        L.back = SDL_Rect{
            rr.x + 12,
            rr.y + (rr.h - backH) / 2,
            backW,
            backH
        };

        int infoRight = rr.x + rr.w - 14;
        int infoLeft = rr.x + rr.w - 420;
        if (infoLeft < rr.x + trackHeaderW + 16) infoLeft = rr.x + trackHeaderW + 16;
        L.info = SDL_Rect{ infoLeft, rr.y, (std::max)(10, infoRight - infoLeft), rr.h };

        int titleX = L.back.x + L.back.w + 14;
        int titleR = L.info.x - 12;
        if (titleR < titleX + 10) titleR = titleX + 10;
        L.title = SDL_Rect{ titleX, rr.y, titleR - titleX, rr.h };

        return L;
    }

    
    
    
    enum class CtxKind : int
    {
        None = 0,
        FocusFrames,
        TrackHeader
    };

    enum class CtxAction : int
    {
        None = 0,

        
        FocusInsert,
        FocusDuplicate,
        FocusDelete,

        
        TrackToggleVisible,
        TrackToggleMute,
        TrackToggleLock,
        TrackFocus,
        TrackRename,
        TrackDuplicate,
        TrackDelete
    };

    struct CtxItem
    {
        std::string label;
        CtxAction action = CtxAction::None;
        bool enabled = true;
    };

    struct CtxMenuState
    {
        bool open = false;
        CtxKind kind = CtxKind::None;

        int x = 0;
        int y = 0;

        int w = 280;
        
        int itemH = 54;
        int hover = -1;

        
        int frame = 0;
        TrackId trackId = 0;

        std::vector<CtxItem> items;
    };

    static std::unordered_map<const TimelineWidget*, CtxMenuState> g_ctx;

    static CtxMenuState& ctxFor(const TimelineWidget* w)
    {
        return g_ctx[w];
    }

    static void ctxClose(const TimelineWidget* w)
    {
        auto& cs = ctxFor(w);
        cs.open = false;
        cs.kind = CtxKind::None;
        cs.hover = -1;
        cs.items.clear();
        cs.frame = 0;
        cs.trackId = 0;
    }

    static SDL_Rect ctxRect(const TimelineWidget* w)
    {
        const auto& cs = ctxFor(w);
        int h = (int)cs.items.size() * cs.itemH + 12;
        return SDL_Rect{ cs.x, cs.y, cs.w, h };
    }

    static void ctxClampToWidget(TimelineWidget* w)
    {
        auto& cs = ctxFor(w);
        SDL_Rect wr = w->getRect();
        SDL_Rect mr = ctxRect(w);

        if (mr.x + mr.w > wr.x + wr.w) mr.x = (wr.x + wr.w) - mr.w - 4;
        if (mr.y + mr.h > wr.y + wr.h) mr.y = (wr.y + wr.h) - mr.h - 4;
        if (mr.x < wr.x) mr.x = wr.x + 4;
        if (mr.y < wr.y) mr.y = wr.y + 4;

        cs.x = mr.x;
        cs.y = mr.y;
    }

    static void ctxOpenFocusFrames(TimelineWidget* w, int mx, int my, int frame)
    {
        auto& cs = ctxFor(w);
        cs.open = true;
        cs.kind = CtxKind::FocusFrames;
        cs.x = mx;
        cs.y = my;
        cs.hover = -1;

        cs.frame = frame;
        cs.trackId = w->focusedTrackId();

        cs.w = 240;
        cs.itemH = 54;

        
        const TimelineTrack* ft = w->findTrack(cs.trackId);
        bool locked = ft ? ft->locked : false;

        cs.items.clear();
        cs.items.push_back({ "Insert frame",    CtxAction::FocusInsert,    !locked && (bool)w->onFocusInsertFrame });
        cs.items.push_back({ "Duplicate frame", CtxAction::FocusDuplicate, !locked && (bool)w->onFocusDuplicateFrame });
        cs.items.push_back({ "Delete frame",    CtxAction::FocusDelete,    !locked && (bool)w->onFocusDeleteFrame });

        ctxClampToWidget(w);
    }

    static void ctxOpenTrackHeader(TimelineWidget* w, int mx, int my, TrackId tid)
    {
        auto& cs = ctxFor(w);
        cs.open = true;
        cs.kind = CtxKind::TrackHeader;
        cs.x = mx;
        cs.y = my;
        cs.hover = -1;

        cs.frame = 0;
        cs.trackId = tid;

        cs.w = 320;
        cs.itemH = 54;

        const TimelineTrack* t = w->findTrack(tid);
        bool vis = t ? t->visible : true;
        bool mut = t ? t->muted : false;
        bool lok = t ? t->locked : false;
        bool isAudio = t ? (t->kind == TrackKind::Audio) : false;

        cs.items.clear();
        cs.items.push_back({ std::string("Visible in render/playback: ") + (vis ? "ON" : "OFF"),
                             CtxAction::TrackToggleVisible, true });

        if (isAudio)
            cs.items.push_back({ std::string("Mute: ") + (mut ? "ON" : "OFF"),
                                 CtxAction::TrackToggleMute, true });

        cs.items.push_back({ std::string("Lock: ") + (lok ? "ON" : "OFF"),
                             CtxAction::TrackToggleLock, true });

        cs.items.push_back({ "Focus track", CtxAction::TrackFocus, true });

        cs.items.push_back({ "Rename track",    CtxAction::TrackRename,    (bool)w->onTrackRenameRequested });
        cs.items.push_back({ "Duplicate track", CtxAction::TrackDuplicate, (bool)w->onTrackDuplicateRequested });
        cs.items.push_back({ "Delete track",    CtxAction::TrackDelete,    (bool)w->onTrackDeleteRequested });

        ctxClampToWidget(w);
    }

    static bool ctxHandleEvent(TimelineWidget* w, SDL_Event& e, int mx, int my)
    {
        auto& cs = ctxFor(w);
        if (!cs.open) return false;

        
        
        
        const int px = mx;
        const int py = my;

        SDL_Rect mr = ctxRect(w);

        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            ctxClose(w);
            return true;
        }

        if (e.type == SDL_MOUSEMOTION)
        {
            cs.hover = -1;

            
            if (hitRectInflated(mr, px, py, 6))
            {
                int localY = py - (mr.y + 8);
                int idx = localY / cs.itemH;
                if (idx >= 0 && idx < (int)cs.items.size())
                    cs.hover = idx;
            }

            return true; 
        }

        if (e.type == SDL_MOUSEBUTTONDOWN)
        {
            
            if (!hitRectInflated(mr, px, py, 6))
            {
                ctxClose(w);
                return true;
            }

            
            if (e.button.button == SDL_BUTTON_RIGHT)
            {
                ctxClose(w);
                return true;
            }

            
            if (e.button.button == SDL_BUTTON_LEFT)
            {
                int localY = py - (mr.y + 8);
                int idx = localY / cs.itemH;

                if (idx >= 0 && idx < (int)cs.items.size())
                {
                    const auto& it = cs.items[idx];
                    if (it.enabled)
                    {
                        if (cs.kind == CtxKind::FocusFrames)
                        {
                            int fr = cs.frame;
                            switch (it.action)
                            {
                            case CtxAction::FocusInsert:
                                if (w->onFocusInsertFrame) w->onFocusInsertFrame(fr);
                                break;
                            case CtxAction::FocusDuplicate:
                                if (w->onFocusDuplicateFrame) w->onFocusDuplicateFrame(fr);
                                break;
                            case CtxAction::FocusDelete:
                                if (w->onFocusDeleteFrame) w->onFocusDeleteFrame(fr);
                                break;
                            default: break;
                            }
                        }
                        else if (cs.kind == CtxKind::TrackHeader)
                        {
                            TrackId tid = cs.trackId;
                            TimelineTrack* t = w->findTrack(tid);

                            switch (it.action)
                            {
                            case CtxAction::TrackToggleVisible:
                                if (t)
                                {
                                    t->visible = !t->visible;
                                    if (w->onTrackVisibilityToggled)
                                        w->onTrackVisibilityToggled(t->id, t->visible);
                                    if (w->onTrackRenderToggled)
                                        w->onTrackRenderToggled(t->id, t->visible);
                                }
                                break;

                            case CtxAction::TrackToggleMute:
                                if (t && t->kind == TrackKind::Audio)
                                {
                                    t->muted = !t->muted;
                                    if (w->onTrackMuteToggled)
                                        w->onTrackMuteToggled(t->id, t->muted);
                                }
                                break;

                            case CtxAction::TrackToggleLock:
                                if (t)
                                {
                                    t->locked = !t->locked;
                                    if (w->onTrackLockToggled)
                                        w->onTrackLockToggled(t->id, t->locked);
                                }
                                break;

                            case CtxAction::TrackFocus:
                                if (w->onTrackSelected) w->onTrackSelected(tid);
                                if (w->onTrackHeaderDoubleClick) w->onTrackHeaderDoubleClick(tid);
                                w->focusTrack(tid);
                                break;

                            case CtxAction::TrackRename:
                                if (w->onTrackRenameRequested) w->onTrackRenameRequested(tid);
                                break;

                            case CtxAction::TrackDuplicate:
                                if (w->onTrackDuplicateRequested) w->onTrackDuplicateRequested(tid);
                                break;

                            case CtxAction::TrackDelete:
                                if (w->onTrackDeleteRequested) w->onTrackDeleteRequested(tid);
                                break;

                            default: break;
                            }
                        }
                    }
                }

                ctxClose(w);
                return true;
            }

            return true; 
        }

        return false;
    }

    static void ctxDraw(TimelineWidget* w, SDL_Renderer* r)
    {
        auto& cs = ctxFor(w);
        if (!cs.open) return;

        SDL_Rect mr = ctxRect(w);

        fillRect(r, mr, SDL_Color{ 16,16,28,245 });
        drawRect(r, mr, SDL_Color{ 120,120,160,140 });

        int y = mr.y + 8;
        for (int i = 0; i < (int)cs.items.size(); ++i)
        {
            SDL_Rect row{ mr.x + 6, y, mr.w - 12, cs.itemH };
            bool hovered = (i == cs.hover);

            SDL_Color bg = hovered ? SDL_Color{ 70,120,255,60 } : SDL_Color{ 0,0,0,0 };
            if (bg.a) fillRect(r, row, bg);

            SDL_Color tc = cs.items[i].enabled ? SDL_Color{ 235,235,245,235 } : SDL_Color{ 200,200,210,120 };

            TimelineWidget::drawTextCenteredY(
                w->getFont(), r,
                [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { w->drawTextPublic(rr2, ss, xx, yy, cc); },
                cs.items[i].label, row, row.x + 10, tc
            );

            y += cs.itemH;
        }
    }

    
    
    
    int TimelineWidget::fontH(TTF_Font* f)
    {
        return f ? TTF_FontHeight(f) : 0;
    }

    void TimelineWidget::drawTextCenteredY(
        TTF_Font* font,
        SDL_Renderer* r,
        const std::function<void(SDL_Renderer*, const std::string&, int, int, SDL_Color)>& drawTextFn,
        const std::string& s,
        const SDL_Rect& box,
        int x,
        SDL_Color c)
    {
        int h = fontH(font);
        int y = box.y + (box.h - h) / 2;
        drawTextFn(r, s, x, y, c);
    }

    void TimelineWidget::setFps(int fps)
    {
        st.fps = strova::limits::clampProjectFps(fps);
    }

    void TimelineWidget::setTotalFrames(int frames)
    {
        st.totalFrames = strova::limits::clampTimelineFrames(frames);
        playheadFrame = clampi(playheadFrame, 0, (std::max)(0, st.totalFrames - 1));
        clampScroll();
    }

    void TimelineWidget::setPlayheadFrame(int frame)
    {
        playheadFrame = clampi(frame, 0, (std::max)(0, st.totalFrames - 1));
    }

    TrackId TimelineWidget::addTrack(TrackKind kind, const char* name)
    {
        if ((int)st.tracks.size() >= strova::limits::kMaxTimelineTracks)
            return 0;
        if (countTracksOfKind(st, kind) >= maxTracksForKind(kind))
            return 0;

        TimelineTrack t{};
        t.id = st.nextTrackId++;
        t.kind = kind;
        t.name = name ? name : "Track";
        t.visible = true;
        t.muted = false;
        t.locked = false;
        t.engineTrackId = 0;
        st.tracks.push_back(t);
        clampScroll();
        return t.id;
    }

    void TimelineWidget::clearTracks()
    {
        st.tracks.clear();
        st.nextTrackId = 1;
        clampScroll();
    }

    TimelineTrack* TimelineWidget::findTrack(TrackId id)
    {
        for (auto& t : st.tracks)
            if (t.id == id) return &t;
        return nullptr;
    }

    const TimelineTrack* TimelineWidget::findTrack(TrackId id) const
    {
        for (const auto& t : st.tracks)
            if (t.id == id) return &t;
        return nullptr;
    }

    ClipId TimelineWidget::addClip(TrackId trackId, int startFrame, int lengthFrames, const char* label)
    {
        if (trackId == 0)
            return 0;
        TimelineClip c{};
        c.id = st.nextClipId++;
        c.trackId = trackId;
        c.startFrame = (std::max)(0, startFrame);
        c.lengthFrames = (std::max)(1, lengthFrames);
        c.startFrame = clampi(c.startFrame, 0, (std::max)(0, strova::limits::kMaxTimelineFrames - 1));
        c.lengthFrames = clampi(c.lengthFrames, 1, strova::limits::kMaxTimelineFrames - c.startFrame);
        c.selected = false;
        c.label = label ? label : "Clip";
        st.clips.push_back(c);
        return c.id;
    }

    void TimelineWidget::clearClips()
    {
        st.clips.clear();
        st.nextClipId = 1;
    }

    TimelineClip* TimelineWidget::findClip(ClipId id)
    {
        for (auto& c : st.clips)
            if (c.id == id) return &c;
        return nullptr;
    }

    ClipId TimelineWidget::addClipOnNewTrack(TrackKind kind, const char* trackName,
        int startFrame, int lengthFrames, const char* clipLabel,
        TrackId* outTrackId)
    {
        TrackId tid = addTrack(kind, trackName);
        if (outTrackId) *outTrackId = tid;
        return addClip(tid, startFrame, lengthFrames, clipLabel);
    }

    void TimelineWidget::focusTrack(TrackId trackId)
    {
        if (!findTrack(trackId)) return;

        focus.enabled = true;
        focus.trackId = trackId;
        focus.scrubbing = false;

        focusMove.active = false;
        focusMove.fromFrame = 0;
        focusMove.toFrame = 0;
        focusMove.shiftAtStart = false;

        st.scrollY = 0;
        clampScroll();

        ctxClose(this);

        if (onFocusEnter) onFocusEnter(trackId);
    }

    void TimelineWidget::clearFocus()
    {
        if (!focus.enabled) return;
        focus.enabled = false;
        focus.scrubbing = false;
        focus.trackId = 0;

        focusMove.active = false;
        focusMove.fromFrame = 0;
        focusMove.toFrame = 0;
        focusMove.shiftAtStart = false;

        clampScroll();
        ctxClose(this);

        if (onFocusExit) onFocusExit();
    }

    SDL_Rect TimelineWidget::rulerRect() const
    {
        SDL_Rect r = rect;
        r.h = st.rulerH;
        return r;
    }

    SDL_Rect TimelineWidget::tracksRect() const
    {
        SDL_Rect r = rect;
        r.y += st.rulerH;
        r.h -= st.rulerH;
        return r;
    }

    bool TimelineWidget::pointIn(const SDL_Rect& r, int x, int y) const
    {
        return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
    }

    int TimelineWidget::trackRowIndexById(TrackId id) const
    {
        for (int i = 0; i < (int)st.tracks.size(); ++i)
            if (st.tracks[i].id == id) return i;
        return -1;
    }

    SDL_Rect TimelineWidget::trackHeaderRectOnScreen(int rowIndex) const
    {
        SDL_Rect tr = tracksRect();
        int y = tr.y + rowIndex * st.trackH - st.scrollY;
        return SDL_Rect{ tr.x, y, st.trackHeaderW, st.trackH };
    }

    int TimelineWidget::frameToX(int frame) const
    {
        SDL_Rect tr = tracksRect();
        int x0 = tr.x + st.trackHeaderW;
        float px = (float)frame * st.pxPerFrame;
        return x0 + (int)std::lround(px) - st.scrollX;
    }

    int TimelineWidget::xToFrame(int x) const
    {
        SDL_Rect tr = tracksRect();
        int x0 = tr.x + st.trackHeaderW;
        int local = (x - x0) + st.scrollX;
        float f = (float)local / (std::max)(0.0001f, st.pxPerFrame);
        int fr = (int)std::floor(f + 0.0001f);
        return clampi(fr, 0, (std::max)(0, st.totalFrames - 1));
    }

    SDL_Rect TimelineWidget::clipRectOnScreen(const TimelineClip& c) const
    {
        int row = trackRowIndexById(c.trackId);
        if (row < 0) row = 0;

        SDL_Rect tr = tracksRect();
        int y = tr.y + row * st.trackH - st.scrollY;
        int x = frameToX(c.startFrame);

        int w = (std::max)(1, (int)std::lround((float)c.lengthFrames * st.pxPerFrame));
        const int padY = 8;
        int h = st.trackH - padY * 2;

        return SDL_Rect{ x, y + padY, w, h };
    }

    int TimelineWidget::textW(const std::string& s) const
    {
        if (!font || s.empty()) return 0;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, s.c_str(), &w, &h) != 0) return 0;
        return w;
    }

    void TimelineWidget::drawText(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color c)
    {
        if (!r || !font || s.empty()) return;

        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s.c_str(), c);
        if (!surf) return;

        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (!tex) { SDL_FreeSurface(surf); return; }

        SDL_Rect dst{ x, y, surf->w, surf->h };
        SDL_FreeSurface(surf);

        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }

    static int focusCellWFromPx(float px)
    {
        
        
        int cw = (int)std::lround(px);
        cw = (std::max)(cw, 76);
        return cw;
    }

    float TimelineWidget::focusPx() const
    {
        
        return (std::max)(90.0f, st.pxPerFrame * 3.0f);
    }

    int TimelineWidget::focusFrameToX(int frame) const
    {
        SDL_Rect tr = tracksRect();
        int x0 = tr.x + st.trackHeaderW;

        int cw = focusCellWFromPx(focusPx());
        int px = frame * cw;

        return x0 + px - st.scrollX;
    }

    
    int TimelineWidget::focusXToFrame(int x) const
    {
        SDL_Rect tr = tracksRect();
        int x0 = tr.x + st.trackHeaderW;

        int cw = focusCellWFromPx(focusPx());

        int local = (x - x0) + st.scrollX;
        int fr = (cw > 0) ? (local / cw) : 0;

        return clampi(fr, 0, (std::max)(0, st.totalFrames - 1));
    }

    
    int TimelineWidget::focusXToFrameSnap(int x) const
    {
        SDL_Rect tr = tracksRect();
        int x0 = tr.x + st.trackHeaderW;

        int cw = focusCellWFromPx(focusPx());
        int local = (x - x0) + st.scrollX;

        if (cw <= 0) return 0;

        
        int fr = (local + (cw / 2)) / cw;
        return clampi(fr, 0, (std::max)(0, st.totalFrames - 1));
    }

    void TimelineWidget::clampScroll()
    {
        SDL_Rect tr = tracksRect();

        int bodyW = (std::max)(0, tr.w - st.trackHeaderW);
        int bodyH = (std::max)(0, tr.h);

        float px = focus.enabled ? focusPx() : st.pxPerFrame;

        int contentW = (int)std::lround((float)st.totalFrames * px);
        int trackRows = focus.enabled ? 1 : (int)st.tracks.size();
        int contentH = trackRows * st.trackH;

        int maxX = (std::max)(0, contentW - bodyW);
        int maxY = (std::max)(0, contentH - bodyH);

        st.scrollX = clampi(st.scrollX, 0, maxX);
        st.scrollY = clampi(st.scrollY, 0, maxY);

        if (focus.enabled) st.scrollY = 0;
    }

    
    
    
    void TimelineWidget::drawFocusHeader(SDL_Renderer* r, const SDL_Rect& rr)
    {
        FocusHeaderLayout L = calcFocusHeaderLayout(rr, st.trackHeaderW);

        fillRoundedRect(r, rr, safeRadius(rr, 12), SDL_Color{ 14,14,24,255 });
        strokeRoundedRect(r, rr, safeRadius(rr, 12), 1, SDL_Color{ 90,90,120,120 }, SDL_Color{ 14,14,24,255 });

        fillRoundedRect(r, L.left, safeRadius(L.left, 12), SDL_Color{ 12,12,22,255 });
        strokeRoundedRect(r, L.left, safeRadius(L.left, 12), 1, SDL_Color{ 90,90,120,120 }, SDL_Color{ 12,12,22,255 });

        fillRoundedRect(r, L.back, safeRadius(L.back, 12), SDL_Color{ 45,45,70,230 });
        strokeRoundedRect(r, L.back, safeRadius(L.back, 12), 1, SDL_Color{ 120,120,150,150 }, SDL_Color{ 45,45,70,230 });

        drawTextCenteredY(font, r,
            [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
            "Back", L.back, L.back.x + 18, SDL_Color{ 235,235,245,235 });

        const TimelineTrack* t = findTrack(focus.trackId);
        std::string title = "Focused: ";
        title += (t ? t->name : std::string("Track"));
        std::string shown = ellipsizeToWidth(font, title, L.title.w);

        drawTextCenteredY(font, r,
            [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
            shown, L.title, L.title.x, SDL_Color{ 235,235,245,230 });

        std::string info = "Frame " + std::to_string(playheadFrame + 1) + "/" + std::to_string(st.totalFrames);
        int iw = textW(info);
        int ix = (std::max)(L.info.x, L.info.x + (L.info.w - iw));
        drawTextCenteredY(font, r,
            [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
            info, L.info, ix, SDL_Color{ 220,220,235,200 });

        if (font && L.info.w > 180)
        {
            std::string hint = "Right-click frames  |  Shift+drag to move";
            int hw = textW(hint);
            int hx = (std::max)(L.title.x, L.info.x - 12 - hw);
            drawTextCenteredY(font, r,
                [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
                hint, rr, hx, SDL_Color{ 200,200,220,120 });
        }

        drawLine(r, rr.x + st.trackHeaderW, rr.y, rr.x + st.trackHeaderW, rr.y + rr.h, SDL_Color{ 90,90,120,120 });
    }

    void TimelineWidget::drawFocusFrames(SDL_Renderer* r, const SDL_Rect& tr)
    {
        
        const int focusH = st.trackH;

        SDL_Rect headerBg{ tr.x, tr.y, st.trackHeaderW, tr.h };
        fillRoundedRect(r, headerBg, safeRadius(headerBg, 12), SDL_Color{ 12,12,22,255 });
        strokeRoundedRect(r, headerBg, safeRadius(headerBg, 12), 1, SDL_Color{ 90,90,120,120 }, SDL_Color{ 12,12,22,255 });

        SDL_Rect rowHeader{ tr.x + 4, tr.y + 10, st.trackHeaderW - 8, focusH };
        fillRoundedRect(r, rowHeader, safeRadius(rowHeader, 10), SDL_Color{ 14,14,24,255 });
        strokeRoundedRect(r, rowHeader, safeRadius(rowHeader, 10), 1, SDL_Color{ 90,90,120,80 }, SDL_Color{ 14,14,24,255 });

        const TimelineTrack* t = findTrack(focus.trackId);

        SDL_Rect pill = kindPillRectForHeader(rowHeader);
        pill.h = std::max(pill.h, 24);
        pill.w += 12;
        fillRoundedRect(r, pill, safeRadius(pill, 10), SDL_Color{ 45,45,80,220 });
        strokeRoundedRect(r, pill, safeRadius(pill, 10), 1, SDL_Color{ 120,120,150,120 }, SDL_Color{ 45,45,80,220 });

        drawTextCenteredY(font, r,
            [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
            "FRAMES", pill, pill.x + 8, SDL_Color{ 235,235,245,220 });

        std::string nm = t ? t->name : std::string("Track");
        SDL_Rect nameBox = nameRectForHeader(font, rowHeader, 0);
        std::string shown = ellipsizeToWidth(font, nm, nameBox.w);

        drawTextCenteredY(font, r,
            [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
            shown, nameBox, nameBox.x, SDL_Color{ 235,235,245,220 });

        SDL_Rect body{ tr.x + st.trackHeaderW, tr.y, tr.w - st.trackHeaderW, tr.h };
        fillRoundedRect(r, body, safeRadius(body, 12), SDL_Color{ 9,9,16,255 });

        SDL_Rect strip{ body.x + 4, tr.y + 10, std::max(1, body.w - 8), focusH };
        fillRoundedRect(r, strip, safeRadius(strip, 10), SDL_Color{ 10,10,18,255 });
        strokeRoundedRect(r, strip, safeRadius(strip, 10), 1, SDL_Color{ 90,90,120,80 }, SDL_Color{ 10,10,18,255 });

        int cw = focusCellWFromPx(focusPx());

        int x0 = strip.x;
        int x1 = strip.x + strip.w;
        int f0 = focusXToFrame(x0);
        int f1 = focusXToFrame(x1);

        for (int f = f0; f <= f1; ++f)
        {
            int x = focusFrameToX(f);
            SDL_Rect cell{ x, strip.y, cw, strip.h };

            bool isPlay = (f == playheadFrame);
            SDL_Color cFill = isPlay ? SDL_Color{ 70,120,255,90 } : SDL_Color{ 12,12,22,255 };

            
            SDL_Rect cellOuter{ cell.x + 3, cell.y + 3, std::max(1, cell.w - 6), std::max(1, cell.h - 6) };
            
            int rad = 10;
            rad = std::min(rad, std::min(cellOuter.w, cellOuter.h) / 4);
            rad = std::max(4, rad);
            fillRoundedRect(r, cellOuter, rad, cFill);

            
            if (focusMove.active && f == focusMove.toFrame)
                fillRoundedRect(r, cellOuter, rad, SDL_Color{ 70,120,255,70 });

            SDL_Texture* thumb = nullptr;
            if (onFocusFrameThumbnail)
                thumb = onFocusFrameThumbnail(focus.trackId, f);

            if (thumb)
            {
                int tw = 0, th = 0;
                SDL_QueryTexture(thumb, nullptr, nullptr, &tw, &th);

                if (tw > 0 && th > 0)
                {
                    
                    SDL_Rect pad{ cellOuter.x + 6, cellOuter.y + 6, (std::max)(1, cellOuter.w - 12), (std::max)(1, cellOuter.h - 12) };

                    float sx = (float)pad.w / (float)tw;
                    float sy = (float)pad.h / (float)th;
                    float s = (std::min)(sx, sy);

                    int dw = (std::max)(1, (int)std::lround((float)tw * s));
                    int dh = (std::max)(1, (int)std::lround((float)th * s));

                    SDL_Rect dst{ pad.x + (pad.w - dw) / 2, pad.y + (pad.h - dh) / 2, dw, dh };

                    SDL_SetTextureBlendMode(thumb, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopy(r, thumb, nullptr, &dst);
                }
            }
            else
            {
                bool has = false;
                if (onFocusFrameHasContent)
                    has = onFocusFrameHasContent(focus.trackId, f);

                if (has)
                {
                    SDL_Rect dot{ cell.x + 8, cell.y + 8, (std::max)(1, cell.w - 16), (std::max)(1, cell.h - 16) };
                    fillRect(r, dot, SDL_Color{ 255,255,255,18 });
                }
            }

            

            if (cell.w >= 30 && (f % (std::max)(1, st.fps)) == 0)
            {
                std::string lab = std::to_string(f / (std::max)(1, st.fps));
                drawText(r, lab, cell.x + 6, cell.y + 3, SDL_Color{ 220,220,235,150 });
            }
        }

        
        if (focusMove.active)
        {
            
            int insX = focusFrameToX(focusMove.toFrame);
            drawLine(r, insX, strip.y, insX, strip.y + strip.h, SDL_Color{ 120,180,255,190 });

            SDL_Texture* ghost = nullptr;
            if (onFocusFrameThumbnail)
                ghost = onFocusFrameThumbnail(focus.trackId, focusMove.fromFrame);

            if (ghost)
            {
                int tw = 0, th = 0;
                SDL_QueryTexture(ghost, nullptr, nullptr, &tw, &th);
                if (tw > 0 && th > 0)
                {
                    int gw = 96;
                    int gh = 72;
                    int gx = lastMouseX - gw / 2;
                    int gy = lastMouseY - gh - 22;

                    gx = clampi(gx, rect.x + 8, rect.x + rect.w - gw - 8);
                    gy = clampi(gy, rect.y + 8, rect.y + rect.h - gh - 8);

                    SDL_Rect gOuter{ gx, gy, gw, gh };
                    int grad = 14;

                    fillRoundedRect(r, gOuter, grad, SDL_Color{ 18,18,32,210 });
                    strokeRoundedRect(r, gOuter, grad, 1, SDL_Color{ 120,160,255,180 }, SDL_Color{ 18,18,32,210 });

                    SDL_Rect gPad{ gOuter.x + 6, gOuter.y + 6, gOuter.w - 12, gOuter.h - 12 };
                    float sx = (float)gPad.w / (float)tw;
                    float sy = (float)gPad.h / (float)th;
                    float s = (std::min)(sx, sy);
                    int dw = (int)std::lround((float)tw * s);
                    int dh = (int)std::lround((float)th * s);
                    SDL_Rect dst{ gPad.x + (gPad.w - dw) / 2, gPad.y + (gPad.h - dh) / 2, dw, dh };

                    SDL_SetTextureBlendMode(ghost, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(ghost, 210);
                    SDL_RenderCopy(r, ghost, nullptr, &dst);
                    SDL_SetTextureAlphaMod(ghost, 255);
                }
            }
        }

        int pxLine = focusFrameToX(playheadFrame);
        drawLine(r, pxLine, tr.y, pxLine, tr.y + tr.h, SDL_Color{ 255,255,255,70 });
    }

    void TimelineWidget::drawRuler(SDL_Renderer* r)
    {
        SDL_Rect rr = rulerRect();

        fillRect(r, rr, SDL_Color{ 14,14,24,255 });
        drawRect(r, rr, SDL_Color{ 90,90,120,120 });

        if (focus.enabled)
        {
            drawFocusHeader(r, rr);
            int px = focusFrameToX(playheadFrame);
            drawLine(r, px, rr.y, px, rr.y + rect.h, SDL_Color{ 255,255,255,70 });
            return;
        }

        int x0 = rr.x + st.trackHeaderW;
        int x1 = rr.x + rr.w;

        int fr0 = xToFrame(x0);
        int fr1 = xToFrame(x1);

        int major = (std::max)(1, st.fps);
        int minor = (std::max)(1, major / 5);

        for (int f = fr0; f <= fr1; ++f)
        {
            int x = frameToX(f);

            bool isMajor = (f % major) == 0;
            bool isMinor = (f % minor) == 0;

            if (isMajor)
            {
                drawLine(r, x, rr.y + rr.h - 3, x, rr.y + 6, SDL_Color{ 255,255,255,40 });
                drawText(r, std::to_string(f / major), x + 4, rr.y + 8, SDL_Color{ 220,220,235,200 });
            }
            else if (isMinor)
            {
                drawLine(r, x, rr.y + rr.h - 3, x, rr.y + rr.h - 14, SDL_Color{ 255,255,255,25 });
            }
        }

        int px = frameToX(playheadFrame);
        drawLine(r, px, rr.y, px, rr.y + rect.h, SDL_Color{ 255,255,255,70 });
    }

    void TimelineWidget::drawTracks(SDL_Renderer* r)
    {
        SDL_Rect tr = tracksRect();

        fillRect(r, tr, SDL_Color{ 10,10,18,255 });
        drawRect(r, tr, SDL_Color{ 90,90,120,120 });

        if (focus.enabled)
        {
            drawFocusFrames(r, tr);
            return;
        }

        SDL_Rect headerBg{ tr.x, tr.y, st.trackHeaderW, tr.h };
        fillRect(r, headerBg, SDL_Color{ 12,12,22,255 });
        drawRect(r, headerBg, SDL_Color{ 90,90,120,120 });

        int rowsVis0 = (std::max)(0, st.scrollY / (std::max)(1, st.trackH));
        int rowsVis1 = (std::min)((int)st.tracks.size(), rowsVis0 + (tr.h / (std::max)(1, st.trackH)) + 2);

        for (int row = rowsVis0; row < rowsVis1; ++row)
        {
            SDL_Rect rowHeader = trackHeaderRectOnScreen(row);
            if (rowHeader.y + rowHeader.h < tr.y || rowHeader.y > tr.y + tr.h) continue;

            TimelineTrack& t = st.tracks[row];

            SDL_Color rowC = (row % 2) ? SDL_Color{ 14,14,24,255 } : SDL_Color{ 12,12,22,255 };
            fillRect(r, rowHeader, rowC);
            drawRect(r, rowHeader, SDL_Color{ 90,90,120,80 });

            SDL_Rect pill = kindPillRectForHeader(rowHeader);
            SDL_Color pillFill{ 40,40,60,220 };
            if (t.kind == TrackKind::Draw)     pillFill = SDL_Color{ 45,45,80,220 };
            if (t.kind == TrackKind::Flow)     pillFill = SDL_Color{ 55,45,85,220 };
            if (t.kind == TrackKind::FlowLink) pillFill = SDL_Color{ 48,76,112,220 };
            if (t.kind == TrackKind::Audio)    pillFill = SDL_Color{ 70,55,55,220 };

            
            if (!t.visible) pillFill.a = 110;

            fillRect(r, pill, pillFill);
            drawRect(r, pill, SDL_Color{ 120,120,150,120 });
            drawText(r, kindLabel(t.kind), pill.x + 10, pill.y + 3, SDL_Color{ 235,235,245,220 });

            
            std::string flags = buildStatusFlags(t);
            int flagsW = flags.empty() ? 0 : textW(flags);

            SDL_Rect nameRc = nameRectForHeader(font, rowHeader, flagsW);

            SDL_Color nameC = SDL_Color{ 235,235,245,220 };
            if (t.locked)   nameC = SDL_Color{ 235,235,245,150 };
            if (!t.visible) nameC = SDL_Color{ 235,235,245,120 };
            if (t.kind == TrackKind::Audio && t.muted)
                nameC = SDL_Color{ 210,210,220,140 };

            std::string shownName = ellipsizeToWidth(font, t.name, nameRc.w);
            drawTextCenteredY(font, r,
                [&](SDL_Renderer* rr2, const std::string& ss, int xx, int yy, SDL_Color cc) { drawText(rr2, ss, xx, yy, cc); },
                shownName, nameRc, nameRc.x, nameC);

            if (!flags.empty())
            {
                int fx = (rowHeader.x + rowHeader.w) - 12 - flagsW;
                drawText(r, flags, fx, rowHeader.y + (rowHeader.h - fontH(font)) / 2, SDL_Color{ 235,235,245,110 });
            }

            SDL_Rect bodyRow{ tr.x + st.trackHeaderW, rowHeader.y, tr.w - st.trackHeaderW, rowHeader.h };
            SDL_Color bodyC = (row % 2) ? SDL_Color{ 10,10,18,255 } : SDL_Color{ 9,9,16,255 };
            fillRect(r, bodyRow, bodyC);

            int f0 = xToFrame(bodyRow.x);
            int f1 = xToFrame(bodyRow.x + bodyRow.w);

            for (int f = f0; f <= f1; ++f)
            {
                int x = frameToX(f);
                bool major = (f % (std::max)(1, st.fps)) == 0;
                SDL_Color lc = major ? SDL_Color{ 255,255,255,18 } : SDL_Color{ 255,255,255,10 };
                drawLine(r, x, bodyRow.y, x, bodyRow.y + bodyRow.h, lc);
            }
        }

        
        for (const auto& c : st.clips)
        {
            int row = trackRowIndexById(c.trackId);
            if (row < 0) continue;

            SDL_Rect rc = clipRectOnScreen(c);
            if (rc.x + rc.w < tr.x + st.trackHeaderW || rc.x > tr.x + tr.w) continue;
            if (rc.y + rc.h < tr.y || rc.y > tr.y + tr.h) continue;

            const TimelineTrack* t = findTrack(c.trackId);
            bool locked = t ? t->locked : false;
            bool visible = t ? t->visible : true;

            SDL_Color fill = c.selected ? SDL_Color{ 70,120,255,200 } : SDL_Color{ 60,60,90,200 };
            SDL_Color border = c.selected ? SDL_Color{ 190,210,255,220 } : SDL_Color{ 130,130,160,180 };

            if (locked)
            {
                fill.a = (Uint8)(std::min)(255, (int)fill.a / 2);
                border.a = (Uint8)(std::min)(255, (int)border.a / 2);
            }
            if (!visible)
            {
                fill.a = (Uint8)(std::min)(255, (int)fill.a / 3);
                border.a = (Uint8)(std::min)(255, (int)border.a / 3);
            }

            fillRect(r, rc, fill);
            drawRect(r, rc, border);

            std::string lab = c.label;
            if ((int)lab.size() > 32) lab.resize(32);

            int maxLabW = (std::max)(0, rc.w - 16);
            std::string shownLab = ellipsizeToWidth(font, lab, maxLabW);

            drawText(r, shownLab, rc.x + 10, rc.y + 10, SDL_Color{ 245,245,250,220 });

            SDL_Color hcol = locked ? SDL_Color{ 255,255,255,20 } : SDL_Color{ 255,255,255,45 };
            drawLine(r, rc.x + 3, rc.y + 5, rc.x + 3, rc.y + rc.h - 6, hcol);
            drawLine(r, rc.x + rc.w - 4, rc.y + 5, rc.x + rc.w - 4, rc.y + rc.h - 6, hcol);
        }

        int px = frameToX(playheadFrame);
        drawLine(r, px, tr.y, px, tr.y + tr.h, SDL_Color{ 255,255,255,60 });
    }

    
    
    
    bool TimelineWidget::handleFocusEvent(SDL_Event& e, int mx, int my)
    {
        SDL_Rect rr = rulerRect();
        SDL_Rect tr = tracksRect();

        FocusHeaderLayout L = calcFocusHeaderLayout(rr, st.trackHeaderW);

        
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            SDL_Rect backHit = inflateRect(L.back, 14);
            if (hitRectInflated(rr, mx, my, 6) && sdlPointInRect(backHit, mx, my))
            {
                clearFocus();
                return true;
            }
        }

        bool overTimeline = pointIn(rect, mx, my);

        if (e.type == SDL_MOUSEWHEEL && overTimeline)
        {
            SDL_Keymod mod = SDL_GetModState();
            bool ctrl = (mod & KMOD_CTRL) != 0;

            const int step = 110;

            if (ctrl)
            {
                float z = (e.wheel.y > 0) ? 1.12f : 0.90f;
                float newPx = st.pxPerFrame * z;
                newPx = clampf(newPx, st.minPxPerFrame, st.maxPxPerFrame);

                if (std::fabs(newPx - st.pxPerFrame) > 0.00001f)
                {
                    int anchorFrame = focusXToFrameSnap(mx);
                    st.pxPerFrame = newPx;

                    int xAfter = focusFrameToX(anchorFrame);
                    int dx = xAfter - mx;
                    st.scrollX += dx;

                    clampScroll();
                }
                return true;
            }

            if (e.wheel.x != 0) st.scrollX -= e.wheel.x * step;
            st.scrollX -= e.wheel.y * step;
            clampScroll();
            return true;
        }

        
        
        const int focusH = st.trackH;
        SDL_Rect body{ tr.x + st.trackHeaderW, tr.y, tr.w - st.trackHeaderW, tr.h };
        SDL_Rect strip{ body.x, tr.y + 10, body.w, focusH };

        SDL_Rect stripHit = inflateRect(strip, 6);

        
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT && sdlPointInRect(stripHit, mx, my))
        {
            int fr = focusXToFrameSnap(mx);
            if (onFocusFrameRightClick && onFocusFrameRightClick(focus.trackId, fr))
                return true;
            ctxOpenFocusFrames(this, mx, my, fr);
            return true;
        }

        SDL_Keymod mod = SDL_GetModState();
        bool shift = (mod & KMOD_SHIFT) != 0;

        
        const TimelineTrack* ft = findTrack(focus.trackId);
        bool focusLocked = ft ? ft->locked : false;

        
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && shift && sdlPointInRect(stripHit, mx, my))
        {
            if (focusLocked) return true;

            focusMove.active = true;
            focusMove.shiftAtStart = true;
            focusMove.fromFrame = focusXToFrameSnap(mx);
            focusMove.toFrame = focusMove.fromFrame;
            focus.scrubbing = false;
            return true;
        }

        
        if (e.type == SDL_MOUSEMOTION && focusMove.active)
        {
            focusMove.toFrame = focusXToFrameSnap(mx);
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && focusMove.active)
        {
            int from = focusMove.fromFrame;
            int to = focusMove.toFrame;

            focusMove.active = false;
            focusMove.shiftAtStart = false;

            if (!focusLocked && from != to && onFocusMoveFrame)
                onFocusMoveFrame(from, to);

            return true;
        }

        
        if (!focusMove.active && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && sdlPointInRect(stripHit, mx, my))
        {
            const int clickedFrame = focusXToFrameSnap(mx);
            if (onFocusFrameLeftClick)
            {
                onFocusFrameLeftClick(focus.trackId, clickedFrame);
                return true;
            }
            focus.scrubbing = true;
            setPlayheadFrame(clickedFrame);
            return true;
        }

        if (e.type == SDL_MOUSEMOTION && focus.scrubbing)
        {
            setPlayheadFrame(focusXToFrameSnap(mx));
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            focus.scrubbing = false;
        }

        
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_MIDDLE && pointIn(tr, mx, my))
        {
            panning = true;
            panStartX = mx;
            panStartY = my;
            panScrollX0 = st.scrollX;
            panScrollY0 = 0;
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_MIDDLE)
        {
            panning = false;
        }
        if (e.type == SDL_MOUSEMOTION && panning)
        {
            st.scrollX = panScrollX0 + (panStartX - mx);
            clampScroll();
            return true;
        }

        return false;
    }

    
    
    
    bool TimelineWidget::handleEvent(SDL_Event& e, int mx, int my)
    {
        if (rect.w <= 0 || rect.h <= 0) return false;

        
        lastMouseX = mx;
        lastMouseY = my;

        auto& cs = ctxFor(this);
        if (cs.open)
            return ctxHandleEvent(this, e, mx, my);

        
        if (ctxHandleEvent(this, e, mx, my))
            return true;

        if (focus.enabled)
            return handleFocusEvent(e, mx, my);

        SDL_Rect rr = rulerRect();
        SDL_Rect tr = tracksRect();

        bool overTimeline = pointIn(rect, mx, my);
        bool overTracks = pointIn(tr, mx, my);

        if (e.type == SDL_MOUSEWHEEL && overTimeline)
        {
            SDL_Keymod mod = SDL_GetModState();
            bool shift = (mod & KMOD_SHIFT) != 0;
            bool ctrl = (mod & KMOD_CTRL) != 0;

            const int step = 110;

            if (ctrl)
            {
                float z = (e.wheel.y > 0) ? 1.12f : 0.90f;

                float newPx = st.pxPerFrame * z;
                newPx = clampf(newPx, st.minPxPerFrame, st.maxPxPerFrame);

                if (std::fabs(newPx - st.pxPerFrame) > 0.00001f)
                {
                    int anchorFrame = xToFrame(mx);

                    float oldPx = st.pxPerFrame;
                    st.pxPerFrame = newPx;

                    int xAfter = frameToX(anchorFrame);
                    int dx = xAfter - mx;
                    st.scrollX += dx;

                    if (oldPx < st.pxPerFrame) st.scrollX = (std::max)(0, st.scrollX);
                    clampScroll();
                }
                return true;
            }

            if (shift)
            {
                st.scrollY -= e.wheel.y * step;
            }
            else
            {
                if (e.wheel.x != 0) st.scrollX -= e.wheel.x * step;
                st.scrollX -= e.wheel.y * step;
            }

            clampScroll();
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_MIDDLE && overTracks)
        {
            panning = true;
            panStartX = mx;
            panStartY = my;
            panScrollX0 = st.scrollX;
            panScrollY0 = st.scrollY;
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_MIDDLE)
        {
            panning = false;
        }

        if (e.type == SDL_MOUSEMOTION && panning)
        {
            st.scrollX = panScrollX0 + (panStartX - mx);
            st.scrollY = panScrollY0 + (panStartY - my);
            clampScroll();
            return true;
        }

        
        if (overTracks && e.type == SDL_MOUSEBUTTONDOWN)
        {
            int row0 = (std::max)(0, st.scrollY / (std::max)(1, st.trackH));
            int row1 = (std::min)((int)st.tracks.size(), row0 + (tr.h / (std::max)(1, st.trackH)) + 2);

            for (int row = row0; row < row1; ++row)
            {
                SDL_Rect hdr = trackHeaderRectOnScreen(row);

                
                SDL_Rect hdrHit = inflateRect(hdr, 6);
                if (!sdlPointInRect(hdrHit, mx, my)) continue;

                TrackId clickedId = st.tracks[row].id;

                std::string flags = buildStatusFlags(st.tracks[row]);
                int flagsW = flags.empty() ? 0 : textW(flags);

                SDL_Rect bodyRc = headerBodyRectForHeader(font, hdr, flagsW);
                SDL_Rect bodyHit = inflateRect(bodyRc, 6);

                
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    ctxOpenTrackHeader(this, mx, my, clickedId);
                    return true;
                }

                
                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    bool isDbl = (e.button.clicks >= 2);
                    if (!isDbl)
                    {
                        Uint32 now = SDL_GetTicks();
                        if (dbl.lastTrackId == clickedId && (now - dbl.lastTicks) < 320)
                            isDbl = true;
                        dbl.lastTrackId = clickedId;
                        dbl.lastTicks = now;
                    }

                    if (isDbl && sdlPointInRect(bodyHit, mx, my))
                    {
                        if (onTrackHeaderDoubleClick) onTrackHeaderDoubleClick(clickedId);
                        focusTrack(clickedId);
                        return true;
                    }

                    if (onTrackSelected) onTrackSelected(clickedId);
                    if (st.tracks[row].kind == TrackKind::FlowLink)
                    {
                        focusTrack(clickedId);
                        return true;
                    }
                    return true;
                }
            }
        }

        
        hotClip = 0;
        for (const auto& c : st.clips)
        {
            SDL_Rect rc = clipRectOnScreen(c);
            SDL_Rect hit = inflateRect(rc, 3);
            if (sdlPointInRect(hit, mx, my)) { hotClip = c.id; break; }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && overTracks)
        {
            if (hotClip != 0)
            {
                for (auto& c : st.clips) c.selected = false;

                TimelineClip* c = findClip(hotClip);
                if (!c) return true;

                const TimelineTrack* t = findTrack(c->trackId);
                c->selected = true;

                SDL_Rect rc = clipRectOnScreen(*c);

                int edge = 9;
                bool onL = (mx >= rc.x && mx < rc.x + edge);
                bool onR = (mx >= rc.x + rc.w - edge && mx < rc.x + rc.w);

                dragClip = c->id;
                dragMouseStartX = mx;
                dragClipStartFrame0 = c->startFrame;
                dragClipLen0 = c->lengthFrames;

                dragMode = 1;
                if (onL) dragMode = 2;
                else if (onR) dragMode = 3;

                if (t && t->locked)
                {
                    dragClip = 0;
                    dragMode = 0;
                }

                return true;
            }
            else
            {
                for (auto& c : st.clips) c.selected = false;
                return true;
            }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointIn(rr, mx, my))
        {
            setPlayheadFrame(xToFrame(mx));
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            if (dragClip != 0)
            {
                TimelineClip* c = findClip(dragClip);
                if (c && dragMode == 1)
                {
                    int oldStart = dragClipStartFrame0;
                    int newStart = c->startFrame;

                    if (newStart != oldStart)
                    {
                        if (onClipMoved) onClipMoved(c->id, oldStart, newStart);
                    }
                }
            }

            dragClip = 0;
            dragMode = 0;
        }

        if (e.type == SDL_MOUSEMOTION && dragClip != 0 && dragMode != 0)
        {
            TimelineClip* c = findClip(dragClip);
            if (!c) return true;

            int dx = mx - dragMouseStartX;
            int df = (int)std::lround((float)dx / (std::max)(0.0001f, st.pxPerFrame));

            if (dragMode == 1)
            {
                c->startFrame = (std::max)(0, dragClipStartFrame0 + df);
            }
            else if (dragMode == 2)
            {
                int newStart = (std::max)(0, dragClipStartFrame0 + df);
                int end = dragClipStartFrame0 + dragClipLen0;
                if (newStart >= end) newStart = end - 1;

                c->startFrame = newStart;
                c->lengthFrames = (std::max)(1, end - newStart);
            }
            else if (dragMode == 3)
            {
                c->lengthFrames = (std::max)(1, dragClipLen0 + df);
            }

            return true;
        }

        return false;
    }

    void TimelineWidget::draw(SDL_Renderer* r)
    {
        if (!r) return;

        st.trackHeaderW = std::clamp(st.trackHeaderW, 240, 340);
        st.rulerH = std::clamp(st.rulerH, 56, 72);
        st.trackH = std::clamp(st.trackH, 64, 92);

        st.pxPerFrame = clampf(st.pxPerFrame, 6.0f, st.maxPxPerFrame);

        clampScroll();

        SDL_RenderSetClipRect(r, &rect);
        drawRuler(r);
        drawTracks(r);

        
        ctxDraw(this, r);

        SDL_RenderSetClipRect(r, nullptr);
    }

} 
