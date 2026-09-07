#pragma once

#include "core/Settings.h"

#include <QString>

namespace screenfx::linuxfx {

enum class ShaderTarget { Hyprland, KWin };

// Full-output compositor fragment shader. Effect values are validated and baked
// into the source; the host supplies the screen texture, physical size and time.
QString BuildShader(const core::EffectSettings& effects, ShaderTarget target);

} // namespace screenfx::linuxfx
