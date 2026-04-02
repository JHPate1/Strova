/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Layer.h
   Module:      Core
   Purpose:     Layer model used by drawing and compositing code.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Stroke.h"

struct LayerTransform
{
    float posX = 0.0f;
    float posY = 0.0f;
    float rotation = 0.0f;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
};

enum class LayerType : int
{
    Stroke = 0,
    Image = 1
};

struct LayerImageBitmap
{
    int width = 0;
    int height = 0;
    std::string sourcePath;
    std::vector<std::uint8_t> rgba;
};

struct Layer
{
    std::string name = "Layer";
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f;

    LayerType type = LayerType::Stroke;
    LayerImageBitmap bitmap;
    LayerTransform transform;

    std::vector<Stroke> strokes;
};
