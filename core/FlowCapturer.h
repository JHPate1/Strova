/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/FlowCapturer.h
   Module:      Core
   Purpose:     Flow capture helpers used by export and drawing flows.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <functional>
#include "SDL.h"
#include "StrovaLimits.h"

struct CaptureSample {
    float x = 0.0f, y = 0.0f;
    float t = 0.0f;
    float pressure = 1.0f;
    SDL_Color color{ 255, 255, 255, 255 };
};

static inline float flow_dist2(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return dx * dx + dy * dy;
}

static inline float flow_dist(float ax, float ay, float bx, float by) {
    return std::sqrt(flow_dist2(ax, ay, bx, by));
}

static inline float flow_clampf(float v, float a, float b) {
    return std::max(a, std::min(b, v));
}



struct FlowLinkSample {
    float posX = 0.0f;
    float posY = 0.0f;
    float rotation = 0.0f;
    float t = 0.0f;
};

struct FlowLinkFrameSample {
    int frameOffset = 0;
    float posX = 0.0f;
    float posY = 0.0f;
    float rotation = 0.0f;
};


enum class FlowLinkSmoothingMode : int {
    None = 0,
    Linear = 1,
    CatmullRom = 2
};

struct FlowLinkClip {
    int targetTrackId = 0;
    int startFrame = 0;
    int duration = 0;
    bool loop = false;
    bool relative = false;
    int laneIndex = 0;
    float basePosX = 0.0f;
    float basePosY = 0.0f;
    float baseRotation = 0.0f;
    std::vector<FlowLinkFrameSample> samples;

    inline bool empty() const {
        return targetTrackId == 0 || duration <= 0 || samples.empty();
    }
};

struct FlowSettings {
    int projectFps = 30;
    float resampleSpacingPx = 2.0f;
    float catmullAlpha = 0.5f;
    int catmullSubdiv = 6;
    float distPerFramePx = 40.0f;
    int minFrames = 8;
    int maxFrames = strova::limits::kDefaultFlowMaxFrames;
    float minMovePx = 0.7f;
    FlowLinkSmoothingMode flowLinkSmoothing = FlowLinkSmoothingMode::CatmullRom;
    bool captureRotation = true;
    bool capturePosition = true;
    bool stitchMode = false;
    bool overlayMode = true;
    bool autoFrameGeneration = true;
    float motionDampening = 0.0f;
    float interpolationStrength = 1.0f;
    int maxStrokeSamples = strova::limits::kMaxFlowSamplesPerCapture;
    int maxFlowLinkSamples = strova::limits::kMaxFlowLinkSamplesPerCapture;
    float minSampleIntervalSec = 1.0f / 240.0f;
    float minFlowLinkRotationDeltaDeg = 0.15f;

    inline void clampToSafeLimits() {
        projectFps = strova::limits::clampProjectFps(projectFps);
        resampleSpacingPx = flow_clampf(resampleSpacingPx, 0.25f, 128.0f);
        catmullAlpha = flow_clampf(catmullAlpha, 0.0f, 1.0f);
        catmullSubdiv = std::clamp(catmullSubdiv, 1, 24);
        distPerFramePx = flow_clampf(distPerFramePx, 1.0f, 4096.0f);
        minFrames = std::clamp(minFrames, 2, strova::limits::kMaxFlowGeneratedFrames);
        maxFrames = std::clamp(maxFrames, minFrames, strova::limits::kMaxFlowGeneratedFrames);
        minMovePx = flow_clampf(minMovePx, 0.05f, 128.0f);
        motionDampening = flow_clampf(motionDampening, 0.0f, 1.0f);
        interpolationStrength = flow_clampf(interpolationStrength, 0.0f, 4.0f);
        maxStrokeSamples = std::clamp(maxStrokeSamples, 64, strova::limits::kMaxFlowSamplesPerCapture);
        maxFlowLinkSamples = std::clamp(maxFlowLinkSamples, 64, strova::limits::kMaxFlowLinkSamplesPerCapture);
        minSampleIntervalSec = flow_clampf(minSampleIntervalSec, 1.0f / 1000.0f, 0.25f);
        minFlowLinkRotationDeltaDeg = flow_clampf(minFlowLinkRotationDeltaDeg, 0.01f, 45.0f);
    }
};

struct FlowStats {
    bool valid = false;
    int frames = 0;
    float duration = 0.0f;
    int fpsUsed = 0;
    float totalDist = 0.0f;
};

