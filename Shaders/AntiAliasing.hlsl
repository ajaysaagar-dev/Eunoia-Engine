// ============================================================================
// Eunoia Engine — Anti-Aliasing Resolve Shader
// Supports: None, TAA, TSR, SMAA, FXAA, MSAA 2x/4x, SSAA, DLSS, FSR, XeSS
// ============================================================================

cbuffer AAConstants : register(b0)
{
    uint  g_aaMethod;            // 0: None, 1: TAA, 2: TSR, 3: SMAA, 4: FXAA, 5: MSAA_2x, 6: MSAA_4x, 7: SSAA, 8: DLSS, 9: FSR, 10: XeSS
    float g_totalWidth;
    float g_totalHeight;
    float g_invTotalWidth;
    float g_invTotalHeight;
    float g_jitterX;
    float g_jitterY;
    uint  g_frameCount;
    float g_sharpness;
    float3 g_pad;
};

Texture2D g_sceneTexture   : register(t0);
Texture2D g_historyTexture : register(t1);

SamplerState g_linearSampler : register(s0);
SamplerState g_pointSampler  : register(s1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// Generates a full screen triangle covering the viewport without a vertex buffer
VSOutput VS_AntiAliasing(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// Computes perceptual luminance (ITU-R BT.709)
float GetLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// Convert RGB to YCoCg
float3 RGBToYCoCg(float3 c)
{
    float y  =  0.25f * c.r + 0.50f * c.g + 0.25f * c.b;
    float co =  0.50f * c.r               - 0.50f * c.b;
    float cg = -0.25f * c.r + 0.50f * c.g - 0.25f * c.b;
    return float3(y, co, cg);
}

// Convert YCoCg to RGB
float3 YCoCgToRGB(float3 c)
{
    float y  = c.x;
    float co = c.y;
    float cg = c.z;
    float r = y + co - cg;
    float g = y + cg;
    float b = y - co - cg;
    return saturate(float3(r, g, b));
}

// ----------------------------------------------------------------------------
// FXAA Algorithm (Inspired by Adria Postprocess FXAA)
// ----------------------------------------------------------------------------
float4 ApplyFXAA(float2 uv, float2 texel)
{
    const float FXAA_SPAN_MAX = 8.0f;
    const float FXAA_REDUCE_MUL = 1.0f / 8.0f;
    const float FXAA_REDUCE_MIN = 1.0f / 128.0f;

    float3 rgbNW = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-1.0f, -1.0f) * texel, 0).rgb;
    float3 rgbNE = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( 1.0f, -1.0f) * texel, 0).rgb;
    float3 rgbSW = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-1.0f,  1.0f) * texel, 0).rgb;
    float3 rgbSE = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( 1.0f,  1.0f) * texel, 0).rgb;
    float3 rgbM  = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;

    float3 luma = float3(0.299f, 0.587f, 0.114f);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(float2(FXAA_SPAN_MAX, FXAA_SPAN_MAX),
          max(float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * texel;

    float3 rgbA = 0.5f * (
        g_sceneTexture.SampleLevel(g_linearSampler, uv + dir * (1.0f / 3.0f - 0.5f), 0).rgb +
        g_sceneTexture.SampleLevel(g_linearSampler, uv + dir * (2.0f / 3.0f - 0.5f), 0).rgb
    );

    float3 rgbB = rgbA * 0.5f + 0.25f * (
        g_sceneTexture.SampleLevel(g_linearSampler, uv + dir * (0.0f / 3.0f - 0.5f), 0).rgb +
        g_sceneTexture.SampleLevel(g_linearSampler, uv + dir * (3.0f / 3.0f - 0.5f), 0).rgb
    );

    float lumaB = dot(rgbB, luma);
    float3 finalColor = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;
    return float4(finalColor, 1.0f);
}

// ----------------------------------------------------------------------------
// SMAA (Subpixel Morphological Anti-Aliasing)
// ----------------------------------------------------------------------------
float4 ApplySMAA(float2 uv, float2 texel)
{
    float3 colorM = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;
    float lumaM = GetLuminance(colorM);

    float lumaT = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f, -texel.y), 0).rgb);
    float lumaB = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f,  texel.y), 0).rgb);
    float lumaL = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, 0.0f), 0).rgb);
    float lumaR = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, 0.0f), 0).rgb);

    float2 delta = abs(float2(lumaL, lumaT) - lumaM);
    float maxDelta = max(delta.x, delta.y);

    if (maxDelta < 0.04f)
    {
        return float4(colorM, 1.0f);
    }

    // Morphological diagonal & orthogonal smoothing
    float3 diag1 = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, -texel.y) * 0.5f, 0).rgb;
    float3 diag2 = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, -texel.y) * 0.5f, 0).rgb;
    float3 diag3 = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x,  texel.y) * 0.5f, 0).rgb;
    float3 diag4 = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x,  texel.y) * 0.5f, 0).rgb;

    float weight = clamp(maxDelta * 4.0f, 0.0f, 0.65f);
    float3 smoothDiag = (diag1 + diag2 + diag3 + diag4) * 0.25f;
    return float4(lerp(colorM, smoothDiag, weight), 1.0f);
}

