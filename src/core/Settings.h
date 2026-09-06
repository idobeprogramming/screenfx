#pragma once

#include <cstdint>

namespace screenfx::core {

enum class FramePacingMode : std::uint32_t {
    Uncapped = 0,
    VSync = 1,
};

struct EffectSettings {
    float globalIntensity = 1.0F;

    float brightness = 0.0F;
    float contrast = 1.0F;
    float saturation = 1.0F;
    float gamma = 1.0F;
    float grayscale = 0.0F;
    float sepia = 0.0F;

    float scanlineIntensity = 0.0F;
    float scanlineSpacing = 2.0F;
    float scanlineThickness = 0.55F;
    float phosphorIntensity = 0.0F;
    float pixelSize = 1.0F;
    float sharpen = 0.0F;
    float chromaticAberration = 0.0F;
    float bloomIntensity = 0.0F;
    float bloomThreshold = 0.75F;
    float bloomRadius = 2.0F;
    float vignetteIntensity = 0.0F;
    float vignetteWidth = 0.65F;
    float grainIntensity = 0.0F;
    float grainSize = 1.0F;

    float tintRed = 1.0F;
    float tintGreen = 1.0F;
    float tintBlue = 1.0F;
    float tintIntensity = 0.0F;
};

struct AppSettings {
    bool enabled = false;
    std::uint32_t monitorIndex = 0;
    FramePacingMode framePacing = FramePacingMode::Uncapped;
    EffectSettings effects{};
};

} // namespace screenfx::core