class FlowCapturer {
public:
    enum class CaptureMode : int {
        Off = 0,
        Armed,
        Recording
    };

    bool armed = false;
    bool capturing = false;
    FlowSettings settings;
    FlowStats lastStats;

    
    std::function<SDL_Color(float x, float y)> colorSampler;

    inline void setProjectFps(int fps) {
        settings.projectFps = strova::limits::clampProjectFps(fps);
        settings.clampToSafeLimits();
    }

    inline CaptureMode strokeState() const {
        if (capturing) return CaptureMode::Recording;
        return armed ? CaptureMode::Armed : CaptureMode::Off;
    }

    inline CaptureMode flowLinkState() const {
        if (flowLinkCapturing) return CaptureMode::Recording;
        return armed ? CaptureMode::Armed : CaptureMode::Off;
    }

    inline void arm() {
        armed = true;
        capturing = false;
        flowLinkCapturing = false;
        samples.clear();
        flowLinkSamples.clear();
        startTime = 0.0f;
        flowLinkStartTime = 0.0f;
    }

    inline void disarm() {
        armed = false;
        capturing = false;
        flowLinkCapturing = false;
        samples.clear();
        flowLinkSamples.clear();
    }

    inline void onStrokeBegin(float x, float y, float nowSeconds) {
        if (!armed || capturing) return;
        capturing = true;
        samples.clear();
        startTime = nowSeconds;
        pushSample(x, y, nowSeconds);
    }

    inline void onStrokeMove(float x, float y, float nowSeconds) {
        if (!capturing) return;
        if (!samples.empty()) {
            const auto& p = samples.back();
            const float dt = nowSeconds - startTime - p.t;
            if (flow_dist2(p.x, p.y, x, y) < (settings.minMovePx * settings.minMovePx) &&
                dt < settings.minSampleIntervalSec) return;
        }
        pushSample(x, y, nowSeconds);
    }

    inline void onStrokeEnd(float ) {
        if (!capturing) return;
        capturing = false;
        if (samples.size() < 2) {
            samples.clear();
        } else {
            compactStrokeSamples();
        }
    }

    inline bool hasResult() const { return !capturing && samples.size() >= 2; }
    inline const std::vector<CaptureSample>& getSamples() const { return samples; }
    inline void clearResult() { samples.clear(); }


    inline void onFlowLinkBegin(float posX, float posY, float rotationDeg, float nowSeconds) {
        if (!armed || flowLinkCapturing) return;
        flowLinkCapturing = true;
        flowLinkSamples.clear();
        flowLinkStartTime = nowSeconds;
        pushFlowLinkSample(posX, posY, rotationDeg, nowSeconds);
    }

    inline void onFlowLinkMove(float posX, float posY, float rotationDeg, float nowSeconds) {
        if (!flowLinkCapturing) return;
        if (!flowLinkSamples.empty()) {
            const auto& p = flowLinkSamples.back();
            const float move2 = flow_dist2(p.posX, p.posY, posX, posY);
            const float rotDelta = std::fabs(rotationDeg - p.rotation);
            const float dt = nowSeconds - flowLinkStartTime - p.t;
            if (move2 < (settings.minMovePx * settings.minMovePx) &&
                rotDelta < settings.minFlowLinkRotationDeltaDeg &&
                dt < settings.minSampleIntervalSec) return;
        }
        pushFlowLinkSample(posX, posY, rotationDeg, nowSeconds);
    }

    inline void onFlowLinkEnd(float) {
        if (!flowLinkCapturing) return;
        flowLinkCapturing = false;
        if (flowLinkSamples.size() < 2) {
            flowLinkSamples.clear();
        } else {
            compactFlowLinkSamples();
        }
    }

    inline bool hasFlowLinkResult() const { return !flowLinkCapturing && flowLinkSamples.size() >= 2; }
    inline const std::vector<FlowLinkSample>& getFlowLinkSamples() const { return flowLinkSamples; }
    inline void clearFlowLinkResult() { flowLinkSamples.clear(); flowLinkCapturing = false; }

