# Eunoia-Engine Architecture & Design Principles

## Overview
Eunoia Engine is a modular, high-performance C++20 game engine and real-time level editor targeting Windows and DirectX 12.

The engine architecture is structured into strict, unidirectional layers. Each layer may only depend on modules within its layer or layers beneath it, completely preventing circular dependencies and ensuring decoupled, maintainable code.

---

## Architectural Layers

```
Layer 7: Editor (Editor/Editor) & Standalone Game Runtime (Runtime)
   │
   ▼
Layer 6: EngineScene (ECS, Scene Graph, Behaviours, Serialization)
   │
   ▼
Layer 5: EngineRenderer (Render Passes, Lighting, Cameras, Shaders, PSOs)
   │
   ▼
Layer 4: EngineAssets (Asset Registry, Loaders, Textures, Meshes, Procedural Geometry)
   │
   ▼
Layer 3: EngineRHI (DirectX 12 Abstraction, Device, Command Queues, SwapChain, Descriptors)
   │
   ▼
Layer 2: EnginePlatform (Window Management, Input System via GLFW)
   │
   ▼
Layer 1: EngineCore (Job System, Thread Pool, Memory Utilities, Logging, Time - STL only)
```

### Layer Details

#### Layer 1: `EngineCore`
- **Dependencies**: STL only.
- **Responsibilities**:
  - High-performance worker thread pool and task graph `JobSystem`.
  - Scoped profiling timers, assertions, and logging utilities.
  - Core data types, math helpers, and filesystem primitives.

#### Layer 2: `EnginePlatform`
- **Dependencies**: `EngineCore`.
- **Responsibilities**:
  - Window creation, event loop, and DPI handling via GLFW.
  - Low-level keyboard, mouse, and controller input handling (`InputSystem`).

#### Layer 3: `EngineRHI`
- **Dependencies**: `EngineCore`, `EnginePlatform`.
- **Responsibilities**:
  - Low-level DirectX 12 graphics device abstraction (`ID3D12Device5`).
  - Command queue, command allocator ring buffers, and frame synchronization fences.
  - Descriptor heaps (RTV, DSV, CBV/SRV/UAV) and linear upload buffer allocators.
  - Safe GPU synchronization and frame retire queues.

#### Layer 4: `EngineAssets`
- **Dependencies**: `EngineRHI`, `EnginePlatform`, `EngineCore`.
- **Responsibilities**:
  - Metadata tracking and unified GUID-based `AssetRegistry`.
  - Texture decoding (via `stb_image` and `DirectXTex`), procedural mesh generators.
  - Model file import (via `ufbx`) with import-time vertex deduplication and index clamping.
  - Source assets live directly in `Projects/<Project>/Content/` as full-size files.

#### Layer 5: `EngineRenderer`
- **Dependencies**: `EngineAssets`, `EngineRHI`, `EngineCore`.
- **Responsibilities**:
  - Forward+ and clustered lighting passes, cascaded shadow maps.
  - Post-processing, skybox rendering, viewport grid, and transform gizmo rendering.
  - GPU instancing and frustum culling.
  - Shader management and PSO pipeline library caching.

#### Layer 6: `EngineScene`
- **Dependencies**: `EngineRenderer`, `EngineAssets`, `EnginePlatform`, `EngineCore`.
- **Responsibilities**:
  - Hierarchical scene graph with dirty-flag world matrix cache propagation.
  - Entity-Component architecture and GameObject state.
  - JSON-based scene serialization (`SceneSerializer`).
  - C++ Behaviour scripts (`EunoiaBehaviour`, `BehaviourRegistry`).

#### Layer 7: `Editor` / `Runtime`
- **Dependencies**: All underlying layers + Dear ImGui / ImGuizmo (Editor only).
- **Responsibilities**:
  - **Editor**: Modular docking layout (Outliner, Details, Viewport, Content Browser, Material Editor, Log, Settings).
  - **Runtime**: Lightweight standalone player executable without editor or ImGui overhead.

---

## Plugin Architecture (`EunoiaPluginCore`)
Plugins (e.g. `Animation-Blending`, `Animation-Graph`, `UISystem`, `SaveSystem`) reside in `Plugins/` and communicate with the engine via service interfaces registered in `EunoiaPluginCore`. This preserves modularity and allows optional plugins to be compiled statically or dynamically without polluting core engine headers.
