    static void drawDiagnosticsOverlay(App& app, SDL_Renderer* r, TTF_Font* font)
    {
        const auto& diag = app.runtimeStateRef().diagnostics;
        if (!diag.overlayEnabled || !font) return;

        SDL_Rect anchor = app.getUILayout().canvas;
        if (anchor.w <= 0 || anchor.h <= 0)
            anchor = SDL_Rect{ 12, 12, 360, 220 };

        SDL_Rect panel{ anchor.x + 12, anchor.y + 12, 300, 278 };
        fillRoundRect(r, panel, 10, SDL_Color{ 10, 14, 20, 218 });
        strokeRoundRect(r, panel, 10, SDL_Color{ 86, 108, 138, 220 });
        drawText(r, font, "Diagnostics", panel.x + 12, panel.y + 10, COL_TEXT_MAIN);

        char buf[160];
        int y = panel.y + 38;
        auto drawRow = [&](const char* label, const std::string& value)
        {
            drawText(r, font, std::string(label) + " " + value, panel.x + 12, y, COL_TEXT_DIM);
            y += 18;
        };

        std::snprintf(buf, sizeof(buf), "%.2f ms", diag.frameMs);
        drawRow("Frame:", buf);
        std::snprintf(buf, sizeof(buf), "%.2f ms", diag.inputMs);
        drawRow("Input:", buf);
        std::snprintf(buf, sizeof(buf), "%.2f ms", diag.playbackMs);
        drawRow("Playback:", buf);
        std::snprintf(buf, sizeof(buf), "%.2f ms", diag.renderMs);
        drawRow("Render:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.compositeRebuildCount);
        drawRow("Composite rebuilds:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.layerRebuildCount);
        drawRow("Layer rebuilds:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.dirtyRegionCount);
        drawRow("Dirty regions:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.flowSampleCount);
        drawRow("Flow samples:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.flowLinkSampleCount);
        drawRow("FlowLink samples:", buf);
        drawRow("Texture est.:", formatBytesShort(diag.textureBytesEstimate));
        drawRow("Undo est.:", formatBytesShort(diag.undoBytesEstimate));
        drawRow("Proxy active:", diag.proxyActive ? "yes" : "no");
        std::snprintf(buf, sizeof(buf), "%d%%", diag.proxyScalePercent);
        drawRow("Proxy scale:", buf);
        std::snprintf(buf, sizeof(buf), "%d", diag.proxyRebuildCount);
        drawRow("Proxy rebuilds:", buf);
    }

