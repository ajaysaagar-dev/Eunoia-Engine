# Eunoia Engine Development Guidelines & Directives

## 1. Asset Storage & Content Management
- **No Content Cooking**: The "cooking content" / 0-byte dummy reference system is permanently removed.
- **Direct Content Usage**: All assets (textures, materials, models, levels, sounds, UI) must always remain directly in the project's `Content/` folder as real, full-sized files.
- Never delete, truncate to 0 bytes, or replace files in `Content/` with dummy references.

## 2. Git & Version Control Policy
- **Dual Repository Architecture**:
  1. **Engine Source Repository (`Eunoia-Engine`)**:
     - Remote: `https://github.com/ajaysaagar-dev/Eunoia-Engine.git`
     - Local Path: `C:\Projects\Eunoia-Engine\Eunoia-Engine`
     - Workflow: After each code change/feature, create multiple granular functional commits divided logically by functional area (e.g., Editor/UI, Asset Management, Renderer/Shaders, Scene & Core, Build System/Tools), then push all commits together to `origin/main`.
  2. **Engine Build Repository (`Eunoia-Engine-Build`)**:
     - Remote: `https://github.com/ajaysaagar-dev/Eunoia-Engine-Build.git`
     - Local Path: `C:\Projects\Eunoia-Engine\Eunoia-Engine-Build`
     - Workflow: Once the engine is built via `Build.bat`, update `Eunoia-Editor.exe` and distribution files in this repository, commit with build details, and push to `origin/main`.
- **GitHub Authentication**:
  - Remote Base: `https://github.com/ajaysaagar-dev/`
  - Credentials are read from the environment/credential manager; never stored in the repo.

## 3. Strict Project Constraints
- **Preserve Player Script**: Under NO circumstances modify `Projects/GameTest/Content/Behaviours/BS_Player.cpp`.
- Maintain code cleanliness, documentation, and clickable `file://` links in user communications.
