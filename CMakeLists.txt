cmake_minimum_required(VERSION 3.20)
project(CustomUPC VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601)
    # Define IMGUI_API to import symbols from the DLL
    add_definitions(-DIMGUI_API=__declspec\(dllimport\))
endif()

# Define project directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(RES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/res)

# Determine architecture for library paths
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x64")
else()
    set(ARCH "x86")
endif()

# --- External Libraries Configuration ---

# 1. HIDAPI
add_library(hidapi SHARED IMPORTED GLOBAL)
set_target_properties(hidapi PROPERTIES
    IMPORTED_LOCATION "${LIB_DIR}/hidapi-win/${ARCH}/hidapi.dll"
    IMPORTED_IMPLIB   "${LIB_DIR}/hidapi-win/${ARCH}/hidapi.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/hidapi-win/include"
)

# 2. ImGui (Pre-built DLL)
add_library(imgui SHARED IMPORTED GLOBAL)
set_target_properties(imgui PROPERTIES
    IMPORTED_LOCATION "${LIB_DIR}/imgui/${ARCH}/imgui.dll"
    IMPORTED_IMPLIB   "${LIB_DIR}/imgui/${ARCH}/imgui.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LIB_DIR}/imgui/include;${LIB_DIR}/imgui/include/backends"
)

# --- Main Executable ---

set(CORE_HEADERS
    ${SRC_DIR}/core/UpsState.h
    ${SRC_DIR}/core/HidDevice.h
    ${SRC_DIR}/core/UpsDevice.h
    ${SRC_DIR}/core/UpsMonitor.h
)

set(UTILS_HEADERS
    ${SRC_DIR}/utils/Logger.h
    ${SRC_DIR}/utils/Utf.h
)

add_executable(${PROJECT_NAME} WIN32
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/UI/MainWindow.h
    ${CORE_HEADERS}
    ${UTILS_HEADERS}
    ${RES_DIR}/CustomUPC.rc
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR})

# Link libraries
# hid.lib provides HidP_* descriptor parsing used by core/HidReport.h
target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui
    hidapi
    hid
    d3d11
    dxgi
    d3dcompiler
)

# Compiler options
if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    # High warning level, disable permissive mode
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
endif()

# Output directories configuration
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# --- Post Build Events (Copy DLLs) ---

# Command to copy required DLLs to the output directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    # Copy HIDAPI DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/hidapi-win/${ARCH}/hidapi.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

    # Copy ImGui DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIB_DIR}/imgui/${ARCH}/imgui.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

    COMMENT "Copying runtime DLLs to output directory..."
)

# --- Source Groups (Visual Studio Filters) ---

source_group("Source" FILES ${SRC_DIR}/main.cpp)
source_group("Headers\\UI" FILES ${SRC_DIR}/UI/MainWindow.h)
source_group("Headers\\Core" FILES ${CORE_HEADERS})
source_group("Headers\\Utils" FILES ${UTILS_HEADERS})
source_group("Resources" FILES ${RES_DIR}/CustomUPC.rc)