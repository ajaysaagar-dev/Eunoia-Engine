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

// ----------------------------------------------------------------------------
// FXAA 3.11 Quality Algorithm
// ----------------------------------------------------------------------------
float4 ApplyFXAA(float2 uv, float2 texel)
{
    float3 colorM = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;
    float lumaM = GetLuminance(colorM);

    float lumaN = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f, -texel.y), 0).rgb);
    float lumaS = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(0.0f,  texel.y), 0).rgb);
    float lumaW = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, 0.0f), 0).rgb);
    float lumaE = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, 0.0f), 0).rgb);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit if contrast is below edge detection threshold
    const float EDGE_THRESHOLD_MIN = 0.0312f;
    const float EDGE_THRESHOLD_MAX = 0.1250f;
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
    {
        return float4(colorM, 1.0f);
    }

    // Corner samples for diagonal direction and subpixel antialiasing
    float lumaNW = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x, -texel.y), 0).rgb);
    float lumaNE = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x, -texel.y), 0).rgb);
    float lumaSW = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2(-texel.x,  texel.y), 0).rgb);
    float lumaSE = GetLuminance(g_sceneTexture.SampleLevel(g_linearSampler, uv + float2( texel.x,  texel.y), 0).rgb);

    // Compute edge gradient direction (horizontal vs vertical)
    float edgeHorz = abs((lumaNW + lumaNE) - 2.0f * lumaN) +
                     2.0f * abs((lumaW + lumaE) - 2.0f * lumaM) +
                     abs((lumaSW + lumaSE) - 2.0f * lumaS);
    float edgeVert = abs((lumaNW + lumaSW) - 2.0f * lumaW) +
                     2.0f * abs((lumaN + lumaS) - 2.0f * lumaM) +
                     abs((lumaNE + lumaSE) - 2.0f * lumaE);
    bool isHorizontal = edgeHorz >= edgeVert;

    // Select positive / negative gradient normal
    float lumaNeg = isHorizontal ? lumaN : lumaW;
    float lumaPos = isHorizontal ? lumaS : lumaE;
    float gradientNeg = abs(lumaNeg - lumaM);
    float gradientPos = abs(lumaPos - lumaM);

    float stepLength = isHorizontal ? texel.y : texel.x;
    if (gradientNeg >= gradientPos)
    {
        stepLength = -stepLength;
    }

    // Subpixel anti-aliasing blend
    float lumaL = (lumaN + lumaS + lumaW + lumaE) * 0.25f;
    float subPixelA = abs(lumaL - lumaM);
    float subPixelB = clamp(subPixelA / max(lumaRange, 0.001f), 0.0f, 1.0f);
    float subPixelBlend = (-2.0f * subPixelB + 3.0f) * subPixelB * subPixelB;
    subPixelBlend = subPixelBlend * subPixelBlend * 0.75f;

    float2 offsetUV = uv;
    if (isHorizontal)
        offsetUV.y += stepLength * (0.5f + subPixelBlend);
    else
        offsetUV.x += stepLength * (0.5f + subPixelBlend);

    float3 filtered = g_sceneTexture.SampleLevel(g_linearSampler, offsetUV, 0).rgb;
    float3 finalColor = lerp(colorM, filtered, 0.75f);
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
// TAA (Temporal Anti-Aliasing with Neighbourhood Clamping)
// ----------------------------------------------------------------------------
float4 ApplyTAA(float2 uv, float2 texel, float2 jitter)
{
    float3 currentColor = g_sceneTexture.SampleLevel(g_linearSampler, uv, 0).rgb;

    // 3x3 neighbourhood bounding box clamping
    float3 minColor = currentColor;
    float3 maxColor = currentColor;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 sampleColor = g_sceneTexture.SampleLevel(g_pointSampler, uv + float2((float)x, (float)y) * texel, 0).rgb;
            minColor = min(minColor, sampleColor);
            maxColor = max(maxColor, sampleColor);
        }
    }

    // Sample history at unjittered UV
    float2 historyUV = uv - jitter * texel;
    float3 historyColor = g_historyTexture.SampleLevel(g_linearSampler, historyUV, 0).rgb;

    // Clamp history to current neighborhood to prevent ghosting
    historyColor = clamp(historyColor, minColor, maxColor);

    // Blend: 88% historical accumulation, 12% current frame
    float blendFactor = 0.12f;
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

    // Temporal blend with history
    float2 historyUV = uv - jitter * texel;
    float3 historyColor = g_historyTexture.SampleLevel(g_linearSampler, historyUV, 0).rgb;

    float3 minC = min(colCenter, min(min(colTop, colBottom), min(colLeft, colRight)));
    float3 maxC = max(colCenter, max(max(colTop, colBottom), max(colLeft, colRight)));
    historyColor = clamp(historyColor, minC, maxC);

    float3 result = lerp(historyColor, sharpened, 0.15f);
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

    // Temporal accumulation with jitter reprojection
    float2 historyUV = uv - jitter * texel;
    float3 history = g_historyTexture.SampleLevel(g_linearSampler, historyUV, 0).rgb;
    history = clamp(history, minC, maxC);

    float3 finalColor = lerp(history, rcasColor, 0.18f);
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
