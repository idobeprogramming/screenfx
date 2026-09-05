cbuffer ScreenFxParams : register(b0)
{
    float2 screenSize;
    float time;
    float frame;

    float globalIntensity;
    float brightness;
    float contrast;
    float saturation;
    float gammaValue;
    float grayscale;
    float sepia;
    float scanlineIntensity;
    float scanlineSpacing;
    float scanlineThickness;
    float phosphorIntensity;
    float pixelSize;
    float sharpen;
    float chromaticAberration;
    float bloomIntensity;
    float bloomThreshold;
    float bloomRadius;
    float vignetteIntensity;
    float vignetteWidth;
    float grainIntensity;
    float grainSize;
    float3 padding;
};

Texture2D sourceTexture : register(t0);
SamplerState linearClamp : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = uvs[vertexId];
    return output;
}

float3 SampleRgb(float2 uv)
{
    return sourceTexture.Sample(linearClamp, saturate(uv)).rgb;
}

float Hash21(float2 value)
{
    value = frac(value * float2(123.34, 456.21));
    value += dot(value, value + 45.32);
    return frac(value.x * value.y);
}

float3 SampleChromatic(float2 uv)
{
    float2 offset = float2(chromaticAberration, chromaticAberration) / max(screenSize, float2(1.0, 1.0));
    float3 result;
    result.r = SampleRgb(uv + float2(offset.x, 0.0)).r;
    result.g = SampleRgb(uv).g;
    result.b = SampleRgb(uv - float2(offset.x, 0.0)).b;
    return result;
}

float3 ApplyBloom(float2 uv, float3 color)
{
    if (bloomIntensity <= 0.001)
    {
        return color;
    }
    float2 texel = bloomRadius / max(screenSize, float2(1.0, 1.0));
    float3 blurred = 0.0;
    blurred += SampleRgb(uv + float2(texel.x, 0.0));
    blurred += SampleRgb(uv - float2(texel.x, 0.0));
    blurred += SampleRgb(uv + float2(0.0, texel.y));
    blurred += SampleRgb(uv - float2(0.0, texel.y));
    blurred *= 0.25;
    float brightnessValue = max(color.r, max(color.g, color.b));
    float mask = smoothstep(bloomThreshold, 1.0, brightnessValue);
    return color + blurred * mask * bloomIntensity;
}

float3 ApplyColour(float3 color)
{
    color += brightness;
    color = (color - 0.5) * contrast + 0.5;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luminance.xxx, color, saturation);
    color = lerp(color, luminance.xxx, grayscale);
    float3 sepiaColor = float3(
        dot(color, float3(0.393, 0.769, 0.189)),
        dot(color, float3(0.349, 0.686, 0.168)),
        dot(color, float3(0.272, 0.534, 0.131)));
    color = lerp(color, sepiaColor, sepia);
    return pow(max(color, 0.0), 1.0 / gammaValue);
}

float3 ApplyCrt(float2 uv, float3 color)
{
    float linePosition = uv.y * screenSize.y / max(scanlineSpacing, 1.0);
    float lineFactor = lerp(1.0, 1.0 - scanlineThickness, step(frac(linePosition), 0.5));
    color *= lerp(1.0, lineFactor, scanlineIntensity);

    if (phosphorIntensity > 0.001)
    {
        float channel = fmod(floor(uv.x * screenSize.x), 3.0);
        float3 mask = channel < 1.0 ? float3(1.0, 0.82, 0.82) : (channel < 2.0 ? float3(0.82, 1.0, 0.82) : float3(0.82, 0.82, 1.0));
        color *= lerp(1.0.xxx, mask, phosphorIntensity);
    }
    return color;
}

float3 ApplyVignette(float2 uv, float3 color)
{
    float2 centered = uv * 2.0 - 1.0;
    float distanceFromCenter = length(centered);
    float vignette = smoothstep(vignetteWidth, 1.414, distanceFromCenter);
    return color * (1.0 - vignette * vignetteIntensity);
}

float4 PSMain(VertexOutput input) : SV_Target
{
    float2 uv = input.uv;
    float2 originalUv = uv;
    if (pixelSize > 1.001)
    {
        float2 pixels = max(screenSize / pixelSize, float2(1.0, 1.0));
        uv = (floor(uv * pixels) + 0.5) / pixels;
    }

    float3 original = SampleRgb(originalUv);
    float3 color = SampleChromatic(uv);
    if (sharpen > 0.001)
    {
        float2 texel = 1.0 / max(screenSize, float2(1.0, 1.0));
        float3 neighbours = SampleRgb(uv + float2(texel.x, 0.0)) + SampleRgb(uv - float2(texel.x, 0.0)) +
                            SampleRgb(uv + float2(0.0, texel.y)) + SampleRgb(uv - float2(0.0, texel.y));
        color = saturate(color * (1.0 + sharpen) - neighbours * (sharpen * 0.25));
    }
    color = ApplyColour(color);
    color = ApplyCrt(uv, color);
    color = ApplyBloom(uv, color);
    color = ApplyVignette(uv, color);

    if (grainIntensity > 0.001)
    {
        float2 grainUv = floor(uv * screenSize / max(grainSize, 0.25));
        float grain = Hash21(grainUv + frame + time * 17.0) - 0.5;
        color = saturate(color + grain * grainIntensity);
    }

    color = lerp(original, color, globalIntensity);
    return float4(saturate(color), 1.0);
}
