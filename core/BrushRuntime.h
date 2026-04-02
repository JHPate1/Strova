#pragma once

#include "BrushSystem.h"
#include "Stroke.h"
#include <algorithm>
#include <cmath>

namespace strova::brush
{
    struct CompiledBrushRuntime
    {
        const BrushPackage* pkg = nullptr;
        ScriptEffect fx{};
        BrushParams params{};
        BrushColorOptions color{};
        float sizeScale = 1.0f;
        float diameterPx = 1.0f;
        float spacingPx = 1.0f;
        float scatterPx = 0.0f;
        float flow = 1.0f;
        float opacity = 1.0f;
        bool userGradient = false;
        bool packageGradient = false;
        float totalLen = 0.0f;
        SDL_Color baseColor{ 0,0,0,255 };
        std::uint64_t stampRevision = 0;
        int stampWidth = 0;
        int stampHeight = 0;

        bool valid() const
        {
            return pkg && stampWidth > 0 && stampHeight > 0;
        }
    };

    inline float runtimeClamp(float v, float a, float b)
    {
        return (v < a) ? a : (v > b ? b : v);
    }

    inline float runtimeHash(float v)
    {
        float s = std::sin(v * 12.9898f) * 43758.5453f;
        return s - std::floor(s);
    }

    inline SDL_Color runtimeLerp(SDL_Color a, SDL_Color b, float t)
    {
        t = runtimeClamp(t, 0.0f, 1.0f);
        SDL_Color o{};
        o.r = (Uint8)std::lround((float)a.r * (1.0f - t) + (float)b.r * t);
        o.g = (Uint8)std::lround((float)a.g * (1.0f - t) + (float)b.g * t);
        o.b = (Uint8)std::lround((float)a.b * (1.0f - t) + (float)b.b * t);
        o.a = (Uint8)std::lround((float)a.a * (1.0f - t) + (float)b.a * t);
        return o;
    }

    inline SDL_Color runtimeSampleStrokeGradient(const GradientConfig& g, float t)
    {
        t = runtimeClamp(t, 0.0f, 1.0f);
        int hi = 1;
        while (hi < STROVA_MAX_GRADIENT_STOPS && g.stopPos[hi] < t) ++hi;
        if (hi >= STROVA_MAX_GRADIENT_STOPS)
            return g.stopColor[STROVA_MAX_GRADIENT_STOPS - 1];
        int lo = hi - 1;
        float a = g.stopPos[lo];
        float b = g.stopPos[hi];
        float f = (b > a) ? (t - a) / (b - a) : 0.0f;
        return runtimeLerp(g.stopColor[lo], g.stopColor[hi], f);
    }

    inline CompiledBrushRuntime compileBrushRuntime(const BrushPackage* pkg, const Stroke& s, float scale, float totalLen)
    {
        CompiledBrushRuntime rt{};
        if (!pkg) return rt;

        rt.pkg = pkg;
        rt.fx = parseScriptEffect(pkg->scriptSource);
        rt.params = pkg->manifest.params;
        rt.color = pkg->manifest.color;
        rt.sizeScale = std::max(0.05f, rt.fx.sizeScale);
        rt.diameterPx = std::max(1.0f, s.thickness * scale * rt.sizeScale);

        const float spacingNorm = (s.settings.spacing > 0.0f) ? s.settings.spacing : rt.params.spacing;
        rt.spacingPx = std::max(0.35f, rt.diameterPx * std::max(0.01f, spacingNorm * std::max(0.1f, rt.fx.spacingScale)));

        const float scatterNorm = std::max(0.0f, rt.params.scatter + s.settings.scatter + rt.fx.scatterBoost);
        rt.scatterPx = scatterNorm * rt.diameterPx * 0.55f;

        rt.flow = std::max(0.01f, ((s.settings.flow > 0.0f) ? s.settings.flow : rt.params.flow));
        rt.opacity = std::clamp(((s.settings.opacity > 0.0f ? s.settings.opacity : rt.params.opacity) * rt.flow * rt.fx.alphaScale), 0.01f, 4.0f);
        rt.userGradient = (s.gradient.enabled && s.gradient.mode != 0 && pkg->manifest.color.supportsGradient && s.settings.brushSupportsGradient && totalLen > 0.0001f);
        rt.packageGradient = (pkg->manifest.color.supportsGradient && s.settings.brushSupportsGradient);
        rt.totalLen = totalLen;
        rt.baseColor = s.color;
        rt.stampRevision = pkg->stamp.revision;
        rt.stampWidth = pkg->stamp.width;
        rt.stampHeight = pkg->stamp.height;
        return rt;
    }

    inline float compiledSizePx(const CompiledBrushRuntime& rt, const Stroke& s, float pressure, float seed)
    {
        float sizePx = rt.diameterPx;
        sizePx *= (1.0f + (runtimeHash(seed * 0.67f) * 2.0f - 1.0f) * std::max(rt.params.jitterSize, s.settings.jitterSize) * 0.35f);
        sizePx *= std::max(0.1f, 1.0f + (pressure - 1.0f) * std::max(rt.params.pressureSize, 0.0f));
        return std::max(1.0f, sizePx);
    }

    inline float compiledAngleDeg(const CompiledBrushRuntime& rt, const Stroke& s, float seed, float dirAngle)
    {
        float angleDeg = 0.0f;
        switch (rt.params.rotationMode)
        {
        case RotationMode::Fixed: angleDeg = rt.params.fixedAngle; break;
        case RotationMode::Random: angleDeg = runtimeHash(seed * 0.5f) * 360.0f; break;
        case RotationMode::Stroke:
        default: angleDeg = dirAngle; break;
        }
        angleDeg += rt.fx.rotationBiasDeg + (runtimeHash(seed * 0.39f) * 2.0f - 1.0f) * std::max(rt.params.jitterRotation, s.settings.jitterRotation) * 45.0f;
        return angleDeg;
    }

    inline SDL_Color compiledColorAt(const CompiledBrushRuntime& rt, const Stroke& s, float strokeU, float pressure, float seed)
    {
        SDL_Color c = rt.baseColor;
        if (!rt.color.supportsUserColor || !s.settings.brushSupportsUserColor)
            c = rt.color.fixedColor;

        if (rt.userGradient)
            c = runtimeSampleStrokeGradient(s.gradient, strokeU);
        else if (rt.packageGradient && rt.color.gradientMode == GradientMode::StrokeProgress)
            c = sampleGradient(rt.color, strokeU);
        else if (rt.packageGradient && rt.color.gradientMode == GradientMode::Pressure)
            c = sampleGradient(rt.color, std::clamp(pressure, 0.0f, 1.0f));
        else if (rt.packageGradient && rt.color.gradientMode == GradientMode::Random)
            c = sampleGradient(rt.color, runtimeHash(seed * 0.173f + strokeU * 8.1f));
        else if (!rt.color.supportsUserColor || !s.settings.brushSupportsUserColor)
            c = rt.color.fixedColor;

        c.a = (Uint8)std::clamp((int)std::lround((float)c.a * rt.opacity), 0, 255);
        if (rt.params.blendMode == BlendMode::Erase)
            c = SDL_Color{ 255, 255, 255, (Uint8)std::clamp((int)std::lround((float)c.a * 0.50f), 0, 255) };
        return c;
    }
}