    inline FlowLinkClip buildFlowLinkClipProjectFps(int startFrame, int targetTrackId, float basePosX = 0.0f, float basePosY = 0.0f, float baseRotation = 0.0f, bool relative = true) {
        FlowLinkClip clip{};
        clip.startFrame = std::max(0, startFrame);
        clip.targetTrackId = targetTrackId;
        clip.basePosX = basePosX;
        clip.basePosY = basePosY;
        clip.baseRotation = baseRotation;
        clip.relative = relative;
        lastStats = {};
        if (flowLinkSamples.size() < 2 || targetTrackId == 0) return clip;
        settings.clampToSafeLimits();

        float duration = flowLinkSamples.back().t;
        if (duration < 0.0001f) duration = 0.0001f;

        float totalDist = 0.0f;
        for (size_t i = 1; i < flowLinkSamples.size(); ++i) {
            totalDist += flow_dist(flowLinkSamples[i - 1].posX, flowLinkSamples[i - 1].posY, flowLinkSamples[i].posX, flowLinkSamples[i].posY);
        }

        const int fps = std::max(1, settings.projectFps);
        int byTime = (int)std::ceil(duration * (float)fps) + 1;
        int byDist = (settings.distPerFramePx > 1.0f)
            ? ((int)std::ceil(totalDist / settings.distPerFramePx) + 1)
            : byTime;

        int frameCount = std::max({ settings.minFrames, byTime, byDist });
        frameCount = std::min(frameCount, settings.maxFrames);
        frameCount = std::max(frameCount, 2);

        std::vector<FlowLinkSample> smooth = flowLinkSamples;
        if (settings.flowLinkSmoothing == FlowLinkSmoothingMode::CatmullRom)
            smooth = catmullRomResampleFlowLink(flowLinkSamples);
        if (smooth.size() < 2) return clip;
        for (size_t i = 1; i < smooth.size(); ++i) {
            if (smooth[i].t < smooth[i - 1].t) smooth[i].t = smooth[i - 1].t;
        }

        clip.duration = frameCount;
        clip.samples.reserve((size_t)frameCount);
        for (int fi = 0; fi < frameCount; ++fi) {
            float tTarget = (frameCount <= 1) ? duration : (duration * (float)fi) / (float)(frameCount - 1);
            auto it = std::lower_bound(smooth.begin(), smooth.end(), tTarget,
                [](const FlowLinkSample& s, float t) { return s.t < t; });
            FlowLinkSample p{};
            if (it != smooth.end()) {
                if (it == smooth.begin()) {
                    p = *it;
                    p.t = tTarget;
                }
                else {
                    const FlowLinkSample& a = *(it - 1);
                    const FlowLinkSample& b = *it;
                    float dt = std::max(0.000001f, b.t - a.t);
                    float u = flow_clampf((tTarget - a.t) / dt, 0.0f, 1.0f);
                    p = lerpFlowLinkSample(a, b, u);
                    p.t = tTarget;
                }
            }
            else if (!smooth.empty()) {
                p = smooth.back();
                p.t = tTarget;
            }
            if (!settings.capturePosition && !clip.samples.empty())
            {
                p.posX = clip.samples.front().posX;
                p.posY = clip.samples.front().posY;
            }
            if (!settings.captureRotation && !clip.samples.empty())
                p.rotation = clip.samples.front().rotation;
            if (clip.relative)
            {
                p.posX -= clip.basePosX;
                p.posY -= clip.basePosY;
                p.rotation -= clip.baseRotation;
            }
            clip.samples.push_back({ fi, p.posX, p.posY, p.rotation });
        }

        lastStats.valid = true;
        lastStats.frames = frameCount;
        lastStats.duration = duration;
        lastStats.fpsUsed = fps;
        lastStats.totalDist = totalDist;
        return clip;
    }

