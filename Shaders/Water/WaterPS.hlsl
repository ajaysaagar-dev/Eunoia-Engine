// ============================================================================
// Eunoia Engine - WaterPS.hlsl
// Hyper-Realistic DirectX 12 Water Pixel Shader:
// - Alexander Alekseev "Seascape" Multi-Octave Choppy Wave Normals (BabylonJS #O03DUQ)
// - Dynamic Chromatic RGB Animated Caustics (Anderson Mancini / Three.js Water)
// - Physical Beer-Lambert Spectral Absorption (Exponential wavelength decay)
// - Wave Crest Translucent Subsurface Scattering (Backlit emerald-cyan glow)
// - Cook-Torrance GGX PBR Specular Microfacet BRDF with Sun Glint Road
// - Multi-scale Shoreline Contact & Trochoidal Whitecap Foam
// - Atmospheric Horizon Sky Blending & Debug Modes 0-6
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
    // 1. Multi-Octave Choppy Surface Normals (TDM Seascape, BabylonJS #O03DUQ)
    float3 N = EvaluateDetailNormal(input.worldPos.xz, time, normalize(input.normal), normalScale, choppiness);
    float3 V = normalize(cameraPos - input.worldPos);

    // Directional sunlight vector (warm sun)
    float3 L = normalize(float3(0.42, 0.82, 0.38));
    float3 H = normalize(V + L);

    // --- 2. Scene Depth Sampling & Thickness (dev.md Section 22) ---
    float2 screenUV = input.clipPos.xy / max(screenSize, float2(1.0, 1.0));
    float rawSceneDepth = gSceneDepth.SampleLevel(gPointSampler, screenUV, 0);

    float sceneLinearZ = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float waterLinearZ = LinearizeDepth(input.clipPos.z, nearPlane, farPlane);

    bool isSkyBackground = (rawSceneDepth >= 0.9999);
    float thickness = isSkyBackground ? 500.0 : max(sceneLinearZ - waterLinearZ, 0.0);

    // --- 3. Physical Beer-Lambert Spectral Absorption ---
    // Deep dark blue-green to shallow turquoise/teal based on exponential light decay
    float3 waterBodyColor = CalculateBeerLambertAbsorption(thickness, shallowColor.rgb, waterColor.rgb, depthFade);

    // --- 4. Dynamic Chromatic RGB Caustics (Anderson Mancini / Three.js Water) ---
    float3 caustics = float3(0.0, 0.0, 0.0);
    if (!isSkyBackground && refractionStr > 0.01)
    {
        caustics = EvaluateCaustics(input.worldPos.xz, time, thickness) * refractionStr;
        // Caustics shimmer under the water surface
        waterBodyColor += caustics * float3(0.85, 0.95, 1.0);
    }

    // --- 5. Wave Crest Subsurface Scattering (Backlit Emerald Glow) ---
    float wavePeak = input.worldPos.y - renderOrigin.y;
    float3 sss = EvaluateSubsurfaceScattering(V, L, N, wavePeak, waveHeight) * (1.0 - roughness * 0.5);

    // --- 6. Shoreline & Trochoidal Wave Crest Foam ---
    float foam = 0.0;
    if (foamEnabled > 0.5)
    {
        // Contact shoreline foam with edge boiling
        if (!isSkyBackground)
        {
            float foamFactor = 1.0 - saturate(thickness / max(foamDistance, 0.01));
            float foamNoise  = WaterFBM(input.worldPos.xz * foamScale + time * foamSpeed);
            foam = smoothstep(0.35, 0.78, foamFactor * foamNoise) * foamStrength;
        }

        // Trochoidal whitecap foam on steep wave crests
        if (wavePeak > waveHeight * 0.35 && waveHeight > 0.04)
        {
            float crestFactor = saturate((wavePeak - waveHeight * 0.35) / max(waveHeight * 0.55, 0.01));
            float crestNoise  = WaterFBM(input.worldPos.xz * (foamScale * 1.6) + time * (foamSpeed * 0.85));
            foam = max(foam, smoothstep(0.48, 0.82, crestFactor * crestNoise) * 0.42 * foamStrength);
        }
    }

    // --- 7. Schlick Fresnel & Environment Reflection ---
    float NdotV = max(dot(N, V), 0.001);
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    fresnel = saturate(fresnel * fresnelStrength);

    // Multi-stop atmospheric sky reflection
    float3 R = reflect(-V, N);
    float3 skyZenith = float3(0.06, 0.20, 0.44);
    float3 skyMid    = float3(0.20, 0.42, 0.62);
    float3 skyHorizon= float3(0.38, 0.56, 0.72);
    float3 skyReflectColor = (R.y > 0.2) ? lerp(skyMid, skyZenith, (R.y - 0.2) / 0.8) : lerp(skyHorizon, skyMid, max(R.y, 0.0) / 0.2);

    // --- 8. Cook-Torrance GGX Specular Highlight & Sun Road ---
    float3 ggxSpec = EvaluateCookTorranceGGX(N, V, L, max(roughness, 0.04), 0.02) * specularIntensity;
    float3 sunColor = float3(1.0, 0.96, 0.88); // Warm sun radiance
    float3 specular = ggxSpec * sunColor * 1.5;

    // Diamond capillary sparkle highlights
    float NdotH = saturate(dot(N, H));
    float sparkle = pow(NdotH, max(sparklePower, 128.0)) * specularIntensity * 0.4;
    specular += sparkle * sunColor;

    // --- 9. Final Lighting Composition ---
    float3 finalColor = lerp(waterBodyColor + sss, skyReflectColor, fresnel * reflectionStr);
    finalColor += specular;

    // Foam blend (micro-bubble soft white/cyan tint)
    float3 foamColor = float3(0.92, 0.97, 0.99);
    finalColor = lerp(finalColor, foamColor, foam);

    // Atmospheric horizon blending
    if (oceanExtent > 10.0)
    {
        float horizonBlend = smoothstep(oceanExtent * 0.35, oceanExtent * 0.49, input.eyeDist);
        finalColor = lerp(finalColor, skyHorizon, horizonBlend);
    }
    finalColor = saturate(finalColor);

    // Alpha transparency
    float waterAlpha = isSkyBackground ? opacity : saturate(lerp(saturate(thickness / 0.45), 1.0, fresnel) * opacity);
    waterAlpha = max(waterAlpha, foam * 0.95);

    // --- 10. Debug Modes 0-6 (dev.md Section 35) ---
    int dbg = (int)(debugMode + 0.5);
    if (dbg == 1) // Base Color
    {
        return float4(waterBodyColor, 1.0);
    }
    else if (dbg == 2) // Normals
    {
        return float4(N * 0.5 + 0.5, 1.0);
    }
    else if (dbg == 3) // Height Field
    {
        float hNorm = saturate((input.worldPos.y - renderOrigin.y + 0.5) / 1.0);
        return float4(lerp(float3(0.0, 0.1, 0.8), float3(1.0, 0.2, 0.0), hNorm), 1.0);
    }
    else if (dbg == 4) // Foam Mask
    {
        return float4(foam, foam, foam, 1.0);
    }
    else if (dbg == 5) // Depth / Thickness
    {
        float dVis = saturate(thickness / 15.0);
        return float4(dVis, dVis, dVis, 1.0);
    }
    else if (dbg == 6) // Wireframe / Regular Topology
    {
        float2 grid = frac(input.uv * 256.0);
        float edge = step(grid.x, 0.08) + step(grid.y, 0.08);
        return float4(lerp(float3(0.02, 0.08, 0.15), float3(0.0, 1.0, 0.7), saturate(edge)), 1.0);
    }

    return float4(finalColor, waterAlpha);
}
