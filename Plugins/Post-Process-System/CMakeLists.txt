cmake_minimum_required(VERSION 3.15)
project(PostProcessSystem)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE PP_SOURCES
    "Source/*.cpp"
    "Include/PostProcess/*.h"
)

add_library(PostProcessSystem STATIC ${PP_SOURCES})

target_include_directories(PostProcessSystem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
