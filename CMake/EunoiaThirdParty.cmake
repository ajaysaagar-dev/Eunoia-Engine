# EunoiaThirdParty.cmake — Separate static libraries for vendored dependencies

# 1. ImGui + ImGuizmo + Backends
file(GLOB IMGUI_SOURCES
    "${EUNOIA_DEPS}/imgui/imgui.cpp"
    "${EUNOIA_DEPS}/imgui/imgui_draw.cpp"
    "${EUNOIA_DEPS}/imgui/imgui_tables.cpp"
    "${EUNOIA_DEPS}/imgui/imgui_widgets.cpp"
    "${EUNOIA_DEPS}/imgui/backends/imgui_impl_glfw.cpp"
    "${EUNOIA_DEPS}/imgui/backends/imgui_impl_dx12.cpp"
    "${EUNOIA_DEPS}/imgui/backends/imgui_impl_opengl3.cpp"
    "${EUNOIA_DEPS}/imgui/ImGuizmo.cpp"
)
add_library(imgui_vendor STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_vendor PUBLIC
    ${EUNOIA_DEPS}/imgui
    ${EUNOIA_DEPS}/imgui/backends
    ${EUNOIA_GLFW_ROOT}/include
)
target_compile_features(imgui_vendor PUBLIC cxx_std_20)
set_target_properties(imgui_vendor PROPERTIES UNITY_BUILD OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imgui_vendor PRIVATE -w)
endif()

# 2. ufbx
add_library(ufbx_vendor STATIC "${EUNOIA_DEPS}/ufbx/ufbx.c")
target_include_directories(ufbx_vendor PUBLIC "${EUNOIA_DEPS}/ufbx")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ufbx_vendor PRIVATE -w)
endif()

# 3. meshoptimizer
add_library(meshoptimizer_vendor STATIC "${EUNOIA_DEPS}/meshoptimizer/meshoptimizer_unity.cpp")
target_include_directories(meshoptimizer_vendor PUBLIC "${EUNOIA_DEPS}/meshoptimizer")
target_compile_features(meshoptimizer_vendor PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(meshoptimizer_vendor PRIVATE -w)
endif()

# 4. DirectXTex
add_library(directxtex_vendor STATIC
    "${EUNOIA_ROOT}/Engine/EngineAssets/Src/DirectXTex.cpp"
    "${EUNOIA_ROOT}/Engine/EngineAssets/Src/DirectXTexD3D12.cpp"
)
target_include_directories(directxtex_vendor PUBLIC
    "${EUNOIA_ROOT}/Engine/EngineAssets/Include"
    "${EUNOIA_DEPS}/glm"
)
target_compile_features(directxtex_vendor PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(directxtex_vendor PRIVATE -w)
endif()
