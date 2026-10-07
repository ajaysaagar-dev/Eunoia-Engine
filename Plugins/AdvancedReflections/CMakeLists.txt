cmake_minimum_required(VERSION 3.15)
project(AdvancedReflections)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE REF_SOURCES
    "Source/*.cpp"
    "Include/AdvancedReflections/*.h"
)

add_library(AdvancedReflections STATIC ${REF_SOURCES})

target_include_directories(AdvancedReflections PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
