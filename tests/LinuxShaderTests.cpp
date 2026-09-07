#include "linux/ShaderSource.h"
#include "core/EffectFields.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSurfaceFormat>
#include <QVector2D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using screenfx::core::EffectSettings;
using screenfx::linuxfx::BuildShader;
using screenfx::linuxfx::ShaderTarget;
constexpr int kWidth = 32;
constexpr int kHeight = 24;
using Pixels = std::vector<std::uint8_t>;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Pixels SourcePixels() {
    Pixels pixels(kWidth * kHeight * 4);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto offset = static_cast<std::size_t>((y * kWidth + x) * 4);
            pixels[offset] = static_cast<std::uint8_t>(20 + x * 6);
            pixels[offset + 1] = static_cast<std::uint8_t>(30 + y * 8);
            pixels[offset + 2] = static_cast<std::uint8_t>(40 + ((x + y) % 5) * 35);
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

float Channel(const Pixels& pixels, int x, int y, int channel) {
    x = std::clamp(x, 0, kWidth - 1);
    y = std::clamp(y, 0, kHeight - 1);
    return static_cast<float>(pixels[static_cast<std::size_t>((y * kWidth + x) * 4 + channel)]) / 255.0F;
}

void ExpectPixels(const Pixels& actual, const std::function<float(int, int, int)>& expected,
                  const std::string& name, int tolerance = 2) {
    int maximumError = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                const int reference = channel == 3 ? 255
                    : static_cast<int>(std::lround(std::clamp(expected(x, y, channel), 0.0F, 1.0F) * 255.0F));
                const int value = actual[static_cast<std::size_t>((y * kWidth + x) * 4 + channel)];
                maximumError = std::max(maximumError, std::abs(value - reference));
            }
        }
    }
    Require(maximumError <= tolerance, name + ": maximum channel error " + std::to_string(maximumError));
}

void TestSources() {
    const QLocale saved = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    for (const auto target : {ShaderTarget::Hyprland, ShaderTarget::KWin}) {
        const QString neutral = BuildShader({}, target);
        Require(neutral.startsWith(target == ShaderTarget::Hyprland ? "#version 300 es\n" : "#version 140\n"),
                "Wrong compositor shader language version");
        Require(!neutral.contains("uniform float time;"), "Static shader requests animated compositor uniform");
        EffectSettings effects;
        effects.grainIntensity = 0.5F;
        Require(BuildShader(effects, target).contains("uniform float time;"), "Grain has no animation time");
        effects.globalIntensity = 0.0F;
        Require(!BuildShader(effects, target).contains("uniform float time;"), "Bypass requests animation time");
        effects = {};
        effects.tintIntensity = 0.25F;
        Require(BuildShader(effects, target).contains("const float tintIntensity = 0.25;"),
                "Localized decimal formatting produced invalid GLSL");
        for (const auto& field : screenfx::core::kEffectFields) {
            EffectSettings invalid;
            EffectSettings expected;
            expected.*(field.member) = field.minimum;
            invalid.*(field.member) = std::numeric_limits<float>::quiet_NaN();
            Require(BuildShader(invalid, target) == BuildShader(expected, target),
                    std::string(field.name) + ": NaN was not sanitized");
            invalid.*(field.member) = std::numeric_limits<float>::infinity();
            Require(BuildShader(invalid, target) == BuildShader(expected, target),
                    std::string(field.name) + ": infinity was not sanitized");
            invalid.*(field.member) = field.minimum - 100.0F;
            Require(BuildShader(invalid, target) == BuildShader(expected, target),
                    std::string(field.name) + ": lower bound was not applied");
            expected.*(field.member) = field.maximum;
            invalid.*(field.member) = field.maximum + 100.0F;
            Require(BuildShader(invalid, target) == BuildShader(expected, target),
                    std::string(field.name) + ": upper bound was not applied");
        }
    }
    QLocale::setDefault(saved);
    std::cout << "PASS: compositor interfaces, all field bounds, nonfinite values, locale and animation uniforms\n";
}

class Renderer {
public:
    Renderer(QOpenGLContext& context, ShaderTarget target, const Pixels& source)
        : gl_(context.functions()), target_(target), framebuffer_(kWidth, kHeight), buffer_(QOpenGLBuffer::VertexBuffer) {
        Require(framebuffer_.isValid(), "Could not create OpenGL framebuffer");
        Require(vao_.create() && buffer_.create(), "Could not create OpenGL vertex resources");
        vao_.bind();
        buffer_.bind();
        constexpr std::array<float, 6> vertices{-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
        buffer_.allocate(vertices.data(), static_cast<int>(sizeof(vertices)));
        gl_->glGenTextures(1, &texture_);
        gl_->glBindTexture(GL_TEXTURE_2D, texture_);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.data());
        Require(gl_->glGetError() == GL_NO_ERROR, "OpenGL source texture initialization failed");
    }

    ~Renderer() { gl_->glDeleteTextures(1, &texture_); }

