cmake_minimum_required(VERSION 3.20)
project(CustomUPC VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601)
    # Define IMGUI_API to import symbols from the DLL
    add_definitions(-DIMGUI_API=__declspec\(dllimport\))
endif()

# Define project directories
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

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

set(SOURCES
    src/main.cpp
    src/UpsDevice.cpp
    src/MainWindow.cpp
    src/Logger.c
)

set(HEADERS
    src/headers/UpsState.h
    src/headers/UpsDevice.h
    src/headers/MainWindow.h
    src/headers/Logger.h
)

add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    res/CustomUPC.rc
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE src/headers)

# Link libraries
# hid.lib provides HidP_* descriptor parsing used by UpsDevice.cpp
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
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 "$<$<COMPILE_LANGUAGE:CXX>:/permissive->")
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

source_group("src" FILES ${SOURCES})
source_group("src\\headers" FILES ${HEADERS})
source_group("" FILES res/CustomUPC.rc)
