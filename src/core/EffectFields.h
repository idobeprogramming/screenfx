#pragma once
#include "Settings.h"
#include <array>

namespace screenfx::core {
struct EffectField {
    const char* name;
    float EffectSettings::* member;
    float minimum;
    float maximum;
    const char* label;
};
inline constexpr std::array kEffectFields{
    EffectField{"globalIntensity", &EffectSettings::globalIntensity, 0, 1, "Overall strength"},
    EffectField{"brightness", &EffectSettings::brightness, -1, 1, "Brightness"},
    EffectField{"contrast", &EffectSettings::contrast, 0, 4, "Contrast"},
    EffectField{"saturation", &EffectSettings::saturation, 0, 4, "Saturation"},
    EffectField{"gamma", &EffectSettings::gamma, 0.1F, 4, "Gamma"},
    EffectField{"grayscale", &EffectSettings::grayscale, 0, 1, "Grayscale"},
    EffectField{"sepia", &EffectSettings::sepia, 0, 1, "Sepia"},
    EffectField{"tintRed", &EffectSettings::tintRed, 0, 1, "Tint red"},
    EffectField{"tintGreen", &EffectSettings::tintGreen, 0, 1, "Tint green"},
    EffectField{"tintBlue", &EffectSettings::tintBlue, 0, 1, "Tint blue"},
    EffectField{"tintIntensity", &EffectSettings::tintIntensity, 0, 1, "Tint strength"},
    EffectField{"scanlineIntensity", &EffectSettings::scanlineIntensity, 0, 1, "Scanlines"},
    EffectField{"scanlineSpacing", &EffectSettings::scanlineSpacing, 1, 8, "Scanline spacing"},
    EffectField{"scanlineThickness", &EffectSettings::scanlineThickness, 0.05F, 1, "Scanline thickness"},
    EffectField{"phosphorIntensity", &EffectSettings::phosphorIntensity, 0, 1, "RGB phosphor mask"},
    EffectField{"pixelSize", &EffectSettings::pixelSize, 1, 32, "Pixel size"},
    EffectField{"sharpen", &EffectSettings::sharpen, 0, 2, "Sharpen"},
    EffectField{"chromaticAberration", &EffectSettings::chromaticAberration, 0, 8, "Chromatic aberration"},
    EffectField{"bloomIntensity", &EffectSettings::bloomIntensity, 0, 2, "Bloom"},
    EffectField{"bloomThreshold", &EffectSettings::bloomThreshold, 0, 1, "Bloom threshold"},
    EffectField{"bloomRadius", &EffectSettings::bloomRadius, 0.5F, 8, "Bloom radius"},
    EffectField{"vignetteIntensity", &EffectSettings::vignetteIntensity, 0, 1, "Vignette"},
    EffectField{"vignetteWidth", &EffectSettings::vignetteWidth, 0.1F, 1, "Vignette width"},
    EffectField{"grainIntensity", &EffectSettings::grainIntensity, 0, 1, "Grain"},
    EffectField{"grainSize", &EffectSettings::grainSize, 0.25F, 8, "Grain size"},
};
}