    Pixels Draw(const EffectSettings& effects, float time = 0.0F) {
        const QByteArray vertex = target_ == ShaderTarget::Hyprland
            ? QByteArray("#version 300 es\nprecision highp float;\nin vec2 position;\nout vec2 v_texcoord;\n"
                         "void main(){v_texcoord=(position+1.0)*0.5;gl_Position=vec4(position,0.0,1.0);}\n")
            : QByteArray("#version 140\nin vec2 position;\nout vec2 texcoord0;\n"
                         "void main(){texcoord0=(position+1.0)*0.5;gl_Position=vec4(position,0.0,1.0);}\n");
        QOpenGLShaderProgram program;
        Require(program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex),
                "Vertex compilation failed: " + program.log().toStdString());
        Require(program.addShaderFromSourceCode(QOpenGLShader::Fragment, BuildShader(effects, target_)),
                "Fragment compilation failed: " + program.log().toStdString());
        Require(program.link(), "Shader linking failed: " + program.log().toStdString());
        Require(program.bind() && framebuffer_.bind(), "Could not bind shader/framebuffer");
        vao_.bind();
        buffer_.bind();
        const int position = program.attributeLocation("position");
        Require(position >= 0, "Missing vertex position attribute");
        program.enableAttributeArray(position);
        program.setAttributeBuffer(position, GL_FLOAT, 0, 2);
        const char* sampler = target_ == ShaderTarget::Hyprland ? "tex" : "sampler";
        const char* size = target_ == ShaderTarget::Hyprland ? "screen_size" : "screenSize";
        if (program.uniformLocation(sampler) >= 0) program.setUniformValue(sampler, 0);
        if (program.uniformLocation(size) >= 0) program.setUniformValue(size, QVector2D(kWidth, kHeight));
        if (program.uniformLocation("time") >= 0) program.setUniformValue("time", time);
        gl_->glActiveTexture(GL_TEXTURE0);
        gl_->glBindTexture(GL_TEXTURE_2D, texture_);
        gl_->glViewport(0, 0, kWidth, kHeight);
        gl_->glDisable(GL_BLEND);
        gl_->glDisable(GL_DEPTH_TEST);
        gl_->glDisable(GL_SCISSOR_TEST);
        gl_->glDisable(GL_DITHER);
        gl_->glDrawArrays(GL_TRIANGLES, 0, 3);
        Pixels result(kWidth * kHeight * 4);
        gl_->glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        Require(gl_->glGetError() == GL_NO_ERROR, "OpenGL draw/readback failed");
        program.disableAttributeArray(position);
        framebuffer_.release();
        return result;
    }

private:
    QOpenGLFunctions* gl_;
    ShaderTarget target_;
    QOpenGLFramebufferObject framebuffer_;
    QOpenGLBuffer buffer_;
    QOpenGLVertexArrayObject vao_;
    GLuint texture_ = 0;
};