    inline std::vector<std::vector<CaptureSample>> buildFramesProjectFps() {
        lastStats = {};
        if (samples.size() < 2) return {};
        settings.clampToSafeLimits();

        float duration = samples.back().t;
        if (duration < 0.0001f) duration = 0.0001f;

        float totalDist = 0.0f;
        for (size_t i = 1; i < samples.size(); ++i) {
            totalDist += flow_dist(samples[i - 1].x, samples[i - 1].y, samples[i].x, samples[i].y);
        }

        const int fps = std::max(1, settings.projectFps);
        int byTime = (int)std::ceil(duration * (float)fps) + 1;
        int byDist = (settings.distPerFramePx > 1.0f)
            ? ((int)std::ceil(totalDist / settings.distPerFramePx) + 1)
            : byTime;

        int frameCount = std::max({ settings.minFrames, byTime, byDist });
        frameCount = std::min(frameCount, settings.maxFrames);
        frameCount = std::max(frameCount, 2);

        std::vector<CaptureSample> smooth = catmullRomResample(samples);
        if (smooth.size() < 2) return {};

        for (size_t i = 1; i < smooth.size(); ++i) {
            if (smooth[i].t < smooth[i - 1].t) smooth[i].t = smooth[i - 1].t;
        }

        std::vector<std::vector<CaptureSample>> framesOut;
        framesOut.reserve((size_t)frameCount);

        for (int fi = 0; fi < frameCount; ++fi) {
            float tTarget = (frameCount <= 1) ? duration : (duration * (float)fi) / (float)(frameCount - 1);
            std::vector<CaptureSample> framePts;
            framePts.reserve(256);

            auto it = std::lower_bound(smooth.begin(), smooth.end(), tTarget,
                [](const CaptureSample& s, float t) { return s.t < t; });

            framePts.insert(framePts.end(), smooth.begin(), it);

            if (it != smooth.end()) {
                if (it == smooth.begin()) {
                    CaptureSample p = *it;
                    p.t = tTarget;
                    framePts.push_back(p);
                }
                else {
                    const CaptureSample& a = *(it - 1);
                    const CaptureSample& b = *it;
                    float dt = std::max(0.000001f, b.t - a.t);
                    float u = flow_clampf((tTarget - a.t) / dt, 0.0f, 1.0f);
                    CaptureSample p = lerpSample(a, b, u);
                    p.t = tTarget;
                    framePts.push_back(p);
                }
            }
            else if (!smooth.empty()) {
                CaptureSample p = smooth.back();
                p.t = tTarget;
                framePts.push_back(p);
            }

            framesOut.push_back(std::move(framePts));
        }

        lastStats.valid = true;
        lastStats.frames = (int)framesOut.size();
        lastStats.duration = duration;
        lastStats.fpsUsed = fps;
        lastStats.totalDist = totalDist;
        return framesOut;
    }

private:
    inline void pushSample(float x, float y, float nowSeconds) {
        CaptureSample s;
        s.x = x;
        s.y = y;
        s.t = nowSeconds - startTime;

        
        if (colorSampler) {
            s.color = colorSampler(x, y);
        }

        const int maxSamples = std::max(16, settings.maxStrokeSamples);
        if ((int)samples.size() >= maxSamples) {
            samples.back() = s;
            return;
        }
        samples.push_back(s);
    }

    inline void compactStrokeSamples() {
        normalizeSampleTimes(samples);
        if (samples.size() <= 2) return;

        std::vector<CaptureSample> compact;
        compact.reserve(samples.size());
        compact.push_back(samples.front());
        for (size_t i = 1; i + 1 < samples.size(); ++i) {
            const auto& prev = compact.back();
            const auto& cur = samples[i];
            const float dt = cur.t - prev.t;
            if (flow_dist2(prev.x, prev.y, cur.x, cur.y) < (settings.minMovePx * settings.minMovePx) &&
                dt < settings.minSampleIntervalSec) {
                continue;
            }
            compact.push_back(cur);
        }
        compact.push_back(samples.back());
        samples.swap(compact);
    }

    static inline CaptureSample lerpSample(const CaptureSample& a, const CaptureSample& b, float t) {
        CaptureSample o;
        o.t = a.t + (b.t - a.t) * t;
        o.x = a.x + (b.x - a.x) * t;
        o.y = a.y + (b.y - a.y) * t;
        o.pressure = a.pressure + (b.pressure - a.pressure) * t;

        
        o.color.r = (Uint8)std::lround(a.color.r * (1.0f - t) + b.color.r * t);
        o.color.g = (Uint8)std::lround(a.color.g * (1.0f - t) + b.color.g * t);
        o.color.b = (Uint8)std::lround(a.color.b * (1.0f - t) + b.color.b * t);
        o.color.a = (Uint8)std::lround(a.color.a * (1.0f - t) + b.color.a * t);

        return o;
    }

    static inline CaptureSample lerp(const CaptureSample& a, const CaptureSample& b, float t) {
        return lerpSample(a, b, t);
    }

    static inline float tj(float ti, const CaptureSample& pi, const CaptureSample& pj, float alpha) {
        float dx = pj.x - pi.x;
        float dy = pj.y - pi.y;
        float d = std::sqrt(dx * dx + dy * dy);
        return ti + std::pow(std::max(0.0001f, d), alpha);
    }

