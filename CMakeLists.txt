cmake_minimum_required(VERSION 3.20)

project(Strova VERSION 1.0.5 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_SUPPRESS_REGENERATION ON)

if (NOT WIN32)
    message(FATAL_ERROR "This Strova build file is configured for Windows only.")
endif()

if (MSVC)
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "Win32/x86 is not supported. Configure with x64.")
    endif()

    add_compile_options(/nologo /utf-8)
    add_link_options(/nologo)
endif()

# --------------------------------------------------
# Root paths
# --------------------------------------------------
set(STROVA_ROOT "${CMAKE_SOURCE_DIR}")
set(STROVA_SRC_DIR "${STROVA_ROOT}/Strova")
set(STROVA_DEPS_DIR "${STROVA_ROOT}/build-dependencies")

set(SDL2_ROOT       "${STROVA_DEPS_DIR}/SDL2-2.32.10")
set(SDL2_IMAGE_ROOT "${STROVA_DEPS_DIR}/SDL2_image-2.8.6")
set(SDL2_TTF_ROOT   "${STROVA_DEPS_DIR}/SDL3_ttf-devel-3.2.2-VC/SDL3_ttf-3.2.2")

foreach(req_dir
    "${STROVA_SRC_DIR}"
    "${SDL2_ROOT}"
    "${SDL2_IMAGE_ROOT}"
    "${SDL2_TTF_ROOT}"
)
    if (NOT EXISTS "${req_dir}")
        message(FATAL_ERROR "Missing required directory: ${req_dir}")
    endif()
endforeach()

# --------------------------------------------------
# Includes
# --------------------------------------------------
set(SDL2_INCLUDE_DIR       "${SDL2_ROOT}/include")
set(SDL2_IMAGE_INCLUDE_DIR "${SDL2_IMAGE_ROOT}/include")
set(SDL2_TTF_INCLUDE_DIR   "${SDL2_TTF_ROOT}/include")

# --------------------------------------------------
# Libraries / DLLs
# --------------------------------------------------
set(SDL2_LIB       "${SDL2_ROOT}/lib/x64/SDL2.lib")
set(SDL2MAIN_LIB   "${SDL2_ROOT}/lib/x64/SDL2main.lib")
set(SDL2_DLL       "${SDL2_ROOT}/lib/x64/SDL2.dll")

set(SDL2_IMAGE_LIB "${SDL2_IMAGE_ROOT}/lib/x64/SDL2_image.lib")
set(SDL2_IMAGE_DLL "${SDL2_IMAGE_ROOT}/lib/x64/SDL2_image.dll")

set(SDL2_TTF_LIB   "${SDL2_TTF_ROOT}/lib/x64/SDL2_ttf.lib")
set(SDL2_TTF_DLL   "${SDL2_TTF_ROOT}/lib/x64/SDL2_ttf.dll")

set(SDL2_IMAGE_OPTIONAL_DLLS
    "${SDL2_IMAGE_ROOT}/lib/x64/optional/libavif-16.dll"
    "${SDL2_IMAGE_ROOT}/lib/x64/optional/libtiff-5.dll"
    "${SDL2_IMAGE_ROOT}/lib/x64/optional/libwebp-7.dll"
    "${SDL2_IMAGE_ROOT}/lib/x64/optional/libwebpdemux-2.dll"
)

foreach(req_file
    "${SDL2_LIB}"
    "${SDL2MAIN_LIB}"
    "${SDL2_DLL}"
    "${SDL2_IMAGE_LIB}"
    "${SDL2_IMAGE_DLL}"
    "${SDL2_TTF_LIB}"
    "${SDL2_TTF_DLL}"
)
    if (NOT EXISTS "${req_file}")
        message(FATAL_ERROR "Missing required dependency file: ${req_file}")
    endif()
endforeach()

# --------------------------------------------------
# Native File Dialog (Windows-only source build)
# --------------------------------------------------
set(NFD_ROOT        "${STROVA_SRC_DIR}/third_party/nfd")
set(NFD_SRC_DIR     "${NFD_ROOT}/src")
set(NFD_INCLUDE_DIR "${NFD_SRC_DIR}/include")
set(NFD_WIN_SOURCE  "${NFD_SRC_DIR}/nfd_win.cpp")

if (NOT EXISTS "${NFD_INCLUDE_DIR}/nfd.h")
    message(FATAL_ERROR "Missing NFD header: ${NFD_INCLUDE_DIR}/nfd.h")
endif()

if (NOT EXISTS "${NFD_WIN_SOURCE}")
    message(FATAL_ERROR "Missing NFD Windows backend source: ${NFD_WIN_SOURCE}")
endif()

add_library(strova_nfd STATIC
    "${NFD_WIN_SOURCE}"
)

target_include_directories(strova_nfd
    PUBLIC
        "${NFD_INCLUDE_DIR}"
)

