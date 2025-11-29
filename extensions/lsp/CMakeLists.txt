# ============================================================================
# slang-autos LSP Server
# ============================================================================

add_executable(slang-autos-lsp
    main.cpp
    AutosServer.cpp
)

target_include_directories(slang-autos-lsp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

# Add reflect-cpp include directory (for rfl/Generic.hpp etc)
# FetchContent sets <lowercased_name>_SOURCE_DIR
message(STATUS "reflectcpp_SOURCE_DIR = ${reflectcpp_SOURCE_DIR}")
target_include_directories(slang-autos-lsp SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/reflectcpp-src/include
)

# Link against core library and reflect-cpp for JSON serialization
target_link_libraries(slang-autos-lsp
    PRIVATE
        slang-autos-lib
        reflectcpp
)

# Suppress warnings in LSP code that come from reflect-cpp templates
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(slang-autos-lsp PRIVATE
        -Wall -Wextra
        -Wno-missing-field-initializers
    )
endif()
