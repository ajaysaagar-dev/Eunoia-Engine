cmake_minimum_required(VERSION 3.15)

# Layer 2: EnginePlatform — windowing, input, OS abstraction (GLFW only)
add_library(EnginePlatform STATIC
    Src/Window.cpp
)

target_include_directories(EnginePlatform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    $<INSTALL_INTERFACE:Include>
    ${EUNOIA_GLFW_ROOT}/include
)

target_link_libraries(EnginePlatform PUBLIC
    EngineCore
)

target_link_directories(EnginePlatform PUBLIC
    ${EUNOIA_GLFW_ROOT}/lib-mingw-w64
)

target_link_libraries(EnginePlatform PUBLIC
    glfw3
)

target_compile_features(EnginePlatform PUBLIC cxx_std_20)
