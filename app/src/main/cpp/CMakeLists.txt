cmake_minimum_required(VERSION 3.22.1)
project(funasr_engine LANGUAGES C CXX)

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
set(GGML_CPU_KLEIDIAI OFF CACHE BOOL "" FORCE)

set(LLAMA_COMMIT 803b7fcae893e9caaee3921779628fef83ac0965)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LLAMA_SOURCE "${PROJECT_ROOT}/third_party/llama.cpp-${LLAMA_COMMIT}")
if(NOT EXISTS "${LLAMA_SOURCE}/ggml/CMakeLists.txt")
    message(FATAL_ERROR "Run scripts/bootstrap.ps1 before building")
endif()

add_subdirectory("${LLAMA_SOURCE}/ggml" "${CMAKE_CURRENT_BINARY_DIR}/ggml")

add_library(funasr_engine SHARED
    native_engine.cpp
    sensevoice.cpp
    vad.cpp)
target_include_directories(funasr_engine PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_options(funasr_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(funasr_engine PRIVATE ggml android log)