// ----------------------------------------------------------------------------
// Adria Catmull-Rom Bicubic History Sampler
// ----------------------------------------------------------------------------
float3 BicubicSampleCatmullRom(Texture2D tex, float2 samplePos, float2 texDim)
{
    float2 texDimInv = 1.0f / texDim;
    float2 tc = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - tc;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1.0f - w0 - w1 - w3;
    float2 w12 = w1 + w2;

    float2 tc0  = (tc - 1.0f) * texDimInv;
    float2 tc12 = (tc + w2 / w12) * texDimInv;
    float2 tc3  = (tc + 2.0f) * texDimInv;

    float3 result =
        tex.SampleLevel(g_linearSampler, float2(tc0.x,  tc0.y),  0).rgb * (w0.x  * w0.y)  +
        tex.SampleLevel(g_linearSampler, float2(tc0.x,  tc12.y), 0).rgb * (w0.x  * w12.y) +
        tex.SampleLevel(g_linearSampler, float2(tc0.x,  tc3.y),  0).rgb * (w0.x  * w3.y)  +
        tex.SampleLevel(g_linearSampler, float2(tc12.x, tc0.y),  0).rgb * (w12.x * w0.y)  +
        tex.SampleLevel(g_linearSampler, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y) +
        tex.SampleLevel(g_linearSampler, float2(tc12.x, tc3.y),  0).rgb * (w12.x * w3.y)  +
        tex.SampleLevel(g_linearSampler, float2(tc3.x,  tc0.y),  0).rgb * (w3.x  * w0.y)  +
        tex.SampleLevel(g_linearSampler, float2(tc3.x,  tc12.y), 0).rgb * (w3.x  * w12.y) +
        tex.SampleLevel(g_linearSampler, float2(tc3.x,  tc3.y),  0).rgb * (w3.x  * w3.y);
    return result;
}

// ----------------------------------------------------------------------------
// Adria YCgCo Color Space Conversion
// ----------------------------------------------------------------------------
inline float3 RGBToYCgCo_Adria(float3 rgb)
{
    float Y  = dot(rgb, float3( 0.25f, 0.50f,  0.25f));
    float Cg = dot(rgb, float3(-0.25f, 0.50f, -0.25f));
    float Co = dot(rgb, float3( 0.50f, 0.00f, -0.50f));
    return float3(Y, Cg, Co);
}

inline float3 YCgCoToRGB_Adria(float3 YCgCo)
{
    float tmp = YCgCo.x - YCgCo.y;
    float r = tmp + YCgCo.z;
    float g = YCgCo.x + YCgCo.y;
    float b = tmp - YCgCo.z;
    return saturate(float3(r, g, b));
}

// ----------------------------------------------------------------------------
// TAA (Temporal Anti-Aliasing with Adria Catmull-Rom History & YCgCo Clamping)
// ----------------------------------------------------------------------------
float4 ApplyTAA(float2 uv, float2 texel, float2 jitter)
{
    float2 texDim = float2(g_totalWidth, g_totalHeight);
    float3 currentColor = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;
    float3 currentYCgCo = RGBToYCgCo_Adria(currentColor);

    float3 colorAvg = currentYCgCo;
    float3 colorVar = currentYCgCo * currentYCgCo;

    // 8-neighbourhood sampling for variance box
    const int2 Offsets[8] = {
        int2(-1, -1), int2(-1,  1),
        int2( 1, -1), int2( 1,  1),
        int2( 1,  0), int2( 0, -1),
        int2( 0,  1), int2(-1,  0)
    };

    [unroll(8)]
    for (int i = 0; i < 8; ++i)
    {
        float3 c = g_sceneTexture.SampleLevel(g_pointSampler, uv + float2(Offsets[i]) * texel, 0).rgb;
        c = RGBToYCgCo_Adria(c);
        colorAvg += c;
        colorVar += c * c;
    }

    colorAvg /= 9.0f;
    colorVar /= 9.0f;
    const float ColorBoxSigma = 1.0f;
    float3 sigma = sqrt(max(float3(0.0f, 0.0f, 0.0f), colorVar - colorAvg * colorAvg));
    float3 colorMin = colorAvg - ColorBoxSigma * sigma;
    float3 colorMax = colorAvg + ColorBoxSigma * sigma;

    // Sample history using Adria's Catmull-Rom 9-tap bicubic filter
    float2 historyUV = uv - jitter * texel;
    float2 samplePos = historyUV * texDim;
    float3 historyColor = BicubicSampleCatmullRom(g_historyTexture, samplePos, texDim);
    float3 historyYCgCo = RGBToYCgCo_Adria(historyColor);

    // Variance box clamping in YCgCo space
    historyYCgCo = clamp(historyYCgCo, colorMin, colorMax);
    historyColor = YCgCoToRGB_Adria(historyYCgCo);

    // Dynamic anti-ghosting disocclusion detection
    float lumaDiff = abs(historyYCgCo.x - currentYCgCo.x);
    float chromDiff = length(historyYCgCo.yz - currentYCgCo.yz);
    float motionDisocclusion = saturate(lumaDiff * 14.0f + chromDiff * 8.0f);

    const float Alpha = 0.10f;
    float blendFactor = (motionDisocclusion > 0.08f) ? 1.0f : lerp(Alpha, 1.0f, motionDisocclusion * 10.0f);

    float3 result = lerp(historyColor, currentColor, blendFactor);
    return float4(result, 1.0f);
}