    inline std::vector<CaptureSample> catmullRomResample(const std::vector<CaptureSample>& in) const {
        if (in.size() < 2) return in;

        std::vector<CaptureSample> out;
        out.reserve(in.size() * (size_t)std::max(1, settings.catmullSubdiv));

        auto get = [&](int idx) -> CaptureSample {
            if (idx < 0) return in.front();
            if (idx >= (int)in.size()) return in.back();
            return in[(size_t)idx];
            };

        const int subdiv = std::max(1, settings.catmullSubdiv);

        for (int i = 0; i < (int)in.size() - 1; ++i) {
            CaptureSample p0 = get(i - 1);
            CaptureSample p1 = get(i);
            CaptureSample p2 = get(i + 1);
            CaptureSample p3 = get(i + 2);

            float t0 = 0.0f;
            float t1 = tj(t0, p0, p1, settings.catmullAlpha);
            float t2 = tj(t1, p1, p2, settings.catmullAlpha);
            float t3 = tj(t2, p2, p3, settings.catmullAlpha);

            for (int s = 0; s < subdiv; ++s) {
                float u = (subdiv <= 1) ? 0.0f : (float)s / (float)subdiv;
                float tt = t1 + (t2 - t1) * u;

                float d10 = std::max(0.000001f, t1 - t0);
                float d21 = std::max(0.000001f, t2 - t1);
                float d32 = std::max(0.000001f, t3 - t2);
                float d20 = std::max(0.000001f, t2 - t0);
                float d31 = std::max(0.000001f, t3 - t1);

                auto A1 = lerp(p0, p1, flow_clampf((tt - t0) / d10, 0.0f, 1.0f));
                auto A2 = lerp(p1, p2, flow_clampf((tt - t1) / d21, 0.0f, 1.0f));
                auto A3 = lerp(p2, p3, flow_clampf((tt - t2) / d32, 0.0f, 1.0f));
                auto B1 = lerp(A1, A2, flow_clampf((tt - t0) / d20, 0.0f, 1.0f));
                auto B2 = lerp(A2, A3, flow_clampf((tt - t1) / d31, 0.0f, 1.0f));
                auto C = lerp(B1, B2, flow_clampf((tt - t1) / d21, 0.0f, 1.0f));

                out.push_back(C);
            }
        }

        out.push_back(in.back());

        if (settings.resampleSpacingPx <= 0.5f || out.size() < 2) return out;

        std::vector<CaptureSample> spaced;
        spaced.reserve(out.size());
        spaced.push_back(out[0]);
        float accum = 0.0f;

        for (size_t i = 1; i < out.size(); ++i) {
            CaptureSample a = out[i - 1];
            const CaptureSample b = out[i];
            float d = flow_dist(a.x, a.y, b.x, b.y);
            if (d < 0.00001f) continue;

            while (accum + d >= settings.resampleSpacingPx) {
                float need = settings.resampleSpacingPx - accum;
                float u = need / d;
                CaptureSample p = lerpSample(a, b, u);
                spaced.push_back(p);
                a = p;
                d = flow_dist(a.x, a.y, b.x, b.y);
                accum = 0.0f;
                if (d < 0.00001f) break;
            }
            accum += d;
        }

        if (spaced.size() < 2) return out;
        return spaced;
    }



    inline void pushFlowLinkSample(float posX, float posY, float rotationDeg, float nowSeconds) {
        FlowLinkSample s;
        s.posX = posX;
        s.posY = posY;
        s.rotation = rotationDeg;
        s.t = nowSeconds - flowLinkStartTime;
        const int maxSamples = std::max(16, settings.maxFlowLinkSamples);
        if ((int)flowLinkSamples.size() >= maxSamples) {
            flowLinkSamples.back() = s;
            return;
        }
        flowLinkSamples.push_back(s);
    }

    inline void compactFlowLinkSamples() {
        normalizeSampleTimes(flowLinkSamples);
        if (flowLinkSamples.size() <= 2) return;

        std::vector<FlowLinkSample> compact;
        compact.reserve(flowLinkSamples.size());
        compact.push_back(flowLinkSamples.front());
        for (size_t i = 1; i + 1 < flowLinkSamples.size(); ++i) {
            const auto& prev = compact.back();
            const auto& cur = flowLinkSamples[i];
            const float move2 = flow_dist2(prev.posX, prev.posY, cur.posX, cur.posY);
            const float rotDelta = std::fabs(cur.rotation - prev.rotation);
            const float dt = cur.t - prev.t;
            if (move2 < (settings.minMovePx * settings.minMovePx) &&
                rotDelta < settings.minFlowLinkRotationDeltaDeg &&
                dt < settings.minSampleIntervalSec) {
                continue;
            }
            compact.push_back(cur);
        }
        compact.push_back(flowLinkSamples.back());
        flowLinkSamples.swap(compact);
    }

