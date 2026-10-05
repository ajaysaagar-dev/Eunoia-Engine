# Project Instructions & Rules

## 1. Build Execution Policy
- **ALWAYS BUILD ONCE CODE IS DONE**: Always execute `Build.bat` after completing code modifications to verify successful compilation and ensure no compiler or linker errors exist.

## 2. Rendering & Engine Architecture
- **Preserve WickedEngine Rendering**: All graphics, shaders, post-processing, and renderer pipelines must remain powered natively by WickedEngine (`wi::renderer`, `wi::RenderPath3D`, `wi::graphics::GraphicsDevice`).
- **Do Not Break Shaders**: Custom UI shaders must never be loaded into WickedEngine's engine shader registry via `wi::renderer::LoadShader`. Direct creation or compilation through `wi::shadercompiler::Compile` / `device->CreateShader` must be used.
- **Maintain Eunoia Engine Styling**: Follow the UE5-inspired dark theme (`#2B2B2B` background, `#007ACC` accent blue, `#3E3E3E` borders) across Outliner, Details, Content Browser, Top Menu Bar, and Project Browser.

## 3. Communication Guidelines
- Use concise, direct, technical communication (Caveman style: minimal fluff, high density, drop unnecessary filler words).
