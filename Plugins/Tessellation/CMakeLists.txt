cmake_minimum_required(VERSION 3.15)
project(Tessellation)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE TESS_SOURCES
    "Source/*.cpp"
    "Include/Tessellation/*.h"
)

add_library(Tessellation STATIC ${TESS_SOURCES})

target_include_directories(Tessellation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
