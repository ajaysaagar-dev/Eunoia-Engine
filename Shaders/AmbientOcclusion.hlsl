// ============================================================================
// Eunoia Engine — Ambient Occlusion Shaders (DirectX 12)
// Reference: Wicked Engine (wiRenderer / wiRenderPath3D: SSAO, HBAO, MSAO, RTAO)
// ============================================================================

cbuffer AOConstants : register(b0)
{
    float4x4 g_viewMatrix;
    float4x4 g_projMatrix;
    float4x4 g_invProjMatrix;
    float4   g_resolution;       // x: width, y: height, z: 1/width, w: 1/height
    float4   g_cameraParams;     // x: zNear, y: zFar, z: fov, w: aspect
    float4   g_viewportParams;   // x: vx, y: vy, z: vw, w: vh
    float4   g_aoParams;         // x: aoPower, y: aoRange, z: aoSampleCount, w: aoBias
    uint4    g_aoConfig;         // x: aoType (1:SSAO, 2:HBAO, 3:MSAO, 4:RTAO), y: enableBlur, z: blurRadius, w: frameCount
};

Texture2D<float>  g_depthTexture : register(t0);
Texture2D<float>  g_aoRawTexture : register(t1);

SamplerState g_pointClamp  : register(s0);
SamplerState g_linearClamp : register(s1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// Full-screen triangle without vertex buffers
VSOutput VS_AO(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// Convert camera viewport UV (0..1) to full texture screen UV (0..1)
float2 ViewportUVToScreenUV(float2 vpUV)
{
    float2 pixelPos = g_viewportParams.xy + vpUV * g_viewportParams.zw;
    return pixelPos * g_resolution.zw;
}

// Convert hardware depth to linear view-space depth (positive meters from camera)
float LinearizeDepth(float depth)
{
    float zNear = g_cameraParams.x;
    float zFar  = g_cameraParams.y;
    return (zNear * zFar) / max(zFar - depth * (zFar - zNear), 1e-6f);
}

// Reconstruct view-space position from viewport UV and hardware depth
float3 ReconstructViewPos(float2 vpUV, float depth)
{
    float4 clip = float4(vpUV.x * 2.0f - 1.0f, (1.0f - vpUV.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 view = mul(g_invProjMatrix, clip);
    return view.xyz / view.w;
}

// 4-tap discontinuity-aware normal reconstruction (Wicked Engine ssaoCS.hlsl algorithm)
float3 ReconstructNormalFromDepth(float2 vpUV, float2 screenUV, float centerDepth, float3 centerPos)
{
    float2 texel = g_resolution.zw;
    float2 vpTexel = 1.0f / max(g_viewportParams.zw, float2(1.0f, 1.0f));

    float dRight = g_depthTexture.SampleLevel(g_pointClamp, screenUV + float2(texel.x, 0.0f), 0);
    float dLeft  = g_depthTexture.SampleLevel(g_pointClamp, screenUV - float2(texel.x, 0.0f), 0);
    float dDown  = g_depthTexture.SampleLevel(g_pointClamp, screenUV + float2(0.0f, texel.y), 0);
    float dUp    = g_depthTexture.SampleLevel(g_pointClamp, screenUV - float2(0.0f, texel.y), 0);

    float3 pRight = ReconstructViewPos(vpUV + float2(vpTexel.x, 0.0f), dRight);
    float3 pLeft  = ReconstructViewPos(vpUV - float2(vpTexel.x, 0.0f), dLeft);
    float3 pDown  = ReconstructViewPos(vpUV + float2(0.0f, vpTexel.y), dDown);
    float3 pUp    = ReconstructViewPos(vpUV - float2(0.0f, vpTexel.y), dUp);

    float lR = abs(dRight - centerDepth);
    float lL = abs(dLeft - centerDepth);
    float lD = abs(dDown - centerDepth);
    float lU = abs(dUp - centerDepth);

    // Pick horizontal and vertical vectors with least depth discontinuity
    float3 pHoriz = (lR < lL) ? (pRight - centerPos) : (centerPos - pLeft);
    float3 pVert  = (lU < lD) ? (pUp - centerPos) : (centerPos - pDown);

    float3 normal = cross(pHoriz, pVert);
    float len = length(normal);
    normal = (len > 1e-5f) ? (normal / len) : float3(0.0f, 0.0f, 1.0f);

    // Ensure normal points towards camera in view space
    float3 viewDir = -normalize(centerPos);
    if (dot(normal, viewDir) < 0.0f)
    {
        normal = -normal;
    }
    return normal;
}

// Pseudo-random noise hash (from Wicked Engine RNG)
float3 Hash3(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(443.897f, 441.423f, 437.195f));
    p3 += dot(p3, p3.yzx + 19.19f);
    return frac((p3.xxy + p3.yzz) * p3.zyx) * 2.0f - 1.0f;
}

// Low-discrepancy Hammersley sequence point on 2D hemisphere
float2 Hammersley2D(uint i, uint N)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rad = float(bits) * 2.3283064365386963e-10f;
    return float2(float(i) / float(N), rad);
}

float3 HemisphereUniform(float u, float v)
{
    float phi = v * 6.28318530718f;
    float cosTheta = 1.0f - u;
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// ============================================================================
// TECHNIQUE 1: SSAO (Screen Space Ambient Occlusion - Wicked Engine Reference)
// ============================================================================
float ComputeSSAO(float2 vpUV, float2 screenUV, float depth, float3 P, float3 N, uint sampleCount, float radius, float bias)
{
    float3 noise = Hash3(vpUV * g_viewportParams.zw + float(g_aoConfig.w % 64u));
    float3 tangent = normalize(noise - N * dot(noise, N));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0f;
    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 hamm = Hammersley2D(i, sampleCount);
        float3 hemi = HemisphereUniform(hamm.x, hamm.y);
        float3 rayDir = mul(hemi, TBN);

        // Modulate sample radius exponentially towards the center for high contact sharpness
        float scale = lerp(0.2f, 1.0f, (float(i) / float(sampleCount)) * (float(i) / float(sampleCount)));
        float3 samplePos = P + rayDir * (radius * scale);

        // Project sample point to screen UV
        float4 proj = mul(g_projMatrix, float4(samplePos, 1.0f));
        float2 sampleVPUV = proj.xy / proj.w * float2(0.5f, -0.5f) + 0.5f;

        [branch]
        if (sampleVPUV.x >= 0.0f && sampleVPUV.x <= 1.0f && sampleVPUV.y >= 0.0f && sampleVPUV.y <= 1.0f)
        {
            float2 sampleScreenUV = ViewportUVToScreenUV(sampleVPUV);
            float sDepth = g_depthTexture.SampleLevel(g_pointClamp, sampleScreenUV, 0);

            float linearReal = LinearizeDepth(proj.z / proj.w);
            float linearSample = LinearizeDepth(sDepth);

            // Wicked Engine depth_fix: cancels occlusion if sample penetrates beyond radius
            float depthDiff = linearReal - linearSample;
            float depthFix = saturate(1.0f - abs(depthDiff) / max(radius, 0.01f));

            // Surface in front of sample point occludes it (eliminates ghosting trails)
            if (depthDiff > bias && depthFix > 0.0f)
            {
                occlusion += depthFix;
            }
        }
    }
    return occlusion / max(float(sampleCount), 1.0f);
}

// ============================================================================
// TECHNIQUE 2: HBAO (Horizon Based Ambient Occlusion - Wicked Engine Reference)
// ============================================================================
float ComputeHBAO(float2 vpUV, float2 screenUV, float depth, float3 P, float3 N, uint sampleCount, float radius, float bias)
{
    float2 noise = Hash3(vpUV * g_viewportParams.zw).xy;
    float centerLinearZ = LinearizeDepth(depth);
    float stepSize = max(radius * 10.0f / max(centerLinearZ, 0.5f), 1.5f);
    uint numDirections = clamp(sampleCount / 2u, 4u, 16u);
    uint numSteps = 4u;

    float occlusion = 0.0f;
    float angleStep = 6.2831853f / float(numDirections);

    [loop]
    for (uint d = 0; d < numDirections; ++d)
    {
        float angle = float(d) * angleStep + noise.x * 6.2831853f;
        float2 dir = float2(cos(angle), sin(angle)) * (1.0f / max(g_viewportParams.zw, float2(1.0f, 1.0f)));
        float maxHorizonTan = -0.05f;

        [unroll]
        for (uint s = 1; s <= numSteps; ++s)
        {
            float2 sampleVPUV = vpUV + dir * (float(s) * stepSize);
            if (sampleVPUV.x < 0.0f || sampleVPUV.x > 1.0f || sampleVPUV.y < 0.0f || sampleVPUV.y > 1.0f) break;

            float2 sampleScreenUV = ViewportUVToScreenUV(sampleVPUV);
            float sDepth = g_depthTexture.SampleLevel(g_pointClamp, sampleScreenUV, 0);
            float3 sPos = ReconstructViewPos(sampleVPUV, sDepth);
            float3 V = sPos - P;
            float dist = length(V);

            // Distance threshold prevents ghosting beyond radius
            if (dist > 0.001f && dist < radius)
            {
                float tanAngle = dot(normalize(V), N);
                if (tanAngle > maxHorizonTan + bias)
                {
                    float falloff = saturate(1.0f - (dist / radius) * (dist / radius));
                    occlusion += (tanAngle - maxHorizonTan) * falloff;
                    maxHorizonTan = tanAngle;
                }
            }
        }
    }
    return saturate(occlusion / float(numDirections));
}

// ============================================================================
// TECHNIQUE 3: MSAO (Multi-Scale Ambient Occlusion - Wicked Engine Reference)
// ============================================================================
float ComputeMSAO(float2 vpUV, float2 screenUV, float depth, float3 P, float3 N, uint sampleCount, float radius, float bias)
{
    float r1 = radius * 0.35f;
    float r2 = radius * 0.85f;
    float r3 = radius * 1.80f;

    uint subSamples = max(sampleCount / 3u, 4u);
    float occ1 = ComputeSSAO(vpUV, screenUV, depth, P, N, subSamples, r1, bias);
    float occ2 = ComputeSSAO(vpUV, screenUV, depth, P, N, subSamples, r2, bias * 1.5f);
    float occ3 = ComputeSSAO(vpUV, screenUV, depth, P, N, subSamples, r3, bias * 2.0f);

    return saturate(occ1 * 0.50f + occ2 * 0.35f + occ3 * 0.15f);
}

// ============================================================================
// TECHNIQUE 4: RTAO (Ray Traced Ambient Occlusion Simulation - Wicked Reference)
// ============================================================================
float ComputeRTAO(float2 vpUV, float2 screenUV, float depth, float3 P, float3 N, uint sampleCount, float radius, float bias)
{
    uint rtaoSamples = clamp(sampleCount * 2u, 16u, 64u);
    float occlusion = 0.0f;
    float3 noise = Hash3(vpUV * g_viewportParams.zw + float((g_aoConfig.w * 13u) % 128u));
    float3 tangent = normalize(noise - N * dot(noise, N));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    [loop]
    for (uint i = 0; i < rtaoSamples; ++i)
    {
        float2 hamm = Hammersley2D(i, rtaoSamples);
        float phi = hamm.y * 6.2831853f;
        float cosT = sqrt(1.0f - hamm.x);
        float sinT = sqrt(hamm.x);
        float3 rayDir = mul(float3(cos(phi) * sinT, sin(phi) * sinT, cosT), TBN);

        float rayLen = radius * lerp(0.2f, 1.0f, hamm.x);
        float3 samplePos = P + rayDir * rayLen;

        float4 proj = mul(g_projMatrix, float4(samplePos, 1.0f));
        float2 sampleVPUV = proj.xy / proj.w * float2(0.5f, -0.5f) + 0.5f;

        [branch]
        if (sampleVPUV.x >= 0.0f && sampleVPUV.x <= 1.0f && sampleVPUV.y >= 0.0f && sampleVPUV.y <= 1.0f)
        {
            float2 sampleScreenUV = ViewportUVToScreenUV(sampleVPUV);
            float sDepth = g_depthTexture.SampleLevel(g_pointClamp, sampleScreenUV, 0);

            float linearReal = LinearizeDepth(proj.z / proj.w);
            float linearSample = LinearizeDepth(sDepth);

            float depthDiff = linearReal - linearSample;
            float depthFix = saturate(1.0f - abs(depthDiff) / max(radius, 0.01f));

            if (depthDiff > bias && depthFix > 0.0f)
            {
                occlusion += depthFix * cosT;
            }
        }
    }
    return saturate((occlusion / float(rtaoSamples)) * 1.5f);
}

// ============================================================================
// PIXEL SHADER: AO GENERATION (Wicked Engine Algorithm Dispatch)
// ============================================================================
float4 PS_AO_Generate(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.position.xy * g_resolution.zw;
    float depth = g_depthTexture.SampleLevel(g_pointClamp, screenUV, 0);

    // Background / skybox pixels (depth >= 1.0) have no ambient occlusion
    if (depth >= 0.99999f)
    {
        return 1.0f;
    }

    float3 P = ReconstructViewPos(input.uv, depth);
    float3 N = ReconstructNormalFromDepth(input.uv, screenUV, depth, P);

    uint  aoType      = g_aoConfig.x;
    float aoPower     = max(g_aoParams.x, 0.25f);
    float aoRange     = max(g_aoParams.y, 0.05f);
    uint  sampleCount = clamp(uint(g_aoParams.z), 4u, 64u);
    float aoBias      = max(g_aoParams.w, 0.0001f);

    float rawOcclusion = 0.0f;
    switch (aoType)
    {
        case 1: // SSAO
            rawOcclusion = ComputeSSAO(input.uv, screenUV, depth, P, N, sampleCount, aoRange, aoBias);
            break;
        case 2: // HBAO
            rawOcclusion = ComputeHBAO(input.uv, screenUV, depth, P, N, sampleCount, aoRange, aoBias);
            break;
        case 3: // MSAO
            rawOcclusion = ComputeMSAO(input.uv, screenUV, depth, P, N, sampleCount, aoRange, aoBias);
            break;
        case 4: // RTAO
            rawOcclusion = ComputeRTAO(input.uv, screenUV, depth, P, N, sampleCount, aoRange, aoBias);
            break;
        default:
            return 1.0f;
    }

    // Wicked Engine Exponential Power Law: AO = pow(saturate(1 - occ), power)
    float ao = pow(saturate(1.0f - rawOcclusion), aoPower);
    return float4(ao, ao, ao, 1.0f);
}

// ============================================================================
// PIXEL SHADER: CROSS-BILATERAL DEPTH BLUR (Edge-Preserving Filtering)
// ============================================================================
float4 PS_AO_BilateralBlur(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.position.xy * g_resolution.zw;
    float centerDepth = g_depthTexture.SampleLevel(g_pointClamp, screenUV, 0);
    if (centerDepth >= 0.99999f)
    {
        return 1.0f;
    }

    float centerLinearZ = LinearizeDepth(centerDepth);
    float centerAO = g_aoRawTexture.SampleLevel(g_pointClamp, screenUV, 0);

    float2 texel = g_resolution.zw;
    float blurRadius = max(float(g_aoConfig.z), 1.0f);

    // Cross-bilateral kernel (9 samples in horizontal + vertical cross)
    float totalAO = centerAO;
    float totalWeight = 1.0f;

    static const float offsets[4] = { -2.0f, -1.0f, 1.0f, 2.0f };
    static const float spatialWeights[4] = { 0.20f, 0.50f, 0.50f, 0.20f };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        // Horizontal tap
        float2 uvH = screenUV + float2(offsets[i] * blurRadius * texel.x, 0.0f);
        float dH = g_depthTexture.SampleLevel(g_pointClamp, uvH, 0);
        float zH = LinearizeDepth(dH);
        float aoH = g_aoRawTexture.SampleLevel(g_pointClamp, uvH, 0);

        float depthDiffH = abs(zH - centerLinearZ);
        float weightH = spatialWeights[i] * exp(-depthDiffH / max(centerLinearZ * 0.05f + 0.02f, 0.01f));
        totalAO += aoH * weightH;
        totalWeight += weightH;

        // Vertical tap
        float2 uvV = screenUV + float2(0.0f, offsets[i] * blurRadius * texel.y);
        float dV = g_depthTexture.SampleLevel(g_pointClamp, uvV, 0);
        float zV = LinearizeDepth(dV);
        float aoV = g_aoRawTexture.SampleLevel(g_pointClamp, uvV, 0);

        float depthDiffV = abs(zV - centerLinearZ);
        float weightV = spatialWeights[i] * exp(-depthDiffV / max(centerLinearZ * 0.05f + 0.02f, 0.01f));
        totalAO += aoV * weightV;
        totalWeight += weightV;
    }

    float finalAO = totalAO / max(totalWeight, 1e-4f);
    return float4(finalAO, finalAO, finalAO, 1.0f);
}

// ============================================================================
// PIXEL SHADER: SCENE LIGHTING MODULATION / COMPOSITE
// ============================================================================
float4 PS_AO_Composite(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.position.xy * g_resolution.zw;
    float ao = g_aoRawTexture.SampleLevel(g_linearClamp, screenUV, 0);
    // Modulate scene color via hardware multiplicative blending (SrcBlend = DEST_COLOR, DestBlend = ZERO)
    return float4(ao, ao, ao, 1.0f);
}
