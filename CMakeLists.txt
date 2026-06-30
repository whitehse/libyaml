cmake_minimum_required(VERSION 3.20)
project(libyaml LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 10.1 Shared library build option
option(BUILD_SHARED_LIBS "Build shared library instead of static" OFF)

add_library(yaml src/yaml.c)
set_target_properties(yaml PROPERTIES
    SOVERSION 0
    VERSION ${PROJECT_VERSION}
)

target_include_directories(yaml PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

include(GNUInstallDirs)

# 10.2 pkg-config file generation
configure_file(yaml.pc.in yaml.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/yaml.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# 10.3 Version header generation
configure_file(include/yaml_version.h.in include/yaml_version.h @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/yaml_version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS yaml EXPORT yaml-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT yaml-targets FILE yaml-config.cmake
    NAMESPACE yaml:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yaml)

# 10.4 Sanitizer CI targets
option(ENABLE_ASAN "Enable AddressSanitizer for test binaries" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for test binaries" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

enable_testing()

# Smoke test
add_executable(yaml_smoke_test tests/test_yaml_smoke.c)
target_link_libraries(yaml_smoke_test PRIVATE yaml)
add_test(NAME yaml_smoke COMMAND yaml_smoke_test)

# Dialectic test (paired parse/serialize buffer exchange)
add_executable(yaml_dialectic_test tests/test_yaml_dialectic.c)
target_link_libraries(yaml_dialectic_test PRIVATE yaml)
add_test(NAME yaml_dialectic COMMAND yaml_dialectic_test)

# Error and edge-case tests
add_executable(yaml_errors_test tests/test_yaml_errors.c)
target_link_libraries(yaml_errors_test PRIVATE yaml)
add_test(NAME yaml_errors COMMAND yaml_errors_test)

# Knowledge/merge tests
add_executable(yaml_knowledge_test tests/test_yaml_knowledge.c)
target_link_libraries(yaml_knowledge_test PRIVATE yaml)
add_test(NAME yaml_knowledge COMMAND yaml_knowledge_test)

# Nesting tests (indentation-based nested mappings/sequences)
add_executable(yaml_nesting_test tests/test_yaml_nesting.c)
target_link_libraries(yaml_nesting_test PRIVATE yaml)
add_test(NAME yaml_nesting COMMAND yaml_nesting_test)

# Block scalar tests (literal | and folded >)
add_executable(yaml_block_scalar_test tests/test_yaml_block_scalars.c)
target_link_libraries(yaml_block_scalar_test PRIVATE yaml)
add_test(NAME yaml_block_scalar COMMAND yaml_block_scalar_test)

# Deep knowledge tree tests (nested paths, sequence indices, enumeration, stats)
add_executable(yaml_knowledge_deep_test tests/test_yaml_knowledge_deep.c)
target_link_libraries(yaml_knowledge_deep_test PRIVATE yaml)
add_test(NAME yaml_knowledge_deep COMMAND yaml_knowledge_deep_test)

# API query/status tests
add_executable(yaml_api_test tests/test_yaml_api.c)
target_link_libraries(yaml_api_test PRIVATE yaml)
add_test(NAME yaml_api COMMAND yaml_api_test)

# Anchor/Alias tests
add_executable(yaml_anchor_alias_test tests/test_yaml_anchor_alias.c)
target_link_libraries(yaml_anchor_alias_test PRIVATE yaml)
add_test(NAME yaml_anchor_alias COMMAND yaml_anchor_alias_test)

# Quoted scalar edge-case tests
add_executable(yaml_quoted_edge_test tests/test_yaml_quoted_edge.c)
target_link_libraries(yaml_quoted_edge_test PRIVATE yaml)
add_test(NAME yaml_quoted_edge COMMAND yaml_quoted_edge_test)

# Round-trip fidelity tests
add_executable(yaml_roundtrip_test tests/test_yaml_roundtrip.c)
target_link_libraries(yaml_roundtrip_test PRIVATE yaml)
add_test(NAME yaml_roundtrip COMMAND yaml_roundtrip_test)

# Flow collection tests ([item1, item2] and {k: v})
add_executable(yaml_flow_test tests/test_yaml_flow.c)
target_link_libraries(yaml_flow_test PRIVATE yaml)
add_test(NAME yaml_flow COMMAND yaml_flow_test)

# Multiline scalar continuation tests
add_executable(yaml_multiline_test tests/test_yaml_multiline.c)
target_link_libraries(yaml_multiline_test PRIVATE yaml)
add_test(NAME yaml_multiline COMMAND yaml_multiline_test)

# Fuzz harness (optional — requires -fsanitize=fuzzer)
option(ENABLE_FUZZ "Build fuzz harness (requires libFuzzer)" OFF)
if(ENABLE_FUZZ)
    add_executable(fuzz_yaml fuzz/fuzz_yaml.c)
    target_link_libraries(fuzz_yaml PRIVATE yaml)
    target_compile_options(fuzz_yaml PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(fuzz_yaml PRIVATE -fsanitize=fuzzer,address)

    add_executable(fuzz_yaml_serializer fuzz/fuzz_yaml_serializer.c)
    target_link_libraries(fuzz_yaml_serializer PRIVATE yaml)
    target_compile_options(fuzz_yaml_serializer PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(fuzz_yaml_serializer PRIVATE -fsanitize=fuzzer,address)

    add_executable(fuzz_yaml_knowledge fuzz/fuzz_yaml_knowledge.c)
    target_link_libraries(fuzz_yaml_knowledge PRIVATE yaml)
    target_compile_options(fuzz_yaml_knowledge PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(fuzz_yaml_knowledge PRIVATE -fsanitize=fuzzer,address)
endif()

# Valgrind integration
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    option(ENABLE_VALGRIND "Run tests under Valgrind" OFF)
    if(ENABLE_VALGRIND)
        add_test(NAME yaml_smoke_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_smoke_test>)
        add_test(NAME yaml_dialectic_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_dialectic_test>)
        add_test(NAME yaml_errors_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_errors_test>)
        add_test(NAME yaml_knowledge_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_knowledge_test>)
        add_test(NAME yaml_nesting_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_nesting_test>)
        add_test(NAME yaml_block_scalar_valgrind
            COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --error-exitcode=1
                    $<TARGET_FILE:yaml_block_scalar_test>)
    endif()
endif()
