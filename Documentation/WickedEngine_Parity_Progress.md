# Wicked Engine Feature Parity Progress Report

This document tracks the implementation, integration, and verification status of all graphics features and viewport capabilities ported from **Wicked Engine** into **Eunoia Engine**.

### Status Definitions:
- **NOT ANALYZED**: Feature has not yet been surveyed in Wicked Engine source code.
- **ANALYZED**: Architectural requirements, shader code, and dependencies are mapped.
- **NOT IMPLEMENTED**: Feature is mapped but code changes have not begun in Eunoia.
- **IMPLEMENTED**: Code and shaders are written in Eunoia Engine.
- **INTEGRATED**: Feature is connected to the engine scene, UI, and rendering loop.
- **TESTED**: Verified through automated compilation and live execution in the Eunoia viewport.
- **BLOCKED**: Requires specific hardware capabilities (e.g. DXR Raytracing, Tier 2 VRS, Mesh Shaders) not available on target platform.

---

## Progress Summary Matrix

| Category | Total Features | Tested | Integrated | Implemented | Not Implemented | Blocked | Parity % |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Rendering Quality** | 9 | 8 | 0 | 0 | 1 | 0 | 88.9% |
| **Shadows & Cascades**| 10 | 8 | 0 | 0 | 2 | 0 | 80.0% |
| **Lighting & GI** | 8 | 5 | 0 | 0 | 2 | 1 | 62.5% |
| **Ambient Effects & Post** | 16 | 12 | 0 | 0 | 4 | 0 | 75.0% |
| **Viewport Optimizations & Debug** | 12 | 10 | 0 | 0 | 0 | 2 | 83.3% |
| **Total** | **55** | **43** | **0** | **0** | **9** | **3** | **78.2%** |

---

## Detailed Subsystem Tracking

### 1. Rendering Quality & Anti-Aliasing
| Feature ID | Feature Name | Wicked Engine Source | Eunoia Engine Location | Status | Last Verification Date |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **RQ-01** | Render Resolution Scaling | `wiRenderPath3D.cpp`, `GraphicsWindow.cpp` | `Scene.h`, `Main.cpp` | **TESTED** | 2026-11-04 |
| **RQ-02** | Texture Quality Selection | `wiRenderer.cpp` | `Scene.h`, `Main.cpp` | **TESTED** | 2026-11-04 |
| **RQ-03** | Mip LOD Bias Adjustment | `wiRenderer::ModifyObjectSampler` | `Scene.h`, `Main.cpp` | **TESTED** | 2026-11-04 |
| **RQ-04** | Anisotropic Filtering (1x - 16x)| `wiGraphicsDevice_DX12.cpp` | `Main.cpp` (`g_samplerDesc`) | **TESTED** | 2026-11-04 |
| **RQ-05** | VSync Synchronization | `wiGraphicsDevice_DX12::Present` | `Main.cpp` (`swapChain->Present`) | **TESTED** | 2026-11-04 |
| **RQ-06** | Frame Rate Limiting | `wiApplication.cpp` | `Scene.h`, `Main.cpp` | **TESTED** | 2026-11-04 |
| **RQ-07** | Forward MSAA Geometry | `wiRenderPath3D.cpp` | `Scene.h`, `Main.cpp` | **ANALYZED** | — |
| **RQ-08** | Temporal Anti-Aliasing (TAA) | `shaders/temporalaaCS.hlsl` | `Scene.h`, `Shaders/AntiAliasing.hlsl` | **NOT IMPLEMENTED** | — |
| **RQ-09** | Fast Approximate Anti-Aliasing (FXAA) | `wiRenderer::Postprocess_FXAA` | `Scene.h`, `Shaders/AntiAliasing.hlsl` | **NOT IMPLEMENTED** | — |

---

