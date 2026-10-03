cmake_minimum_required(VERSION 3.16)

project(zxgw
    VERSION 1.0.0
    DESCRIPTION "The Gosh Wonderful ZX Spectrum ROM translated to C++ for the terminal"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ZXGW_BUILD_APP "Build the zxgw command line application" ON)
option(ZXGW_BUILD_EXAMPLES "Build the example that embeds the library" ON)
option(ZXGW_BUILD_TESTS "Build and register the tests" ON)

# ---------------------------------------------------------------------------
# The static library: everything except main().
# ---------------------------------------------------------------------------
add_library(zxgw STATIC
    src/core/calculator.cpp
    src/core/charset.cpp
    src/core/editor.cpp
    src/core/expression.cpp
    src/core/extensions.cpp
    src/core/files.cpp
    src/core/fp.cpp
    src/core/graphics.cpp
    src/core/gw.cpp
    src/core/interpreter.cpp
    src/core/keyboard.cpp
    src/core/machine.cpp
    src/core/screen.cpp
    src/core/screen_model.cpp
    src/core/sound.cpp
    src/core/tables.cpp
    src/core/tape.cpp
    src/ext/standard_extensions.cpp
    src/platform/terminal_host.cpp
    src/platform/terminal_posix.cpp
    src/platform/terminal_win32.cpp
)
add_library(zxgw::zxgw ALIAS zxgw)

target_include_directories(zxgw
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)

if(MSVC)
    target_compile_options(zxgw PRIVATE /W4 /utf-8)
    target_compile_definitions(zxgw PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
    target_compile_options(zxgw PRIVATE -Wall -Wextra)
endif()

# std::filesystem needs an extra library on old GCC versions.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    target_link_libraries(zxgw PUBLIC stdc++fs)
endif()

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
if(ZXGW_BUILD_APP)
    add_executable(zxgw-app app/main.cpp)
    set_target_properties(zxgw-app PROPERTIES OUTPUT_NAME zxgw)
    target_link_libraries(zxgw-app PRIVATE zxgw::zxgw)
    if(MSVC)
        target_compile_options(zxgw-app PRIVATE /utf-8)
    endif()
    install(TARGETS zxgw-app RUNTIME DESTINATION bin)
endif()

if(ZXGW_BUILD_EXAMPLES)
    add_executable(zxgw-embed-example examples/embed/embed.cpp)
    target_link_libraries(zxgw-embed-example PRIVATE zxgw::zxgw)
    add_executable(zxgw-extend-example examples/extend/extend.cpp)
    target_link_libraries(zxgw-extend-example PRIVATE zxgw::zxgw)
endif()

# ---------------------------------------------------------------------------
# Installation of the library, so other projects can use find_package(zxgw).
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS zxgw EXPORT zxgwTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/zxgw DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT zxgwTargets NAMESPACE zxgw:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zxgw)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/zxgwConfig.cmake
    "include(\${CMAKE_CURRENT_LIST_DIR}/zxgwTargets.cmake)\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/zxgwConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zxgw)

# ---------------------------------------------------------------------------
# Tests: BASIC programs run in batch mode, output compared with the expected.
# ---------------------------------------------------------------------------
if(ZXGW_BUILD_TESTS AND ZXGW_BUILD_APP)
    enable_testing()
    file(GLOB ZXGW_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.in)
    foreach(test_input ${ZXGW_TESTS})
        get_filename_component(test_name ${test_input} NAME_WE)
        add_test(NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
                -DZXGW=$<TARGET_FILE:zxgw-app>
                -DINPUT=${test_input}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}.out
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/test-work
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake)
    endforeach()
    if(ZXGW_BUILD_EXAMPLES)
        add_test(NAME extend_example
            COMMAND ${CMAKE_COMMAND}
                -DPROGRAM=$<TARGET_FILE:zxgw-extend-example>
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/extend_example.expected
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/test-work
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake)
        set_tests_properties(extend_example PROPERTIES ENVIRONMENT "ZXGW_EXAMPLE=from the environment")
    endif()
endif()