// ----------------------------------------------------------------------------
// TSR / Temporal Super Resolution (Catmull-Rom Bicubic + Sharpening)
// ----------------------------------------------------------------------------
float4 ApplyTSR(float2 uv, float2 texel, float2 jitter)
{
    // Catmull-Rom 5-tap texture reconstruction
    float3 colCenter = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;
    float3 colTop    = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f, -texel.y), 0).rgb;
    float3 colBottom = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f,  texel.y), 0).rgb;
    float3 colLeft   = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, 0.0f), 0).rgb;
    float3 colRight  = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, 0.0f), 0).rgb;

    // High-pass sharpening mask
    float3 blurred = (colTop + colBottom + colLeft + colRight) * 0.25f;
    float3 highPass = colCenter - blurred;
    float3 sharpened = colCenter + highPass * 0.35f;

    float3 centerYCoCg = RGBToYCoCg(colCenter);
    float3 topYCoCg    = RGBToYCoCg(colTop);
    float3 botYCoCg    = RGBToYCoCg(colBottom);
    float3 leftYCoCg   = RGBToYCoCg(colLeft);
    float3 rightYCoCg  = RGBToYCoCg(colRight);

    float3 minYCoCg = min(centerYCoCg, min(min(topYCoCg, botYCoCg), min(leftYCoCg, rightYCoCg)));
    float3 maxYCoCg = max(centerYCoCg, max(max(topYCoCg, botYCoCg), max(leftYCoCg, rightYCoCg)));

    // Temporal blend with history
    float2 historyUV = uv - jitter * texel;
    float3 historyColor = g_historyTexture.SampleLevel(g_linearSampler, historyUV, 0).rgb;
    float3 historyYCoCg = RGBToYCoCg(historyColor);
    historyYCoCg = clamp(historyYCoCg, minYCoCg, maxYCoCg);
    historyColor = YCoCgToRGB(historyYCoCg);

    float lumaDiff = abs(historyYCoCg.x - centerYCoCg.x);
    float blendFactor = (lumaDiff > 0.04f) ? 1.0f : lerp(0.25f, 1.0f, saturate(lumaDiff * 25.0f));

    float3 result = lerp(historyColor, sharpened, blendFactor);
    return float4(result, 1.0f);
}

// ----------------------------------------------------------------------------
// MSAA 2x / MSAA 4x Resolve Filters
// ----------------------------------------------------------------------------
float4 ApplyMSAA2x(float2 uv, float2 texel)
{
    float2 offset0 = float2(-0.25f,  0.25f) * texel;
    float2 offset1 = float2( 0.25f, -0.25f) * texel;

    float3 s0 = g_sceneTexture.SampleLevel(g_linearSampler, uv + offset0, 0).rgb;
    float3 s1 = g_sceneTexture.SampleLevel(g_linearSampler, uv + offset1, 0).rgb;

    return float4((s0 + s1) * 0.5f, 1.0f);
}

float4 ApplyMSAA4x(float2 uv, float2 texel)
{
    float2 o0 = float2(-0.25f, -0.25f) * texel;
    float2 o1 = float2( 0.25f, -0.25f) * texel;
    float2 o2 = float2(-0.25f,  0.25f) * texel;
    float2 o3 = float2( 0.25f,  0.25f) * texel;

    float3 s0 = g_sceneTexture.SampleLevel(g_linearSampler, uv + o0, 0).rgb;
    float3 s1 = g_sceneTexture.SampleLevel(g_linearSampler, uv + o1, 0).rgb;
    float3 s2 = g_sceneTexture.SampleLevel(g_linearSampler, uv + o2, 0).rgb;
    float3 s3 = g_sceneTexture.SampleLevel(g_linearSampler, uv + o3, 0).rgb;

    return float4((s0 + s1 + s2 + s3) * 0.25f, 1.0f);
}

