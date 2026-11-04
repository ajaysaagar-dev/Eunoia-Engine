# Wicked Engine Reference Analysis & Architectural Mapping

## Executive Overview
This document provides a comprehensive technical mapping between **Wicked Engine** (the reference engine located at `READ-ONLY/WickedEngine/`) and **Eunoia Engine** (the target engine located at `Eunoia-Engine/`). It details the graphics architecture, GPU resource management, rendering pipeline, viewport implementation, shader systems, and graphics options for both engines.

---

## 1. Core Architecture Comparison

| Architectural Aspect | Wicked Engine Reference (`READ-ONLY/WickedEngine/`) | Eunoia Engine Target (`Eunoia-Engine/`) | Status & Architectural Delta |
| :--- | :--- | :--- | :--- |
| **Graphics API Backend** | Multi-backend abstraction (`wiGraphicsDevice.h`): Direct3D 12 (`wiGraphicsDevice_DX12.h`), Vulkan, Metal. Uses `ID3D12Device5` for DXR 1.1 / Ray Tracing / Mesh Shaders / VRS. | D3D12 raw in Editor (`Editor/Editor/Src/Main.cpp`) + thin abstraction in `EngineRHI` (`Device.h`, `CommandContext.h`, `DescriptorAllocator.h`, `PipelineState.h`, `SwapChain.h`, `UploadHeap.h`). Device creates `ID3D12Device`. | **ANALYZED**. Eunoia uses raw D3D12 with unified descriptor heaps and explicit command list recordings. RHI is present but Editor bypasses it for direct D3D12 control in `Main.cpp`. |
| **Render Path & Target Management** | Multi-pass HDR pipeline (`wiRenderPath3D.h`, `wiRenderer.cpp`). Renders scene to `R11G11B10_FLOAT` or `R16G16B16A16_FLOAT` HDR target, runs compute post-processing passes, resolves tonemapping to swapchain `R8G8B8A8_UNORM`. | Forward+ shading in `Main.cpp` pixel shader (`PSMain`), directly writing to swapchain render target or intermediate textures. MSAA / depth-prepass supported. | **ANALYZED / PARTIALLY IMPLEMENTED**. Eunoia needs dedicated intermediate HDR buffer (`R16G16B16A16_FLOAT`) to cleanly isolate post-process compute passes (Bloom, SSAO, DoF, TAA, Motion Blur, Tonemap) from the rasterization pass. |
| **Shader Compilation & Cache** | DXC / FXC compilation (`wiShaderCompiler.h`), offline tool `OfflineShaderCompiler.exe`, runtime caching in binary blobs. Compute shaders used for 90% of post-processing. | Embedded HLSL strings in `Main.cpp` compiled at startup via `D3DCompile` (`d3dcompiler.h` / FXC). Binary bytecodes cached in `Shaders/Cache/*.cso` keyed by hash. Standalone shaders in `Shaders/*.hlsl`. | **ANALYZED / INTEGRATED**. Caching mechanism is identical in concept. Compute shader passes can be compiled and cached into `Shaders/Cache/` using the existing `CompileOrLoadCachedShader` helper. |
| **Constant Buffer Structure** | Structured `FrameCB` / `ShaderScene` defined in `shaders/ShaderInterop_Renderer.h`. Bindless resource indexing via root parameters and global SRV/UAV heaps. | 256-byte aligned `SceneConstantBuffer` (`cbuffer FrameConstants : register(b0)`) and `MaterialConstants` (`register(b1)`), with up to 64 dynamic point/spot lights and full graphics settings block. | **ANALYZED / IMPLEMENTED**. `SceneConstantBuffer` in Eunoia already incorporates the full 75+ WickedEngine graphics settings fields and light parameters. |
| **Scene & Entity System** | Entity-Component-System (`wiECS.h`, `wiScene.h`, `wiScene_Components.h`). Transforms, materials, meshes, lights, weather, probes as components. | GameObject hierarchy (`EngineScene/GameObject.h`, `Scene.h`). Integrated components: Transform, Mesh, Material, LevelAnimation, Audio, Collision, SFLOD, Behaviours. | **ANALYZED**. Eunoia's `Scene` class acts as the centralized scene container with serialization support (`SceneSerializer.h`). |

---

## 2. Graphics Settings & Viewport Feature Mapping

