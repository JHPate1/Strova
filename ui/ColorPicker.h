/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ColorPicker.h
   Module:      Ui
   Purpose:     Color picker state and UI helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include "SDL.h"
#include "SDL_ttf.h"
#include <string>
#include <vector>
#include "../core/Gradient.h"

class DrawingEngine;

class ColorPicker {
public:
    enum class GradientMode { Solid, Linear, Radial };
    enum class EditField { None, Hex, R, G, B, RGB };
    enum class ValueMode { Hex, RGB };

    
    static constexpr int kGradientStopCount = 4;

    
    float H = 0.0f, S = 1.0f, V = 1.0f;
    float opacity = 1.0f;
    SDL_Color rgba{ 255,255,255,255 };

    
    GradientMode gradientMode = GradientMode::Solid;
    bool gradientEnabled = false;
    float gradientStopPos[kGradientStopCount] = { 0.0f, 0.33f, 0.66f, 1.0f };
    SDL_Color gradientStopColor[kGradientStopCount] = {
        {255,0,0,255}, {0,255,0,255}, {0,0,255,255}, {255,255,255,255}
    };
    int selectedGradientStop = 0;

    
    std::string hexInput = "FFFFFF";
    std::string rInput = "255", gInput = "255", bInput = "255";
    std::string rgbInput = "255,255,255";
    EditField activeField = EditField::None;
    EditField editingField = EditField::None;
    bool replaceFieldOnType = false;
    ValueMode valueMode = ValueMode::Hex;

    
    SDL_Rect panelRect{};
    SDL_Rect purpleBarRect{};
    SDL_Rect topRowRect{};
    SDL_Rect swatchRect{};
    SDL_Rect opacityLabelRect{};
    SDL_Rect opacityPctRect{};
    SDL_Rect opacityTrackRect{};
    SDL_Rect opacityKnobRect{};
    SDL_Rect wheelBlockRect{};
    SDL_Rect wheelRect{};
    SDL_Rect svRect{};
    SDL_Rect alphaRect{};
    SDL_Rect rgbRowRect{};
    SDL_Rect rTextRect{}, gTextRect{}, bTextRect{};
    SDL_Rect hexRowRect{};
    SDL_Rect colorInputRect{};
    SDL_Rect hexTextRect{};
    SDL_Rect hexModeRect{};
    SDL_Rect modeDropdownRect{};
    SDL_Rect hueBarRect{};

    
    SDL_Rect gradientRowRect{};
    SDL_Rect gradientToggleRect{};
    SDL_Rect gradientSolidRect{};
    SDL_Rect gradientLinearRect{};
    SDL_Rect gradientRadialRect{};
    SDL_Rect gradientPreviewRect{};
    SDL_Rect gradientStopsRect{};

    
    bool draggingSV = false;
    bool draggingHue = false;
    bool draggingAlpha = false;
    bool draggingOpacity = false;
    bool draggingGradientStop = false;

    TTF_Font* font = nullptr;
    DrawingEngine* engine = nullptr;

    
    static float clamp01(float x);
    static float clampf(float v, float a, float b);
    static int clampi(int v, int a, int b);
    static SDL_Color hsvToRgb(float h, float s, float v);
    static void rgbToHsv(SDL_Color c, float& h, float& s, float& v);

    
    void setEngine(DrawingEngine* e) { engine = e; }
    void setFont(TTF_Font* f) { font = f; }

    SDL_Color getColorRGBA() const { return rgba; }
    void setColor(SDL_Color c);
    void pushToEngine();
    GradientConfig buildGradientConfig() const;

    
    SDL_Color gradientSample(float t) const;
    void updateSelectedGradientStopColor();
    void syncGradientToEngine();
    void updateGradientStopFromMouse(int mx);

    
    void beginFieldEditing(EditField field);
    void stopFieldEditing(bool apply);
    bool isEditingText() const;
    void syncTextFieldsFromColor();

    
    bool applyHexInput();
    bool applyRgbInput(EditField field);
    bool applyRgbInputs();
    bool handleTextInput(const char* text);
    bool handleKeyDown(SDL_Keycode key);
    bool handleMouseDown(int mx, int my);
    bool handleMouseUp(int mx, int my);
    bool handleMouseMotion(int mx, int my);

    
    void layout(const SDL_Rect& panelArea);
    void draw(SDL_Renderer* r);

    
    void updateSVFromMouse(int mx, int my);
    void updateHueFromMouse(int mx, int my);
    void updateAlphaFromMouse(int mx, int my);
    void updateOpacityFromMouse(int mx, int my);

    
    SDL_Color sampleAtPosition(float x, float y) const;

private:
    void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    void drawChecker(SDL_Renderer* r, const SDL_Rect& rc, int cell);
    void drawText(SDL_Renderer* r, const char* text, int x, int y, SDL_Color c);
    void drawText(SDL_Renderer* r, const char* text);
    void drawText(SDL_Renderer* r, const char* text, int x);
    void drawText(SDL_Renderer* r, const char* text, int x, int y);

    void drawHueWheel(SDL_Renderer* r);
    void drawSVSquare(SDL_Renderer* r);
    void drawAlphaStrip(SDL_Renderer* r);
    void drawHueBar(SDL_Renderer* r);
    void drawGradientControls(SDL_Renderer* r);

    static bool pointIn(const SDL_Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }
};