### 2. Shadows & Cascades
| Feature ID | Feature Name | Wicked Engine Source | Eunoia Engine Location | Status | Last Verification Date |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **SH-01** | Cascaded Shadow Mapping (4 splits)| `wiRenderer::DrawShadowmaps` | `Main.cpp` (`RenderShadowPass`) | **TESTED** | 2026-11-04 |
| **SH-02** | Cascade Split Visualization | `wiRenderer.cpp` | `Main.cpp` (`visualizeCascades`) | **TESTED** | 2026-11-04 |
| **SH-03** | Cascade Smooth Blending | `shaders/shadowPS.hlsl` | `Main.cpp` (`cascadeBlending`) | **TESTED** | 2026-11-04 |
| **SH-04** | Normal Offset Bias | `shaders/ShaderInterop_Renderer.h` | `Main.cpp` (`shadowBias`) | **TESTED** | 2026-11-04 |
| **SH-05** | 16-Tap Poisson Disc / PCF | `shaders/shadowPS.hlsl` | `Main.cpp` (`pcfRadius`) | **TESTED** | 2026-11-04 |
| **SH-06** | Point Light Cube Shadow Atlas | `wiRenderer.cpp` | `Main.cpp` (`g_pointShadowMap`) | **TESTED** | 2026-11-04 |
| **SH-07** | Spot Light Frustum Shadow Maps| `wiRenderer.cpp` | `Main.cpp` (`g_spotShadowMap`) | **TESTED** | 2026-11-04 |
| **SH-08** | Shadow Distance Culling & LOD | `GraphicsWindow.cpp` (`shadowLODCheckBox`) | `Main.cpp` (`shadowLODOverride`) | **TESTED** | 2026-11-04 |
| **SH-09** | Screen Space Contact Shadows | `shaders/screenspaceshadowCS.hlsl` | `Scene.h`, `Main.cpp` | **ANALYZED** | — |
| **SH-10** | Capsule Indirect Shadows | `shaders/capsuleshadowCS.hlsl` | `Scene.h`, `Main.cpp` | **ANALYZED** | — |

---

### 3. Lighting & Global Illumination
| Feature ID | Feature Name | Wicked Engine Source | Eunoia Engine Location | Status | Last Verification Date |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **LT-01** | Directional Sun Lighting | `shaders/ShaderInterop_Renderer.h` | `Main.cpp` (`SceneConstantBuffer`) | **TESTED** | 2026-11-04 |
| **LT-02** | Dynamic Point Lights (Up to 64) | `wiRenderer.cpp` | `Main.cpp` (`pointLightPosRange`) | **TESTED** | 2026-11-04 |
| **LT-03** | Dynamic Spot Lights with Cones | `wiRenderer.cpp` | `Main.cpp` (`pointLightSpotAreaParams`) | **TESTED** | 2026-11-04 |
| **LT-04** | Rectangular Area Light Approx | `shaders/lighting.hlsli` | `Main.cpp` (`pointLightSpotAreaParams`) | **TESTED** | 2026-11-04 |
| **LT-05** | Skybox Ambient & Environment | `wiRenderer::DrawSky` | `Main.cpp` (`ambientColor`, `ambCol`) | **TESTED** | 2026-11-04 |
| **LT-06** | Global Illumination Boost Slider| `GraphicsWindow.cpp` (`GIBoostSlider`) | `Scene.h`, `Main.cpp` (`giBoost`) | **TESTED** | 2026-11-04 |
| **LT-07** | Dynamic Diffuse GI (DDGI Probes)| `shaders/ddgi_*.hlsl` | `Scene.h` (`enableDDGI`) | **NOT IMPLEMENTED** | — |
| **LT-08** | Surfel GI / Voxel GI (VXGI) | `shaders/surfel_*.hlsl`, `shaders/vxgi_*.hlsl` | `Scene.h` (`enableSurfelGI`, `enableVXGI`) | **BLOCKED** | Requires DXR / Compute Voxelization |

---

