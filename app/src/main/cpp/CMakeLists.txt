cmake_minimum_required(VERSION 3.22.1)
project(funasr_engine LANGUAGES C CXX)

# ggml's Vulkan shader generator writes this path into a host toolchain file;
# normalize it before the template is configured so Windows backslashes are
# not parsed as CMake escape sequences.
if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    file(TO_CMAKE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" CMAKE_RUNTIME_OUTPUT_DIRECTORY)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# AGP cppFlags only reach C++ files. ggml's quantization and CPU dot-product
# kernels are C, so an otherwise optimized debug APK was running those at -O0.
# Apply optimization before adding ggml so both languages, including dependency
# targets, receive it. Keep debug symbols and IEEE math semantics for profiling.
add_compile_options(-O3)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Keep the default APK compatible with arm64 devices. Device-specific builds
# can select armv8.2-a+dotprod+fp16 after checking the CPU's advertised features.
set(FUNASR_ARM_ARCH "armv8-a" CACHE STRING "ARM instruction baseline")
set(GGML_CPU_ARM_ARCH "${FUNASR_ARM_ARCH}" CACHE STRING "" FORCE)
set(GGML_CPU_KLEIDIAI OFF CACHE BOOL "" FORCE)
option(FUNASR_VULKAN "Build the experimental Vulkan backend" OFF)
set(GGML_VULKAN ${FUNASR_VULKAN} CACHE BOOL "" FORCE)

set(LLAMA_COMMIT 803b7fcae893e9caaee3921779628fef83ac0965)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LLAMA_SOURCE "${PROJECT_ROOT}/third_party/llama.cpp-${LLAMA_COMMIT}")
if(NOT EXISTS "${LLAMA_SOURCE}/ggml/CMakeLists.txt")
    message(FATAL_ERROR "Run scripts/bootstrap.ps1 before building")
endif()

# The Android NDK ships the Vulkan C headers and loader stub, but not
# Vulkan-Hpp or a CMake package for SPIR-V-Headers.  The local experimental
# bundle is created from those NDK assets plus the host's Vulkan-Hpp headers.
# Keep this opt-in at configure time so a clean checkout can still build CPU
# only until the Vulkan dependency bundle is prepared.
set(FUNASR_VULKAN_ROOT "${PROJECT_ROOT}/.android-vulkan" CACHE PATH "Local Vulkan build dependencies")
if(FUNASR_VULKAN)
    if(NOT EXISTS "${FUNASR_VULKAN_ROOT}/include/vulkan/vulkan.hpp")
        message(FATAL_ERROR "Vulkan build requires headers in ${FUNASR_VULKAN_ROOT}/include")
    endif()
    set(Vulkan_INCLUDE_DIR "${FUNASR_VULKAN_ROOT}/include" CACHE PATH "" FORCE)
    find_library(Vulkan_LIBRARY NAMES vulkan REQUIRED)
    if(CMAKE_HOST_WIN32)
        set(Vulkan_GLSLC_EXECUTABLE "${CMAKE_ANDROID_NDK}/shader-tools/windows-x86_64/glslc.exe" CACHE FILEPATH "" FORCE)
    endif()
    set(SPIRV-Headers_DIR "${FUNASR_VULKAN_ROOT}/cmake/SPIRV-Headers" CACHE PATH "" FORCE)
endif()

add_subdirectory("${LLAMA_SOURCE}/ggml" "${CMAKE_CURRENT_BINARY_DIR}/ggml")

if(TARGET ggml-vulkan)
    target_include_directories(ggml-vulkan PRIVATE "${FUNASR_VULKAN_ROOT}/include")
endif()

add_library(funasr_engine SHARED
    native_engine.cpp
    sensevoice.cpp
    vad.cpp)
target_include_directories(funasr_engine PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_options(funasr_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(funasr_engine PRIVATE ggml android log)
