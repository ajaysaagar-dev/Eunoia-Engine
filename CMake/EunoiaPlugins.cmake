# EunoiaPlugins.cmake — Discovers and declares core plugins statically

set(EUNOIA_CORE_PLUGIN_NAMES
    Post-Process-System
    Water
    Graphics
    Texture-Streaming
    UISystem
    Collisions
    Physics
    RayTrace
    SaveSystem
    SFLOD
    Animation-Graph
    Animation-Blending
    Level-Animation
    Character-Mechanics
    Node-Behaviour
)

# Common plugin includes
set(EUNOIA_PLUGIN_COMMON_INCLUDES
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_ROOT}/Engine/EnginePlatform/Include
    ${EUNOIA_ROOT}/Engine/EngineRHI/Include
    ${EUNOIA_ROOT}/Engine/EngineRenderer/Include
    ${EUNOIA_ROOT}/Engine/EngineScene/Include
    ${EUNOIA_ROOT}/Engine/EngineAssets/Include
    ${EUNOIA_ROOT}/Engine/EunoiaPluginCore/Include
    ${EUNOIA_ROOT}/Plugins/Post-Process-System/Include
    ${EUNOIA_ROOT}/Plugins/Water/Include
    ${EUNOIA_ROOT}/Plugins/Graphics/Include
    ${EUNOIA_ROOT}/Plugins/UISystem/Include
    ${EUNOIA_ROOT}/Plugins/Collisions/Include
    ${EUNOIA_ROOT}/Plugins/Physics/Include
    ${EUNOIA_ROOT}/Plugins/RayTrace/Include
    ${EUNOIA_ROOT}/Plugins/SaveSystem/Include
    ${EUNOIA_ROOT}/Plugins/Texture-Streaming/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/ThirdParty
    ${EUNOIA_ROOT}/Plugins/SFLOD/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Graph/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Blending/Include
    ${EUNOIA_ROOT}/Plugins/Level-Animation/Include
    ${EUNOIA_ROOT}/Plugins/Character-Mechanics/Include
    ${EUNOIA_ROOT}/EunoiaUI-Framework/Include
    ${EUNOIA_DEPS}/glm
    ${EUNOIA_DEPS}/json
    ${EUNOIA_DEPS}/imgui
    ${EUNOIA_DEPS}/imgui/backends
    ${EUNOIA_GLFW_ROOT}/include
)

set(EUNOIA_ALL_PLUGIN_TARGETS "")

foreach(PLUGIN_DIR_NAME ${EUNOIA_CORE_PLUGIN_NAMES})
    set(PLUGIN_ROOT "${EUNOIA_ROOT}/Plugins/${PLUGIN_DIR_NAME}")
    if(EXISTS "${PLUGIN_ROOT}")
        file(GLOB_RECURSE P_SRCS "${PLUGIN_ROOT}/Source/*.cpp" "${PLUGIN_ROOT}/ThirdParty/*.cpp")
        if(P_SRCS)
            string(REPLACE "-" "_" TARGET_NAME "plugin_${PLUGIN_DIR_NAME}")
            add_library(${TARGET_NAME} STATIC ${P_SRCS})
            target_include_directories(${TARGET_NAME} PUBLIC
                "${PLUGIN_ROOT}/Include"
                "${PLUGIN_ROOT}/ThirdParty"
                ${EUNOIA_PLUGIN_COMMON_INCLUDES}
            )
            target_link_libraries(${TARGET_NAME} PUBLIC
                EngineScene
                EngineRenderer
                EngineAssets
                EngineRHI
                EnginePlatform
                EngineCore
                EunoiaPluginCore
                imgui_vendor
                ufbx_vendor
                meshoptimizer_vendor
                directxtex_vendor
            )
            target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)
            list(APPEND EUNOIA_ALL_PLUGIN_TARGETS ${TARGET_NAME})
        endif()
    endif()
endforeach()
