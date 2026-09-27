// ============================================================================
// Eunoia Engine - WaterPS.hlsl
// AquaTerrain DX12 Water Pixel Shader:
// Depth thickness, Shallow/Deep color, Shoreline Foam with FBM, Fresnel, Specular, Sparkles
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
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldPos);

    // Directional light direction (sun)
    float3 L = normalize(float3(0.4, 0.85, 0.35));
    float3 H = normalize(V + L);

    // --- 1. Scene Depth Sampling & Thickness (AquaTerrain Section 13 & 14) ---
    float2 screenUV = input.clipPos.xy / max(screenSize, float2(1.0, 1.0));
    float rawSceneDepth = gSceneDepth.SampleLevel(gPointSampler, screenUV, 0);

    float sceneLinearZ = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float waterLinearZ = LinearizeDepth(input.clipPos.z, nearPlane, farPlane);

    float thickness = max(sceneLinearZ - waterLinearZ, 0.0);

    // Shallow to Deep color blending
    float depthFactor = saturate(thickness / max(depthFade, 0.01));
    float4 baseColor = lerp(shallowColor, waterColor, depthFactor);

    // --- 2. Shoreline Foam (Section 15 & 16) ---
    float foam = 0.0;
    if (foamEnabled > 0.5)
    {
        float foamFactor = 1.0 - saturate(thickness / max(foamDistance, 0.01));
        float foamNoise  = WaterFBM(input.worldPos.xz * foamScale + time * foamSpeed);
        foam = smoothstep(0.3, 0.75, foamFactor * foamNoise) * foamStrength;

        // Wave crest foam based on normal inclination
        float crestFactor = saturate((1.0 - N.y) * 2.0);
        foam = max(foam, crestFactor * foamNoise * 0.4 * foamStrength);
    }

    // --- 3. Fresnel Reflection (Section 17) ---
    float NdotV = saturate(dot(N, V));
    float fresnel = pow(1.0 - NdotV, 5.0) * fresnelStrength;
    fresnel = saturate(fresnel);

    // --- 4. Specular Highlights & Sparkle / Sun Glint (Section 18 & 19) ---
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, max(specularPower, 1.0)) * specularIntensity;
    float sparkle = pow(NdotH, max(sparklePower, 10.0)) * specularIntensity * 2.5;

    // --- 5. Reflection Environment Color ---
    float3 skyReflectColor = lerp(float3(0.35, 0.50, 0.65), float3(0.80, 0.90, 1.0), fresnel);

    // --- 6. Composition ---
    float3 finalColor = lerp(baseColor.rgb, skyReflectColor, fresnel * reflectionStr);
    finalColor += (spec + sparkle);
    finalColor = lerp(finalColor, float3(1.0, 1.0, 1.0), foam);
    finalColor = saturate(finalColor);

    // Alpha transparency based on depth thickness and fresnel
    float waterAlpha = saturate(lerp(saturate(thickness / 0.35), 1.0, fresnel) * opacity);
    waterAlpha = max(waterAlpha, foam * 0.9); // Foam is opaque

    return float4(finalColor, waterAlpha);
}
