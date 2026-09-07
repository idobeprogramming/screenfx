#include "ShaderSource.h"

#include "core/EffectFields.h"

#include <algorithm>
#include <cmath>

namespace screenfx::linuxfx {
namespace {

QString FloatLiteral(float value) {
    // QString::number always uses the C locale, including on localized desktops.
    QString text = QString::number(value == 0.0F ? 0.0 : static_cast<double>(value), 'g', 9);
    if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e'))) {
        text += QStringLiteral(".0");
    }
    return text;
}

constexpr char kShaderBody[] = R"glsl(
const vec3 tintColor = vec3(tintRed, tintGreen, tintBlue);

vec3 SampleRgb(vec2 uv)
{
    return texture(sourceTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

float Hash21(vec2 value)
{
    value = fract(value * vec2(123.34, 456.21));
    value += dot(value, value + 45.32);
    return fract(value.x * value.y);
}

vec3 SampleChromatic(vec2 uv)
{
    if (chromaticAberration == 0.0)
        return SampleRgb(uv);
    float offset = chromaticAberration / max(screenSize.x, 1.0);
    return vec3(SampleRgb(uv + vec2(offset, 0.0)).r,
                SampleRgb(uv).g,
                SampleRgb(uv - vec2(offset, 0.0)).b);
}

vec3 ApplyColour(vec3 color)
{
    if (brightness != 0.0 || contrast != 1.0) {
        color += brightness;
        color = (color - 0.5) * contrast + 0.5;
    }
    if (saturation != 1.0 || grayscale != 0.0) {
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), color, saturation);
        color = mix(color, vec3(luminance), grayscale);
    }
    if (sepia != 0.0) {
        vec3 sepiaColor = vec3(
            dot(color, vec3(0.393, 0.769, 0.189)),
            dot(color, vec3(0.349, 0.686, 0.168)),
            dot(color, vec3(0.272, 0.534, 0.131)));
        color = mix(color, sepiaColor, sepia);
    }
    color = max(color, vec3(0.0));
    if (gammaValue != 1.0)
        color = pow(color, vec3(1.0 / gammaValue));
    return color;
}

vec3 ApplyCrt(vec2 uv, vec3 color)
{
    if (scanlineIntensity != 0.0 && scanlineThickness != 0.0) {
        float linePosition = uv.y * screenSize.y / max(scanlineSpacing, 1.0);
        float lineFactor = mix(1.0, 1.0 - scanlineThickness, step(fract(linePosition), 0.5));
        color *= mix(1.0, lineFactor, scanlineIntensity);
    }
    if (phosphorIntensity > 0.001) {
        float channel = mod(floor(uv.x * screenSize.x), 3.0);
        vec3 mask = channel < 1.0 ? vec3(1.0, 0.82, 0.82)
            : (channel < 2.0 ? vec3(0.82, 1.0, 0.82) : vec3(0.82, 0.82, 1.0));
        color *= mix(vec3(1.0), mask, phosphorIntensity);
    }
    return color;
}

vec3 ApplyBloom(vec2 uv, vec3 color)
{
    if (bloomIntensity > 0.001 && bloomThreshold < 1.0) {
        vec2 texel = bloomRadius / max(screenSize, vec2(1.0));
        vec3 blurred = SampleRgb(uv + vec2(texel.x, 0.0));
        blurred += SampleRgb(uv - vec2(texel.x, 0.0));
        blurred += SampleRgb(uv + vec2(0.0, texel.y));
        blurred += SampleRgb(uv - vec2(0.0, texel.y));
        blurred *= 0.25;
        float brightnessValue = max(color.r, max(color.g, color.b));
        float mask = smoothstep(bloomThreshold, 1.0, brightnessValue);
        color += blurred * mask * bloomIntensity;
    }
    return color;
}

vec3 ApplyVignette(vec2 uv, vec3 color)
{
    if (vignetteIntensity != 0.0) {
        float distanceFromCenter = length(uv * 2.0 - 1.0);
        float vignette = smoothstep(vignetteWidth, 1.414, distanceFromCenter);
        color *= 1.0 - vignette * vignetteIntensity;
    }
    return color;
}

void main()
{
    if (globalIntensity == 0.0) {
        fragColor = vec4(clamp(SampleRgb(screenUv), vec3(0.0), vec3(1.0)), 1.0);
        return;
    }
    vec2 originalUv = screenUv;
    vec2 uv = originalUv;
    if (pixelSize > 1.001) {
        vec2 pixels = max(screenSize / pixelSize, vec2(1.0));
        uv = (floor(uv * pixels) + 0.5) / pixels;
    }
    vec3 color = SampleChromatic(uv);
    if (sharpen > 0.001) {
        vec2 texel = 1.0 / max(screenSize, vec2(1.0));
        vec3 neighbours = SampleRgb(uv + vec2(texel.x, 0.0)) + SampleRgb(uv - vec2(texel.x, 0.0))
            + SampleRgb(uv + vec2(0.0, texel.y)) + SampleRgb(uv - vec2(0.0, texel.y));
        color = clamp(color * (1.0 + sharpen) - neighbours * (sharpen * 0.25), vec3(0.0), vec3(1.0));
    }
    color = ApplyColour(color);
    color = ApplyCrt(uv, color);
    color = ApplyBloom(uv, color);
    color = ApplyVignette(uv, color);
    if (grainIntensity > 0.001) {
        vec2 grainUv = floor(uv * screenSize / max(grainSize, 0.25));
        // Compositors supply elapsed seconds rather than a capture-frame counter.
        float grain = Hash21(grainUv + time * 17.0) - 0.5;
        color = clamp(color + grain * grainIntensity, vec3(0.0), vec3(1.0));
    }
    if (tintIntensity != 0.0)
        color *= mix(vec3(1.0), tintColor, tintIntensity);
    if (globalIntensity != 1.0)
        color = mix(SampleRgb(originalUv), color, globalIntensity);
    fragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
)glsl";

} // namespace

