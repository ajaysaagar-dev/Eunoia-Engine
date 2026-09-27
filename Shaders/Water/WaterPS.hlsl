// ============================================================================
// Eunoia Engine - WaterPS.hlsl
// High-Fidelity Ocean Water Pixel Shader:
// Dark Teal/Blue-Green Depth Blend, Schlick Fresnel, Controlled Specular & Sun Sparkles,
// Shoreline & Wave Crest Foam, and Debug Modes 0-6
// ============================================================================

#include "WaterCommon.hlsli"

Texture2D<float> gSceneDepth : register(t1);
SamplerState gPointSampler   : register(s1);

struct VSOut
{
    float4 clipPos  : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float  eyeDist  : TEXCOORD3;
};

float4 PSMain(VSOut input) : SV_TARGET
{
    // Micro-ripple normal perturbation for realistic sparkling surface (dev.md Section 16)
    float3 N = EvaluateDetailNormal(input.worldPos.xz, time, normalize(input.normal), normalScale);
    float3 V = normalize(cameraPos - input.worldPos);

    // Directional sunlight vector
    float3 L = normalize(float3(0.4, 0.85, 0.35));
    float3 H = normalize(V + L);

    // --- 1. Scene Depth Sampling & Thickness (dev.md Section 22) ---
    float2 screenUV = input.clipPos.xy / max(screenSize, float2(1.0, 1.0));
    float rawSceneDepth = gSceneDepth.SampleLevel(gPointSampler, screenUV, 0);

    float sceneLinearZ = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float waterLinearZ = LinearizeDepth(input.clipPos.z, nearPlane, farPlane);

    bool isSkyBackground = (rawSceneDepth >= 0.9999);
    float thickness = isSkyBackground ? 500.0 : max(sceneLinearZ - waterLinearZ, 0.0);

    // Shallow to Deep color blending (Target: Dark blue-green deep, dark teal shallow)
    float depthFactor = saturate(thickness / max(depthFade, 0.1));
    float4 baseColor = lerp(float4(shallowColor.rgb, 1.0), float4(waterColor.rgb, 1.0), depthFactor);

    // --- 2. Shoreline & Wave Crest Foam (dev.md Section 23) ---
    float foam = 0.0;
    if (foamEnabled > 0.5)
    {
        // Shoreline foam near terrain/objects
        if (!isSkyBackground)
        {
            float foamFactor = 1.0 - saturate(thickness / max(foamDistance, 0.01));
            float foamNoise  = WaterFBM(input.worldPos.xz * foamScale + time * foamSpeed);
            foam = smoothstep(0.4, 0.8, foamFactor * foamNoise) * foamStrength;
        }

        // Wave crest foam based on displacement peak
        float wavePeak = (input.worldPos.y - renderOrigin.y);
        if (wavePeak > waveHeight * 0.4 && waveHeight > 0.05)
        {
            float crestFactor = saturate((wavePeak - waveHeight * 0.4) / max(waveHeight * 0.5, 0.01));
            float crestNoise  = WaterFBM(input.worldPos.xz * (foamScale * 1.5) + time * (foamSpeed * 0.8));
            foam = max(foam, smoothstep(0.5, 0.85, crestFactor * crestNoise) * 0.35 * foamStrength);
        }
    }

    // --- 3. Schlick Fresnel (F0 = 0.02 for water, dev.md Section 19) ---
    float NdotV = saturate(dot(N, V));
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    fresnel = saturate(fresnel * fresnelStrength);

    // --- 4. Controlled Specular Highlight & Sparkle (dev.md Section 20) ---
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, max(specularPower, 16.0)) * specularIntensity * 0.75;
    float sparkle = pow(NdotH, max(sparklePower, 64.0)) * specularIntensity * 0.35;

    // --- 5. Environment Sky Reflection (dev.md Section 21) ---
    float3 R = reflect(-V, N);
    float3 skyZenith = float3(0.08, 0.22, 0.42);
    float3 skyHorizon = float3(0.35, 0.52, 0.68);
    float3 skyReflectColor = lerp(skyHorizon, skyZenith, saturate(R.y));

    // --- 6. Composition (dev.md Section 25) ---
    float3 finalColor = lerp(baseColor.rgb, skyReflectColor, fresnel * reflectionStr);
    finalColor += (spec + sparkle) * float3(1.0, 0.98, 0.92); // Warm sunlight glint
    finalColor = lerp(finalColor, float3(0.92, 0.96, 0.98), foam);
    finalColor = saturate(finalColor);

    // Alpha transparency
    float waterAlpha = isSkyBackground ? opacity : saturate(lerp(saturate(thickness / 0.5), 1.0, fresnel) * opacity);
    waterAlpha = max(waterAlpha, foam * 0.9);

    // --- 7. Debug Modes (dev.md Section 35) ---
    int dbg = (int)(debugMode + 0.5);
    if (dbg == 1) // Base Color
    {
        return float4(baseColor.rgb, 1.0);
    }
    else if (dbg == 2) // Normals
    {
        return float4(N * 0.5 + 0.5, 1.0);
    }
    else if (dbg == 3) // Height
    {
        float hNorm = saturate((input.worldPos.y - renderOrigin.y + 0.5) / 1.0);
        float3 hCol = lerp(float3(0.0, 0.1, 0.8), float3(1.0, 0.2, 0.0), hNorm);
        return float4(hCol, 1.0);
    }
    else if (dbg == 4) // Foam
    {
        return float4(foam, foam, foam, 1.0);
    }
    else if (dbg == 5) // Depth / Thickness
    {
        float dVis = saturate(thickness / 15.0);
        return float4(dVis, dVis, dVis, 1.0);
    }
    else if (dbg == 6) // Wireframe / Grid Topology
    {
        float2 grid = frac(input.uv * 256.0);
        float edge = step(grid.x, 0.08) + step(grid.y, 0.08);
        edge = saturate(edge);
        return float4(lerp(float3(0.02, 0.08, 0.15), float3(0.0, 1.0, 0.7), edge), 1.0);
    }

    return float4(finalColor, waterAlpha);
}
