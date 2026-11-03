cmake_minimum_required(VERSION 3.15)
project(Eunoia LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ──────────────────────────────────────────────────────────────────────────────
# Shared paths and dependencies
# ──────────────────────────────────────────────────────────────────────────────
set(EUNOIA_ROOT        ${CMAKE_CURRENT_SOURCE_DIR})
set(EUNOIA_DEPS        ${EUNOIA_ROOT}/deps)
set(EUNOIA_GLFW_ROOT   ${EUNOIA_DEPS}/glfw-3.5.1.bin.WIN64)

# ──────────────────────────────────────────────────────────────────────────────
# Vendored Third-Party Targets (Compiled as isolated static libraries)
# ──────────────────────────────────────────────────────────────────────────────
include(CMake/EunoiaThirdParty.cmake)

# ──────────────────────────────────────────────────────────────────────────────
# Strict Layered Engine Modules (Layer 1 -> Layer 6)
# ──────────────────────────────────────────────────────────────────────────────
add_subdirectory(Engine/EngineCore)
add_subdirectory(Engine/EnginePlatform)
add_subdirectory(Engine/EngineRHI)
add_subdirectory(Engine/EngineAssets)
add_subdirectory(Engine/EngineRenderer)
add_subdirectory(Engine/EngineScene)
add_subdirectory(Engine/EunoiaPluginCore)

# Optional standalone UI Framework
if(EXISTS "${EUNOIA_ROOT}/EunoiaUI-Framework/CMakeLists.txt")
    add_subdirectory(EunoiaUI-Framework)
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Core Plugins
# ──────────────────────────────────────────────────────────────────────────────
include(CMake/EunoiaPlugins.cmake)

# ──────────────────────────────────────────────────────────────────────────────
# Application Targets
# ──────────────────────────────────────────────────────────────────────────────
add_subdirectory(Editor/Editor)
add_subdirectory(Runtime)

# ──────────────────────────────────────────────────────────────────────────────
# Test Targets (optional)
# ──────────────────────────────────────────────────────────────────────────────
option(EUNOIA_BUILD_TESTS "Build unit tests" ON)
if(EUNOIA_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
