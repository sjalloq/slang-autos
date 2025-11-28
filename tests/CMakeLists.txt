# Test executable
add_executable(slang-autos-tests
    test_main.cpp
    test_parser.cpp
    test_template_matcher.cpp
    test_expander.cpp
    test_writer.cpp
    test_tool.cpp
    test_integration.cpp
)

target_link_libraries(slang-autos-tests
    PRIVATE
        slang-autos-lib
        Catch2::Catch2WithMain
)

# Register tests with CTest
include(Catch)
catch_discover_tests(slang-autos-tests)
