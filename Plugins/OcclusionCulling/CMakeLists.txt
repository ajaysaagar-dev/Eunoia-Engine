cmake_minimum_required(VERSION 3.15)
project(OcclusionCulling)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE OCC_SOURCES
    "Source/*.cpp"
    "Include/OcclusionCulling/*.h"
)

add_library(OcclusionCulling STATIC ${OCC_SOURCES})

target_include_directories(OcclusionCulling PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