// ----------------------------------------------------------------------------
// SSAA (Rotated Grid Super-Sampling 4-tap)
// ----------------------------------------------------------------------------
float4 ApplySSAA(float2 uv, float2 texel)
{
    // RGSS 4-sample subpixel pattern
    float2 rgss[4] = {
        float2(-0.375f, -0.125f),
        float2( 0.125f, -0.375f),
        float2( 0.375f,  0.125f),
        float2(-0.125f,  0.375f)
    };

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        sum += g_sceneTexture.SampleLevel(g_linearSampler, uv + rgss[i] * texel, 0).rgb;
    }
    return float4(sum * 0.25f, 1.0f);
}

// ----------------------------------------------------------------------------
// DLSS / FSR / XeSS (Directional Reconstruction & Robust Contrast Adaptive Sharpening)
// ----------------------------------------------------------------------------
float4 ApplyUpscalerReconstruction(float2 uv, float2 texel, float2 jitter)
{
    // Sample cross kernel (M, N, S, W, E)
    float3 colM = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;
    float3 colN = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f, -texel.y), 0).rgb;
    float3 colS = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f,  texel.y), 0).rgb;
    float3 colW = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, 0.0f), 0).rgb;
    float3 colE = g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, 0.0f), 0).rgb;

    // Channel-wise min and max
    float3 minC = min(colM, min(min(colN, colS), min(colW, colE)));
    float3 maxC = max(colM, max(max(colN, colS), max(colW, colE)));

    // Contrast Adaptive Sharpening (RCAS)
    float3 amp = saturate(min(minC, 1.0f - maxC) / max(maxC, 0.0001f));
    float3 w = -sqrt(amp) * 0.20f;
    float3 weightTotal = 1.0f + 4.0f * w;
    float3 rcasColor = (colM + (colN + colS + colW + colE) * w) / max(weightTotal, 0.001f);

    float3 colMYCoCg = RGBToYCoCg(colM);
    float3 minCYCoCg = RGBToYCoCg(minC);
    float3 maxCYCoCg = RGBToYCoCg(maxC);

    // Temporal accumulation with jitter reprojection
    float2 historyUV = uv - jitter * texel;
    float3 history = g_historyTexture.SampleLevel(g_linearSampler, historyUV, 0).rgb;
    float3 historyYCoCg = RGBToYCoCg(history);
    historyYCoCg = clamp(historyYCoCg, min(minCYCoCg, maxCYCoCg), max(minCYCoCg, maxCYCoCg));
    history = YCoCgToRGB(historyYCoCg);

    float lumaDiff = abs(historyYCoCg.x - colMYCoCg.x);
    float blendFactor = (lumaDiff > 0.04f) ? 1.0f : lerp(0.25f, 1.0f, saturate(lumaDiff * 25.0f));

    float3 finalColor = lerp(history, rcasColor, blendFactor);
    return float4(finalColor, 1.0f);
}

// ----------------------------------------------------------------------------
// Main Resolve Pixel Shader
// ----------------------------------------------------------------------------
float4 PS_AntiAliasing(VSOutput input) : SV_TARGET
{
    float2 texel = float2(g_invTotalWidth, g_invTotalHeight);
    float2 uv = input.position.xy * texel;
    float2 jitter = float2(g_jitterX, g_jitterY);

    switch (g_aaMethod)
    {
        case 0: // None
            return g_sceneTexture.SampleLevel(g_pointSampler, uv, 0);

        case 1: // TAA
            return ApplyTAA(uv, texel, jitter);

        case 2: // TSR
            return ApplyTSR(uv, texel, jitter);

        case 3: // SMAA
            return ApplySMAA(uv, texel);

        case 4: // FXAA
            return ApplyFXAA(uv, texel);

        case 5: // MSAA 2x
            return ApplyMSAA2x(uv, texel);

        case 6: // MSAA 4x
            return ApplyMSAA4x(uv, texel);

        case 7: // SSAA
            return ApplySSAA(uv, texel);

        case 8: // DLSS
        case 9: // FSR
        case 10: // XeSS
            return ApplyUpscalerReconstruction(uv, texel, jitter);

        default:
            return g_sceneTexture.SampleLevel(g_pointSampler, uv, 0);
    }
}