QString BuildShader(const core::EffectSettings& effects, ShaderTarget target) {
    core::EffectSettings validated = effects;
    for (const auto& field : core::kEffectFields) {
        const float value = validated.*(field.member);
        validated.*(field.member) = std::isfinite(value)
            ? std::clamp(value, field.minimum, field.maximum) : field.minimum;
    }

    QString shader;
    shader.reserve(10000);
    if (target == ShaderTarget::Hyprland) {
        // Matches Hyprland's current example/screenShader.frag and tex300 vertex shader.
        shader += QStringLiteral(
            "#version 300 es\nprecision highp float;\nprecision highp int;\n"
            "in vec2 v_texcoord;\nlayout(location = 0) out vec4 fragColor;\n"
            "uniform sampler2D tex;\nuniform vec2 screen_size;\n"
            "#define sourceTexture tex\n#define screenSize screen_size\n#define screenUv v_texcoord\n");
    } else {
        shader += QStringLiteral(
            "#version 140\nin vec2 texcoord0;\nout vec4 fragColor;\n"
            "uniform sampler2D sampler;\nuniform vec2 screenSize;\n"
            "#define sourceTexture sampler\n#define screenUv texcoord0\n");
    }
    // A live time uniform makes Hyprland require continuous full-output repaint.
    // Static presets and global bypass must not accidentally request animation.
    shader += validated.grainIntensity > 0.001F && validated.globalIntensity > 0.0F
        ? QStringLiteral("uniform float time;\n") : QStringLiteral("const float time = 0.0;\n");
    for (const auto& field : core::kEffectFields) {
        const QString name = field.member == &core::EffectSettings::gamma
            ? QStringLiteral("gammaValue") : QString::fromLatin1(field.name);
        shader += QStringLiteral("const float ") + name + QStringLiteral(" = ")
            + FloatLiteral(validated.*(field.member)) + QStringLiteral(";\n");
    }
    shader += QString::fromLatin1(kShaderBody);
    return shader;
}

} // namespace screenfx::linuxfx
