// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#pragma once

#include "util/Weather.h"

class GfxRenderer;

// The weather layout shared by the Weather screen and the power-off screen:
// city, icon and temperature, then the detail rows and the Open-Meteo credit.
namespace WeatherFace {

// Draws into the frame buffer (no refresh) within [top, bottom).
void draw(const GfxRenderer& renderer, const weather::Data& data, int left, int width, int top, int bottom);

}  // namespace WeatherFace