    static inline void normalizeSampleTimes(std::vector<CaptureSample>& in) {
        if (in.empty()) return;
        const float start = in.front().t;
        float prev = 0.0f;
        for (auto& sample : in) {
            sample.t = std::max(0.0f, sample.t - start);
            if (sample.t < prev) sample.t = prev;
            prev = sample.t;
        }
    }

    static inline void normalizeSampleTimes(std::vector<FlowLinkSample>& in) {
        if (in.empty()) return;
        const float start = in.front().t;
        float prev = 0.0f;
        for (auto& sample : in) {
            sample.t = std::max(0.0f, sample.t - start);
            if (sample.t < prev) sample.t = prev;
            prev = sample.t;
        }
    }

    static inline FlowLinkSample lerpFlowLinkSample(const FlowLinkSample& a, const FlowLinkSample& b, float t) {
        FlowLinkSample o;
        o.t = a.t + (b.t - a.t) * t;
        o.posX = a.posX + (b.posX - a.posX) * t;
        o.posY = a.posY + (b.posY - a.posY) * t;
        o.rotation = a.rotation + (b.rotation - a.rotation) * t;
        return o;
    }

    static inline float tjFlowLink(float ti, const FlowLinkSample& pi, const FlowLinkSample& pj, float alpha) {
        float dx = pj.posX - pi.posX;
        float dy = pj.posY - pi.posY;
        float d = std::sqrt(dx * dx + dy * dy);
        return ti + std::pow(std::max(0.0001f, d), alpha);
    }

    inline std::vector<FlowLinkSample> catmullRomResampleFlowLink(const std::vector<FlowLinkSample>& in) const {
        if (in.size() < 2) return in;

        std::vector<FlowLinkSample> out;
        out.reserve(in.size() * (size_t)std::max(1, settings.catmullSubdiv));

        auto get = [&](int idx) -> FlowLinkSample {
            if (idx < 0) return in.front();
            if (idx >= (int)in.size()) return in.back();
            return in[(size_t)idx];
        };

        const int subdiv = std::max(1, settings.catmullSubdiv);
        for (int i = 0; i < (int)in.size() - 1; ++i) {
            FlowLinkSample p0 = get(i - 1);
            FlowLinkSample p1 = get(i);
            FlowLinkSample p2 = get(i + 1);
            FlowLinkSample p3 = get(i + 2);

            float t0 = 0.0f;
            float t1 = tjFlowLink(t0, p0, p1, settings.catmullAlpha);
            float t2 = tjFlowLink(t1, p1, p2, settings.catmullAlpha);
            float t3 = tjFlowLink(t2, p2, p3, settings.catmullAlpha);

            for (int s = 0; s < subdiv; ++s) {
                float u = (subdiv <= 1) ? 0.0f : (float)s / (float)subdiv;
                float tt = t1 + (t2 - t1) * u;

                float d10 = std::max(0.000001f, t1 - t0);
                float d21 = std::max(0.000001f, t2 - t1);
                float d32 = std::max(0.000001f, t3 - t2);
                float d20 = std::max(0.000001f, t2 - t0);
                float d31 = std::max(0.000001f, t3 - t1);

                auto A1 = lerpFlowLinkSample(p0, p1, flow_clampf((tt - t0) / d10, 0.0f, 1.0f));
                auto A2 = lerpFlowLinkSample(p1, p2, flow_clampf((tt - t1) / d21, 0.0f, 1.0f));
                auto A3 = lerpFlowLinkSample(p2, p3, flow_clampf((tt - t2) / d32, 0.0f, 1.0f));
                auto B1 = lerpFlowLinkSample(A1, A2, flow_clampf((tt - t0) / d20, 0.0f, 1.0f));
                auto B2 = lerpFlowLinkSample(A2, A3, flow_clampf((tt - t1) / d31, 0.0f, 1.0f));
                auto C = lerpFlowLinkSample(B1, B2, flow_clampf((tt - t1) / d21, 0.0f, 1.0f));
                out.push_back(C);
            }
        }
        out.push_back(in.back());
        return out;
    }

private:
    float startTime = 0.0f;
    float flowLinkStartTime = 0.0f;
    std::vector<CaptureSample> samples;
    bool flowLinkCapturing = false;
    std::vector<FlowLinkSample> flowLinkSamples;
};