### Category A: Rendering Quality & Anti-Aliasing
| Feature | Wicked Engine Source | Eunoia Engine Source | Status | Missing Functionality & Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Render Resolution & Scale** | `wiRenderPath3D.cpp`, `GraphicsWindow.cpp` (`resolutionScaleSlider`) | `Scene.h` (`resolutionScale`), `Main.cpp` (`g_currentResolutionScale`) | **IMPLEMENTED** | Viewport scales dynamically. Resizes viewport render targets when scaled below 1.0. |
| **Texture Quality & Mip Bias** | `wiRenderer::ModifyObjectSampler`, `GraphicsWindow.cpp` (`mipLodBiasSlider`) | `Scene.h` (`textureQuality`, `mipLodBias`), `Main.cpp` (`g_samplerDesc.MipLODBias`) | **IMPLEMENTED** | Sampler desc configured with `MipLODBias` and anisotropic filters. |
| **Anisotropic Filtering** | `wiGraphicsDevice_DX12.cpp` (`D3D12_FILTER_ANISOTROPIC`) | `Main.cpp` (`D3D12_ENCODE_BASIC_FILTER` / `ANISOTROPIC`) | **IMPLEMENTED** | Default anisotropic sampler at `s0`. |
| **MSAA** | `wiRenderPath3D.cpp` (`msaaSampleCount`) | `Scene.h` (`msaaQuality`), `Main.cpp` | **PARTIALLY IMPLEMENTED** | MSAA supported for forward geometry; requires MSAA depth resolve for post-processing. |
| **TAA (Temporal Anti-Aliasing)** | `shaders/temporalaaCS.hlsl`, `wiRenderer::Postprocess_TemporalAA` | `Scene.h` (`enableTAA`, `temporalAADebug`), `Shaders/AntiAliasing.hlsl` | **NOT IMPLEMENTED** | Halton subpixel projection jittering + velocity buffer + history reprojection ping-pong pass needed. |
| **FXAA** | `wiRenderer::Postprocess_FXAA`, `shaders/fxaaCS.hlsl` | `Scene.h` (`enableFXAA`), `Shaders/AntiAliasing.hlsl` | **NOT IMPLEMENTED** | Edge detection and blending compute pass needed. |
| **VSync & Frame Limiting** | `wiGraphicsDevice_DX12::Present` (`sync_interval`), `wiApplication` | `Scene.h` (`enableVSync`, `frameRateLimit`), `Main.cpp` (`swapChain->Present(1, 0)`) | **IMPLEMENTED** | `sync_interval` passed directly to `IDXGISwapChain::Present`. |

---

### Category B: Shadows & Lighting
| Feature | Wicked Engine Source | Eunoia Engine Source | Status | Missing Functionality & Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Cascaded Shadow Maps (CSM)** | `wiRenderer::DrawShadowmaps`, `shaders/shadowPS.hlsl` | `Main.cpp` (`RenderShadowPass`, `g_cascadeSplits`), `Scene.h` | **IMPLEMENTED** | 4 cascades, smooth blend zones, logarithmic-linear split schemes matching WickedEngine. |
| **Shadow Filtering & Bias** | `shaders/ShaderInterop_Renderer.h`, normal offset bias | `Main.cpp` (`shadowBias`, slope-scaled normal bias, PCF kernel) | **IMPLEMENTED** | 16-tap Poisson disc and PCF filtering matching WickedEngine. |
| **Point Light Shadow Cubemaps** | `wiRenderer.cpp` (Cube shadow atlas), `shaders/shadowPS.hlsl` | `Main.cpp` (`TextureCubeArray g_pointShadowMap`), `Scene.h` | **IMPLEMENTED** | Omni-directional cube shadow rendering for active point lights. |
| **Spot Light Shadow Maps** | `wiRenderer.cpp`, 2D shadow atlas | `Main.cpp` (`Texture2DArray g_spotShadowMap`), `Scene.h` | **IMPLEMENTED** | Perspective frustum shadow maps for up to 4 shadow-casting spot lights. |
| **Screen Space Shadows** | `shaders/screenspaceshadowCS.hlsl`, `wiRenderer::Postprocess_ScreenSpaceShadow` | `Scene.h` (`enableScreenSpaceShadows`, `screenSpaceShadowRange`, `screenSpaceShadowSamples`), `Main.cpp` | **PARTIALLY IMPLEMENTED** | Raymarching contact shadow pass integrated into forward lighting. |
| **Capsule Shadows** | `shaders/capsuleshadowCS.hlsl`, `wiRenderer.cpp` | `Scene.h` (`enableCapsuleShadows`, `capsuleShadowAngle`, `capsuleShadowFade`), `Main.cpp` | **PARTIALLY IMPLEMENTED** | Analytical capsule occlusion approximation in shader. |
| **Directional, Point, Spot, Area Lights** | `wiRenderer.cpp`, `wiScene_Components.h` (`LightComponent`) | `Main.cpp` (`SceneConstantBuffer`, 64 point/spot lights), `Scene.h` | **IMPLEMENTED** | Attenuation falloff, cone angles, rectangular/disc light approximations implemented. |
| **Surfel GI / DDGI / VXGI** | `shaders/surfel_*.hlsl`, `shaders/ddgi_*.hlsl`, `shaders/vxgi_*.hlsl` | `Scene.h` (`enableSurfelGI`, `enableDDGI`, `enableVXGI`, `giBoost`), `Main.cpp` | **PARTIALLY IMPLEMENTED** | `giBoost` and debug modes wired; full ray-traced surfel caches and voxel grids require DX12 raytracing or 3D voxelization pipeline. |

