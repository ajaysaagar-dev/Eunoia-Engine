# Eunoia-Engine Performance & Build Timing Log

All measurements are taken on the local hardware:
- OS: Windows 11 (64-bit)
- CPU: 20 Logical Threads
- Compiler: GCC / MinGW-w64 (w64devkit v2.10.0) + ccache
- Graphics: DirectX 12 (D3D12)

---

## 1. Build Times (Benchmark Scenarios)

| Phase / Scenario | Clean Build | Warm ccache Build | Edit 1 Engine .cpp | Edit 1 Editor .cpp | Edit Scene.h | Notes |
|:---|:---:|:---:|:---:|:---:|:---:|:---|
| **Baseline (Before Overhaul)** | 48.2s | 15.4s | 8.3s | 11.2s | 34.6s | .NET EunoiaBuildTool + g++ |
| **Phase 2 (CMake + Ninja + ccache + PCH)** | 45.1s | 31.1s | 2.7s | 7.1s | 28.8s | 5/5 Unit Tests Passing; Target edit-cpp < 5s achieved (2.7s) |
| **Phase 3 (Split Monoliths)** | *TBD* | *TBD* | *TBD* | *TBD* | *TBD* | Main.cpp & EngineUI split |

---

## 2. Runtime Frame Performance (Benchmark Scene)

| Phase / Milestone | CPU Frame Time (ms) | GPU Frame Time (ms) | Total FPS | Draw Calls | Notes |
|:---|:---:|:---:|:---:|:---:|:---|
| **Baseline (1k Objects)** | ~6.5 ms | ~5.8 ms | ~120 FPS | 1000 | Full rebuild per frame |
| **Phase 4 (Cached Transforms & Instancing)** | *TBD* | *TBD* | *TBD* | *TBD* | Instanced batches |
| **Phase 4.2 (Hi-Z & Frustum Culling)** | *TBD* | *TBD* | *TBD* | *TBD* | Indirect execution |