target_compile_definitions(strova_nfd
    PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(strova_nfd
    PRIVATE
        ole32
        uuid
        shell32
)

# --------------------------------------------------
# Source discovery
# --------------------------------------------------
file(GLOB_RECURSE STROVA_SOURCES CONFIGURE_DEPENDS
    "${STROVA_SRC_DIR}/*.c"
    "${STROVA_SRC_DIR}/*.cc"
    "${STROVA_SRC_DIR}/*.cpp"
    "${STROVA_SRC_DIR}/*.cxx"
    "${STROVA_SRC_DIR}/*.rc"
)

file(GLOB_RECURSE STROVA_HEADERS CONFIGURE_DEPENDS
    "${STROVA_SRC_DIR}/*.h"
    "${STROVA_SRC_DIR}/*.hpp"
    "${STROVA_SRC_DIR}/*.hh"
    "${STROVA_SRC_DIR}/*.hxx"
)

# Exclude third-party NFD sources from the main app glob.
# Build only the Windows backend above as strova_nfd.
list(FILTER STROVA_SOURCES EXCLUDE REGEX ".*[\\\\/]third_party[\\\\/]nfd[\\\\/].*")
list(FILTER STROVA_HEADERS EXCLUDE REGEX ".*[\\\\/]third_party[\\\\/]nfd[\\\\/].*")

# Filter out backup / junk files
list(FILTER STROVA_SOURCES EXCLUDE REGEX ".*(\\.bak\\.c|\\.bak\\.cc|\\.bak\\.cpp|\\.bak\\.cxx|\\.old\\.c|\\.old\\.cc|\\.old\\.cpp|\\.old\\.cxx|_bak\\.c|_bak\\.cc|_bak\\.cpp|_bak\\.cxx|_backup\\.c|_backup\\.cc|_backup\\.cpp|_backup\\.cxx)$")
list(FILTER STROVA_HEADERS EXCLUDE REGEX ".*(\\.bak\\.h|\\.old\\.h|_bak\\.h|_backup\\.h|\\.bak\\.hpp|\\.old\\.hpp|_bak\\.hpp|_backup\\.hpp|\\.bak\\.hh|\\.old\\.hh|_bak\\.hh|_backup\\.hh|\\.bak\\.hxx|\\.old\\.hxx|_bak\\.hxx|_backup\\.hxx)$")

# --------------------------------------------------
# Hard fail if key rebuild files are missing
# --------------------------------------------------
foreach(required_src
    "${STROVA_SRC_DIR}/brush/BrushCreatorScreen.cpp"
    "${STROVA_SRC_DIR}/brush/BrushResourceManager.cpp"
    "${STROVA_SRC_DIR}/platform/AppPaths.cpp"
    "${STROVA_SRC_DIR}/ui/UI.cpp"
    "${STROVA_SRC_DIR}/ui/UILayout.cpp"
    "${STROVA_SRC_DIR}/platform/FileDialog.cpp"

    # Plugin system foundation
    "${STROVA_SRC_DIR}/plugin/PluginHost.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginSecurity.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginPackage.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginManager.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginRegistry.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginProjectData.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginDocumentModel.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginFlowBridge.cpp"

    # Tool registry groundwork
    "${STROVA_SRC_DIR}/core/ToolRegistry.cpp"
)
    if (NOT EXISTS "${required_src}")
        message(FATAL_ERROR "Required Strova source file missing: ${required_src}")
    endif()
endforeach()

# Always include critical live sources
set(STROVA_ALWAYS_INCLUDE_SOURCES
    "${STROVA_SRC_DIR}/brush/BrushCreatorScreen.cpp"
    "${STROVA_SRC_DIR}/brush/BrushResourceManager.cpp"
    "${STROVA_SRC_DIR}/platform/AppPaths.cpp"
    "${STROVA_SRC_DIR}/ui/UI.cpp"
    "${STROVA_SRC_DIR}/ui/UILayout.cpp"
    "${STROVA_SRC_DIR}/platform/FileDialog.cpp"

    # Plugin system foundation
    "${STROVA_SRC_DIR}/plugin/PluginHost.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginSecurity.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginPackage.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginManager.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginRegistry.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginProjectData.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginDocumentModel.cpp"
    "${STROVA_SRC_DIR}/plugin/PluginFlowBridge.cpp"

    # Tool registry groundwork
    "${STROVA_SRC_DIR}/core/ToolRegistry.cpp"
)

foreach(extra_src IN LISTS STROVA_ALWAYS_INCLUDE_SOURCES)
    if (EXISTS "${extra_src}")
        list(APPEND STROVA_SOURCES "${extra_src}")
    endif()
endforeach()

list(REMOVE_DUPLICATES STROVA_SOURCES)
list(REMOVE_DUPLICATES STROVA_HEADERS)

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/brush/BrushResourceManager.cpp" BRUSH_RESOURCE_MANAGER_INDEX)
if (BRUSH_RESOURCE_MANAGER_INDEX EQUAL -1)
    message(FATAL_ERROR "BrushResourceManager.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/plugin/PluginManager.cpp" PLUGIN_MANAGER_INDEX)
if (PLUGIN_MANAGER_INDEX EQUAL -1)
    message(FATAL_ERROR "PluginManager.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/plugin/PluginRegistry.cpp" PLUGIN_REGISTRY_INDEX)
if (PLUGIN_REGISTRY_INDEX EQUAL -1)
    message(FATAL_ERROR "PluginRegistry.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/plugin/PluginProjectData.cpp" PLUGIN_PROJECT_DATA_INDEX)
if (PLUGIN_PROJECT_DATA_INDEX EQUAL -1)
    message(FATAL_ERROR "PluginProjectData.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/plugin/PluginDocumentModel.cpp" PLUGIN_DOCUMENT_MODEL_INDEX)
if (PLUGIN_DOCUMENT_MODEL_INDEX EQUAL -1)
    message(FATAL_ERROR "PluginDocumentModel.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/plugin/PluginFlowBridge.cpp" PLUGIN_FLOW_BRIDGE_INDEX)
if (PLUGIN_FLOW_BRIDGE_INDEX EQUAL -1)
    message(FATAL_ERROR "PluginFlowBridge.cpp was not picked up by source discovery.")
endif()

list(FIND STROVA_SOURCES "${STROVA_SRC_DIR}/core/ToolRegistry.cpp" TOOL_REGISTRY_INDEX)
if (TOOL_REGISTRY_INDEX EQUAL -1)
    message(FATAL_ERROR "ToolRegistry.cpp was not picked up by source discovery.")
endif()

# --------------------------------------------------
# Executable
# --------------------------------------------------
add_executable(Strova
    ${STROVA_SOURCES}
    ${STROVA_HEADERS}
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Strova)

target_compile_definitions(Strova
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        STROVA_PLUGIN_SUPPORT=1
)

target_include_directories(Strova
    PRIVATE
        "${STROVA_SRC_DIR}"
        "${SDL2_INCLUDE_DIR}"
        "${SDL2_IMAGE_INCLUDE_DIR}"
        "${SDL2_TTF_INCLUDE_DIR}"
        "${NFD_INCLUDE_DIR}"
)

target_link_libraries(Strova
    PRIVATE
        "${SDL2_LIB}"
        "${SDL2MAIN_LIB}"
        "${SDL2_IMAGE_LIB}"
        "${SDL2_TTF_LIB}"
        strova_nfd
)

# Debug console, release windowed app
if (MSVC)
    target_link_options(Strova PRIVATE
        "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
        "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>"
        "$<$<CONFIG:RelWithDebInfo>:/SUBSYSTEM:WINDOWS>"
        "$<$<CONFIG:MinSizeRel>:/SUBSYSTEM:WINDOWS>"
    )
endif()

source_group(TREE "${STROVA_SRC_DIR}" FILES ${STROVA_SOURCES} ${STROVA_HEADERS})

# --------------------------------------------------
# Runtime DLL copy
# --------------------------------------------------
set(STROVA_RUNTIME_DLLS
    "${SDL2_DLL}"
    "${SDL2_IMAGE_DLL}"
    "${SDL2_TTF_DLL}"
)

foreach(dll_file IN LISTS STROVA_RUNTIME_DLLS)
    add_custom_command(TARGET Strova POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${dll_file}"
            "$<TARGET_FILE_DIR:Strova>"
        VERBATIM
    )
endforeach()

foreach(dll_file IN LISTS SDL2_IMAGE_OPTIONAL_DLLS)
    if (EXISTS "${dll_file}")
        add_custom_command(TARGET Strova POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll_file}"
                "$<TARGET_FILE_DIR:Strova>"
            VERBATIM
        )
    endif()
endforeach()

# --------------------------------------------------
# Final status text
# --------------------------------------------------
list(LENGTH STROVA_SOURCES STROVA_SOURCE_COUNT)
list(LENGTH STROVA_HEADERS STROVA_HEADER_COUNT)

message(STATUS "Strova source files: ${STROVA_SOURCE_COUNT}")
message(STATUS "Strova header files: ${STROVA_HEADER_COUNT}")
message(STATUS "Using NFD Windows backend: ${NFD_WIN_SOURCE}")
message(STATUS "Plugin support enabled: YES")

add_custom_command(TARGET Strova POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Strova Core built."
    COMMAND ${CMAKE_COMMAND} -E echo "Strova UI built."
    COMMAND ${CMAKE_COMMAND} -E echo "Strova Runtime ready."
    COMMAND ${CMAKE_COMMAND} -E echo "Strova Plugin Host ready."
    VERBATIM
)