---

### Category C: Ambient Effects & Post-Processing
| Feature | Wicked Engine Source | Eunoia Engine Source | Status | Missing Functionality & Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Ambient Occlusion (SSAO/HBAO/MSAO)** | `shaders/ssaoCS.hlsl`, `shaders/hbaoCS.hlsl`, `shaders/msaoCS.hlsl` | `Scene.h` (`enableAO`, `aoType`, `aoPower`, `aoRange`, `aoSampleCount`), `Shaders/AmbientOcclusion.hlsl`, `Main.cpp` | **PARTIALLY IMPLEMENTED** | Inline shader implements SSAO sampling; standalone compute pass with bilateral blur is ready in `Shaders/AmbientOcclusion.hlsl`. |
| **Screen Space Reflections (SSR)** | `shaders/ssr_raytraceCS.hlsl`, `shaders/ssr_resolveCS.hlsl` | `Scene.h` (`enableSSR`, `ssrQuality`, `reflectionRoughnessCutoff`), `Main.cpp` | **NOT IMPLEMENTED** | Screen space depth-marching / Hi-Z traversal for planar and rough specular reflections. |
| **Volumetric Lighting & Fog** | `shaders/volumetriclight_*.hlsl`, `shaders/fog.hlsli` | `Scene.h` (`enableFog`, `fogStart`, `fogDensity`, `fogColor`), `Main.cpp` | **IMPLEMENTED** | Height-falloff exponential atmospheric fog and ray-marched sun shafts matching WickedEngine. |
| **Bloom** | `shaders/bloomseparateCS.hlsl`, `shaders/blur_gaussian_float4CS.hlsl` | `Scene.h` (`enableBloom`, `bloomThreshold`, `bloomStrength`), `Main.cpp` | **PARTIALLY IMPLEMENTED** | Luminance extraction and bloom blur implemented in shader post-process chain. |
| **Depth of Field (DoF)** | `shaders/depthoffield_*.hlsl` | `Scene.h` (`enableDoF`, `dofStrength`, `dofFocusDistance`), `Main.cpp` | **NOT IMPLEMENTED** | Circle-of-Confusion (CoC) calculation and bokeh blur pass. |
| **Motion Blur** | `shaders/motionblur_*.hlsl` | `Scene.h` (`enableMotionBlur`, `motionBlurStrength`), `Main.cpp` | **NOT IMPLEMENTED** | Velocity buffer generation from per-object previous world matrices + post-process velocity blur. |
| **Tone Mapping & Exposure** | `shaders/tonemapCS.hlsl`, `shaders/tonemap.hlsli` | `Scene.h` (`postTonemapper`, `postExposureEV`), `Main.cpp` | **IMPLEMENTED** | Reinhard, ACES Filmic, and Linear tonemapping implemented with exposure and color controls. |
| **Eye Adaptation / Auto Exposure** | `shaders/luminance_*.hlsl` | `Scene.h` (`enableEyeAdaptation`, `eyeAdaptionKey`, `eyeAdaptionRate`), `Main.cpp` | **NOT IMPLEMENTED** | Parallel luminance reduction / histogram compute shader. |
| **Cel Outline & Chromatic Aberration**| `shaders/outlineCS.hlsl`, `shaders/chromatic_aberrationCS.hlsl` | `Scene.h` (`enableOutline`, `enableChromaticAberration`), `Main.cpp` | **IMPLEMENTED** | Depth/normal edge detection outline and radial chromatic aberration filter. |
| **CRT & Sharpen Filters (CAS)** | `shaders/crt_screenCS.hlsl`, `shaders/sharpenCS.hlsl` | `Scene.h` (`enableCRTFilter`, `enableSharpenFilter`), `Main.cpp` | **IMPLEMENTED** | Scanline / curvature CRT filter and contrast-adaptive sharpening (CAS). |

