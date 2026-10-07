cmake_minimum_required(VERSION 3.15)
project(ScreenSpaceEffects)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SSE_SOURCES
    "Source/*.cpp"
    "Include/ScreenSpaceEffects/*.h"
)

add_library(ScreenSpaceEffects STATIC ${SSE_SOURCES})

target_include_directories(ScreenSpaceEffects PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
