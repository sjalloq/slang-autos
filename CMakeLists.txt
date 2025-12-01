cmake_minimum_required(VERSION 3.20)
project(slang-autos
    VERSION 0.1.0
    DESCRIPTION "SystemVerilog AUTO macro expander built on slang"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(SLANG_AUTOS_BUILD_TESTS "Build tests" ON)
option(SLANG_AUTOS_BUILD_LSP "Build LSP server" ON)
option(SLANG_AUTOS_USE_SYSTEM_SLANG "Use system-installed slang instead of submodule" OFF)

# ============================================================================
# Dependencies
# ============================================================================

# slang - SystemVerilog compiler
if(SLANG_AUTOS_USE_SYSTEM_SLANG)
    find_package(slang REQUIRED)
else()
    # Use slang as a submodule
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/slang/CMakeLists.txt")
        message(FATAL_ERROR
            "slang submodule not found. Please run:\n"
            "  git submodule update --init --recursive\n"
            "Or set SLANG_AUTOS_USE_SYSTEM_SLANG=ON to use system-installed slang."
        )
    endif()

    # Configure slang build options
    set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
    set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
    set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)

    add_subdirectory(external/slang)
endif()

# FetchContent for dependencies
include(FetchContent)

# reflect-cpp - JSON serialization (required for LSP)
if(SLANG_AUTOS_BUILD_LSP)
    FetchContent_Declare(
        reflectcpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
        GIT_TAG v0.17.0
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(reflectcpp)

    # Suppress warnings in reflect-cpp
    if(TARGET reflectcpp AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(reflectcpp PRIVATE
            -Wno-missing-field-initializers
            -Wno-unused-parameter
        )
    endif()
endif()

# ============================================================================
# Main Library
# ============================================================================

add_library(slang-autos-lib
    src/Diagnostics.cpp
    src/Parser.cpp
    src/TemplateMatcher.cpp
    src/Expander.cpp
    src/Writer.cpp
    src/Tool.cpp
    src/AutosRewriter.cpp
)

target_include_directories(slang-autos-lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(slang-autos-lib
    PUBLIC
        slang::slang
)

# Enable warnings
target_compile_options(slang-autos-lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ============================================================================
# CLI Executable
# ============================================================================

add_executable(slang-autos src/main.cpp)

target_link_libraries(slang-autos
    PRIVATE
        slang-autos-lib
)

# ============================================================================
# Tests
# ============================================================================

if(SLANG_AUTOS_BUILD_TESTS)
    enable_testing()

    # Catch2 - Testing framework
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)

    add_subdirectory(tests)
endif()

# ============================================================================
# LSP Extension
# ============================================================================

if(SLANG_AUTOS_BUILD_LSP)
    add_subdirectory(extensions/lsp)
endif()

# ============================================================================
# Install (future)
# ============================================================================

# install(TARGETS slang-autos slang-autos-lib
#     RUNTIME DESTINATION bin
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
# )
# install(DIRECTORY include/ DESTINATION include)