---

### Category D: Viewport Optimizations & Debug Tools
| Feature | Wicked Engine Source | Eunoia Engine Source | Status | Missing Functionality & Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Freeze Culling Camera** | `wiRenderer::SetFreezeCullingCameraEnabled`, `GraphicsWindow.cpp` | `Scene.h` (`freezeCullingCamera`), `Main.cpp` | **IMPLEMENTED** | Freezes frustum planes for culling so camera can detach and view culling behavior. |
| **Debug Light Culling** | `wiRenderer::SetDebugLightCulling`, `shaders/lightCullingCS.hlsl` | `Scene.h` (`debugLightCulling`), `Main.cpp` | **IMPLEMENTED** | 16x16 screen tile light count heatmap visualization. |
| **Disable Albedo Maps (Clay Mode)** | `wiRenderer::SetDisableAlbedoMaps` | `Scene.h` (`disableAlbedoMaps`), `Main.cpp` | **IMPLEMENTED** | Forces uniform neutral 85% reflectance albedo for lighting inspection. |
| **Force Diffuse Lighting** | `wiRenderer::SetForceDiffuseLighting` | `Scene.h` (`forceDiffuseLighting`), `Main.cpp` | **IMPLEMENTED** | Zeroes specular BRDF and ambient specular reflections. |
| **Force Unlit** | `wiRenderer::SetForceUnlit` | `Scene.h` (`forceUnlit`), `Main.cpp` | **IMPLEMENTED** | Bypasses all lighting equations, returning direct diffuse albedo. |
| **Shadow LOD Override** | `GraphicsWindow.cpp` (`shadowLODCheckBox`) | `Scene.h` (`shadowLODOverride`), `Main.cpp` | **IMPLEMENTED** | Skips shadow map caster draws for distant objects. |
| **Game Speed Simulation Multiplier**| `wiRenderer::SetGameSpeed` | `Scene.h` (`gameSpeed`), `Main.cpp` | **IMPLEMENTED** | Scales delta time for physics, skeletal animations, water simulations. |
| **Occlusion Culling / Hi-Z** | `shaders/depth_pyramidCS.hlsl`, `wiRenderer.cpp` | `Scene.h` (`occlusionCullingEnabled`), `Shaders/HiZBuild.hlsl` | **PARTIALLY IMPLEMENTED** | HiZ compute shader exists; needs bounding box testing pass in compute. |
| **Variable Rate Shading (VRS)** | `shaders/shadingrateclassificationCS.hlsl` | `Scene.h` (`enableVRS`, `debugVRS`), `Main.cpp` | **ANALYZED / BLOCKED** | Requires D3D12 VRS Tier 1/2 hardware capability check. |
| **Mesh Shaders** | `wiGraphicsDevice_DX12.cpp` (`CreateMeshShader`) | `Scene.h` (`enableMeshShaders`), `Main.cpp` | **ANALYZED / BLOCKED** | Requires Shader Model 6.5+ and D3D12_FEATURE_DATA_D3D12_OPTIONS7. |

---

## 3. Required Architectural Enhancements in Eunoia Engine

To achieve full 100% graphics parity with Wicked Engine:
1. **HDR Ping-Pong Surface Architecture**:
   - Provide an intermediate `R16G16B16A16_FLOAT` render target + ping-pong texture.
   - Forward pass writes HDR color + depth.
   - Post-process compute passes process the HDR surface sequentially.
   - Final pass writes tonemapped, sRGB-corrected output to swapchain.
2. **Velocity Buffer Generation**:
   - Store previous frame's `MVP` matrix per object or render a depth/velocity prepass.
   - Unlocks TAA, Motion Blur, and Temporal SSR.
3. **Dedicated Post-Processing Compute Pipeline**:
   - Compile individual compute shaders from `Shaders/*.hlsl` with specific root signatures (`RootSignature` with descriptor tables for SRV/UAV).
   - Dispatch passes sequentially before final blit.

---
*Generated in accordance with Eunoia Engine Development Guidelines & dev.md directives.*