### 4. Ambient Effects & Post-Processing
| Feature ID | Feature Name | Wicked Engine Source | Eunoia Engine Location | Status | Last Verification Date |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **PP-01** | ACES Filmic Tone Mapping | `shaders/tonemapCS.hlsl` | `Main.cpp` (`postTonemapper == 1`) | **TESTED** | 2026-11-04 |
| **PP-02** | Reinhard Tone Mapping | `shaders/tonemapCS.hlsl` | `Main.cpp` (`postTonemapper == 2`) | **TESTED** | 2026-11-04 |
| **PP-03** | Exposure EV & Color Grading | `shaders/tonemapCS.hlsl` | `Main.cpp` (`postExposureEV`, `postContrast`, etc.)| **TESTED** | 2026-11-04 |
| **PP-04** | Cel Outline Edge Detection | `shaders/outlineCS.hlsl` | `Main.cpp` (`outline`, `outlineThreshold`)| **TESTED** | 2026-11-04 |
| **PP-05** | Chromatic Aberration Filter | `shaders/chromatic_aberrationCS.hlsl` | `Main.cpp` (`chromaticAberration`) | **TESTED** | 2026-11-04 |
| **PP-06** | CRT Scanline & Curvature Filter| `shaders/crt_screenCS.hlsl` | `Main.cpp` (`crtFilter`) | **TESTED** | 2026-11-04 |
| **PP-07** | Contrast Adaptive Sharpening (CAS)| `shaders/sharpenCS.hlsl` | `Main.cpp` (`sharpenFilter`) | **TESTED** | 2026-11-04 |
| **PP-08** | Dithering Filter | `shaders/tonemapCS.hlsl` | `Main.cpp` (`dither`) | **TESTED** | 2026-11-04 |
| **PP-09** | Volumetric Atmospheric Fog | `shaders/fog.hlsli` | `Main.cpp` (`fogDensity`, `fogHeightEnd`)| **TESTED** | 2026-11-04 |
| **PP-10** | Intermediate HDR Ping-Pong Target | `wiRenderPath3D.cpp` | `Main.cpp` | **NOT IMPLEMENTED** | (Phase 2 Target) |
| **PP-11** | Screen Space Ambient Occlusion (SSAO/HBAO/MSAO/RTAO)| `shaders/ssaoCS.hlsl` | `Scene.h`, `EngineUI.cpp`, `Main.cpp` | **TESTED** | 2026-11-05 |
| **PP-12** | HDR Bloom (Threshold / Strength) | `shaders/bloomseparateCS.hlsl` | `Scene.h`, `EngineUI.cpp`, `Main.cpp` | **TESTED** | 2026-11-05 |
| **PP-13** | Screen Space Reflections (SSR with Roughness Cutoff) | `shaders/ssr_*.hlsl` | `Scene.h`, `EngineUI.cpp`, `Main.cpp` | **TESTED** | 2026-11-05 |
| **PP-14** | Depth of Field (DoF Bokeh) | `shaders/depthoffield_*.hlsl` | `Scene.h` (`enableDoF`) | **NOT IMPLEMENTED** | (Phase 5H Target) |
| **PP-15** | Velocity Motion Blur | `shaders/motionblur_*.hlsl` | `Scene.h` (`enableMotionBlur`) | **NOT IMPLEMENTED** | (Phase 5I Target) |
| **PP-16** | Eye Adaptation / Auto Exposure | `shaders/luminance_*.hlsl` | `Scene.h` (`enableEyeAdaptation`) | **NOT IMPLEMENTED** | (Phase 5J Target) |

---

### 5. Viewport Optimizations & Debug Tools
| Feature ID | Feature Name | Wicked Engine Source | Eunoia Engine Location | Status | Last Verification Date |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **VO-01** | Freeze Culling Camera | `wiRenderer::SetFreezeCullingCameraEnabled` | `Main.cpp` (`freezeCullingCamera`) | **TESTED** | 2026-11-04 |
| **VO-02** | Tile Light Culling Heatmap | `wiRenderer::SetDebugLightCulling` | `Main.cpp` (`debugLightCulling`) | **TESTED** | 2026-11-04 |
| **VO-03** | Disable Albedo Maps (Clay Mode) | `wiRenderer::SetDisableAlbedoMaps` | `Main.cpp` (`disableAlbedoMaps`) | **TESTED** | 2026-11-04 |
| **VO-04** | Force Diffuse Lighting Override | `wiRenderer::SetForceDiffuseLighting` | `Main.cpp` (`forceDiffuseLighting`) | **TESTED** | 2026-11-04 |
| **VO-05** | Force Unlit Material Override | `wiRenderer::SetForceUnlit` | `Main.cpp` (`forceUnlit`) | **TESTED** | 2026-11-04 |
| **VO-06** | Shadow LOD Caster Distance Cull | `GraphicsWindow.cpp` (`shadowLODCheckBox`) | `Main.cpp` (`shadowLODOverride`) | **TESTED** | 2026-11-04 |
| **VO-07** | Game Speed Simulation Multiplier| `wiRenderer::SetGameSpeed` | `Main.cpp` (`gameSpeed`) | **TESTED** | 2026-11-04 |
| **VO-08** | Dynamic Viewport Resolution Scale| `wiRenderPath3D.cpp` | `Main.cpp` (`resolutionScale`) | **TESTED** | 2026-11-04 |
| **VO-09** | Mip LOD Sampler Bias Override | `wiRenderer::ModifyObjectSampler` | `Main.cpp` (`mipLodBias`) | **TESTED** | 2026-11-04 |
| **VO-10** | Hi-Z Depth Buffer Generation | `shaders/depth_pyramidCS.hlsl` | `Shaders/HiZBuild.hlsl` | **TESTED** | 2026-11-04 |
| **VO-11** | Variable Rate Shading (VRS Tier 1/2)| `shaders/shadingrateclassificationCS.hlsl` | `Scene.h` (`enableVRS`) | **BLOCKED** | Requires hardware support check |
| **VO-12** | Mesh Shaders Pipeline (SM 6.5) | `wiGraphicsDevice_DX12.cpp` | `Scene.h` (`enableMeshShaders`) | **BLOCKED** | Requires hardware support check |

---
*Updated dynamically following feature implementation, integration, and verification.*