void TestPixels(Renderer& renderer, const Pixels& source) {
    auto original = [&](int x, int y, int channel) { return Channel(source, x, y, channel); };
    ExpectPixels(renderer.Draw({}), original, "Neutral identity", 1);
    EffectSettings effects;
    for (const auto& field : screenfx::core::kEffectFields) effects.*(field.member) = field.maximum;
    effects.globalIntensity = 0.0F;
    ExpectPixels(renderer.Draw(effects, 1.0F), original, "Global bypass", 1);

    effects = {};
    effects.tintRed = 0.0F;
    effects.tintBlue = 0.0F;
    effects.tintIntensity = 1.0F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return channel == 1 ? original(x, y, channel) : 0.0F;
    }, "Tint picker channels");
    effects.globalIntensity = 0.35F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return original(x, y, channel) * (channel == 1 ? 1.0F : 0.65F);
    }, "Partial global blend");

    effects = {};
    effects.gamma = 0.5F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        const float value = original(x, y, channel);
        return value * value;
    }, "Gamma");
    effects = {};
    effects.grayscale = 1.0F;
    auto luminance = [&](int x, int y, int) {
        return original(x, y, 0) * 0.2126F + original(x, y, 1) * 0.7152F + original(x, y, 2) * 0.0722F;
    };
    ExpectPixels(renderer.Draw(effects), luminance, "Grayscale");
    effects = {};
    effects.saturation = 0.0F;
    ExpectPixels(renderer.Draw(effects), luminance, "Saturation");
    effects = {};
    effects.sepia = 1.0F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        constexpr float matrix[3][3]{{0.393F, 0.769F, 0.189F}, {0.349F, 0.686F, 0.168F}, {0.272F, 0.534F, 0.131F}};
        return original(x, y, 0) * matrix[channel][0] + original(x, y, 1) * matrix[channel][1]
            + original(x, y, 2) * matrix[channel][2];
    }, "Sepia");
    effects = {};
    effects.brightness = -0.1F;
    effects.contrast = 1.4F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return (original(x, y, channel) - 0.6F) * 1.4F + 0.5F;
    }, "Brightness/contrast");

    effects = {};
    effects.scanlineIntensity = 0.6F;
    effects.phosphorIntensity = 0.8F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return original(x, y, channel) * (y % 2 == 0 ? 0.67F : 1.0F)
            * (x % 3 == channel ? 1.0F : 0.856F);
    }, "Scanline rows and phosphor columns");
    effects = {};
    effects.pixelSize = 2.0F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        x = (x / 2) * 2;
        y = (y / 2) * 2;
        return (original(x, y, channel) + original(x + 1, y, channel)
            + original(x, y + 1, channel) + original(x + 1, y + 1, channel)) * 0.25F;
    }, "Pixelation blocks");
    effects = {};
    effects.chromaticAberration = 2.0F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return original(x + (channel == 0 ? 2 : (channel == 2 ? -2 : 0)), y, channel);
    }, "Chromatic offsets and edge clamp");
    effects = {};
    effects.sharpen = 1.0F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        return original(x, y, channel) * 2.0F - 0.25F * (original(x - 1, y, channel)
            + original(x + 1, y, channel) + original(x, y - 1, channel) + original(x, y + 1, channel));
    }, "Sharpen neighbours");
    effects = {};
    effects.bloomIntensity = 1.0F;
    effects.bloomThreshold = 1.0F;
    ExpectPixels(renderer.Draw(effects), original, "Bloom threshold one", 1);
    effects.bloomRadius = 1.0F;
    effects.bloomThreshold = 0.25F;
    effects.bloomIntensity = 0.5F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        const float bright = std::max({original(x, y, 0), original(x, y, 1), original(x, y, 2)});
        const float level = std::clamp((bright - 0.25F) / 0.75F, 0.0F, 1.0F);
        const float mask = level * level * (3.0F - 2.0F * level);
        const float neighbours = original(x - 1, y, channel) + original(x + 1, y, channel)
            + original(x, y - 1, channel) + original(x, y + 1, channel);
        return original(x, y, channel) + neighbours * 0.125F * mask;
    }, "Bloom neighbourhood and threshold");
    effects = {};
    effects.vignetteIntensity = 0.8F;
    effects.vignetteWidth = 0.4F;
    ExpectPixels(renderer.Draw(effects), [&](int x, int y, int channel) {
        const float dx = (static_cast<float>(x) + 0.5F) * 2.0F / kWidth - 1.0F;
        const float dy = (static_cast<float>(y) + 0.5F) * 2.0F / kHeight - 1.0F;
        const float level = std::clamp((std::sqrt(dx * dx + dy * dy) - 0.4F) / 1.014F, 0.0F, 1.0F);
        return original(x, y, channel) * (1.0F - level * level * (3.0F - 2.0F * level) * 0.8F);
    }, "Vignette");

    effects = {};
    effects.grainIntensity = 0.5F;
    const auto grainA = renderer.Draw(effects, 0.0F);
    const auto grainB = renderer.Draw(effects, 0.37F);
    Require(grainA != grainB && grainA != source, "Grain does not vary with compositor time");

    effects = {};
    for (const auto& field : screenfx::core::kEffectFields) effects.*(field.member) = field.maximum;
    const auto maximum = renderer.Draw(effects, 0.2F);
    Require(maximum.size() == source.size(), "All-effects draw did not produce a full frame");
    effects.brightness = -1.0F;
    effects.gamma = 1.0F;
    renderer.Draw(effects, 0.2F); // Negative pre-gamma channels must still compile and render safely.
}

void TestTarget(ShaderTarget target) {
    QSurfaceFormat format;
    format.setRenderableType(target == ShaderTarget::Hyprland ? QSurfaceFormat::OpenGLES : QSurfaceFormat::OpenGL);
    format.setVersion(3, target == ShaderTarget::Hyprland ? 0 : 1);
    format.setRedBufferSize(8);
    format.setGreenBufferSize(8);
    format.setBlueBufferSize(8);
    format.setAlphaBufferSize(8);
    QOpenGLContext context;
    context.setFormat(format);
    Require(context.create(), "Could not create requested OpenGL context; run under Xvfb with Mesa EGL/OpenGL drivers");
    Require(context.isOpenGLES() == (target == ShaderTarget::Hyprland), "Requested OpenGL API was not created");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    Require(surface.isValid() && context.makeCurrent(&surface), "Could not make offscreen context current");
    context.functions()->initializeOpenGLFunctions();
    {
        const auto source = SourcePixels();
        Renderer renderer(context, target, source);
        TestPixels(renderer, source);
    }
    context.doneCurrent();
    std::cout << "PASS: " << (target == ShaderTarget::Hyprland ? "Hyprland GLES 3.0" : "KWin GLSL 1.40")
              << " compilation, identity, bypass, color, tint, CRT, spatial filters and animated grain pixels\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const bool sourceOnly = argc == 2 && std::string_view(argv[1]) == "--source-only";
        if (sourceOnly) {
            QCoreApplication app(argc, argv);
            TestSources();
        } else {
            QGuiApplication app(argc, argv);
            TestSources();
            TestTarget(ShaderTarget::Hyprland);
            TestTarget(ShaderTarget::KWin);
        }
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
