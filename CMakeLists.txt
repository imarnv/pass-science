cmake_minimum_required(VERSION 3.16)

project(EngineeringWorkbench
    VERSION 1.0.0
    DESCRIPTION "Unified engineering workbench: geometry import, thermal FEA, parallel batches, embedded mocks"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WB_BUILD_TESTS "Build the unit test executable" ON)
option(WB_BUILD_TOOLS "Build the command-line results tool" ON)
option(WB_STATIC_RUNTIME "Link the C++ runtime statically (recommended for the Windows package)" OFF)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Compiler warnings and the flags string reported in benchmark results
# ---------------------------------------------------------------------------
if(MSVC)
    set(WB_WARNINGS /W4 /permissive-)
else()
    set(WB_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" WB_BUILD_TYPE_UPPER)
set(WB_REPORTED_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${WB_BUILD_TYPE_UPPER}}")
string(STRIP "${WB_REPORTED_FLAGS}" WB_REPORTED_FLAGS)

# ---------------------------------------------------------------------------
# Core library: numerical code, parsers, worker pool, device mocks.
# Has no knowledge of HTTP, JSON transport or the user interface.
# ---------------------------------------------------------------------------
add_library(wb_core STATIC
    src/core/fea/thermal_rod.cpp
    src/core/io/stl_parser.cpp
    src/core/io/nodal_csv_parser.cpp
    src/core/io/text_utils.cpp
    src/core/parallel/worker_pool.cpp
    src/core/parallel/batch.cpp
    src/core/hardware/device.cpp
    src/core/hardware/mock_devices.cpp
    src/core/hardware/device_manager.cpp
    src/core/ml/backprop.cpp
    src/core/system/system_info.cpp
    src/core/report/csv_export.cpp
)
target_include_directories(wb_core PUBLIC src)
target_compile_options(wb_core PRIVATE ${WB_WARNINGS})
target_compile_definitions(wb_core PRIVATE
    WB_BUILD_FLAGS="${WB_REPORTED_FLAGS}"
    WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    WB_VERSION="${PROJECT_VERSION}")
target_link_libraries(wb_core PUBLIC Threads::Threads)

if(WB_STATIC_RUNTIME AND NOT MSVC)
    set(WB_STATIC_LINK_FLAGS -static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# Application: local HTTP transport + static web frontend. On Windows the
# output is named Launch.exe and built as a GUI-subsystem program (no console).
# ---------------------------------------------------------------------------
# The application and its web assets are laid out in build/package/ exactly as
# in the shipped ZIP (single- and multi-config generators alike).
set(WB_PACKAGE_DIR ${CMAKE_BINARY_DIR}/package)

add_executable(workbench
    src/server/main.cpp
    src/server/app_server.cpp
    src/server/json_mapping.cpp
    src/server/job_manager.cpp
    src/server/platform.cpp
)
target_include_directories(workbench PRIVATE src third_party)
target_link_libraries(workbench PRIVATE wb_core ${WB_STATIC_LINK_FLAGS})
target_compile_options(workbench PRIVATE ${WB_WARNINGS})
set_target_properties(workbench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${WB_PACKAGE_DIR})
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} CONFIG_UPPER)
    set_target_properties(workbench PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${WB_PACKAGE_DIR})
endforeach()

if(WIN32)
    set_target_properties(workbench PROPERTIES OUTPUT_NAME "Launch" WIN32_EXECUTABLE ON)
    target_link_libraries(workbench PRIVATE ws2_32 shell32 ole32)
    target_compile_definitions(workbench PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
    if(MINGW)
        target_link_options(workbench PRIVATE -mwindows)
    endif()
endif()

# The web frontend (and the bundled examples served by "Load example") is
# copied next to the executable on every build, so a build tree runs exactly
# like the packaged application. A separate target keeps the copy current even
# when only web files changed and the executable did not relink.
add_custom_target(web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/web ${WB_PACKAGE_DIR}/app/web
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WB_PACKAGE_DIR}/app/web/examples
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/data/heatsink.stl
            ${CMAKE_SOURCE_DIR}/data/synthetic_nodal_temperatures.csv
            ${WB_PACKAGE_DIR}/app/web/examples/
    COMMENT "Copying web frontend and example data")
add_dependencies(workbench web_assets)

# ---------------------------------------------------------------------------
# Command-line tool: regenerates results/ (CSV + SVG plots) and example data.
# ---------------------------------------------------------------------------
if(WB_BUILD_TOOLS)
    add_executable(wb_cli
        src/tools/wb_cli.cpp
        src/tools/svg_plot.cpp
        src/tools/example_data.cpp
    )
    target_include_directories(wb_cli PRIVATE src)
    target_link_libraries(wb_cli PRIVATE wb_core ${WB_STATIC_LINK_FLAGS})
    target_compile_options(wb_cli PRIVATE ${WB_WARNINGS})
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WB_BUILD_TESTS)
    enable_testing()
    add_executable(wb_tests
        tests/test_main.cpp
        tests/test_fea.cpp
        tests/test_parsers.cpp
        tests/test_parallel.cpp
        tests/test_hardware.cpp
        tests/test_backprop.cpp
    )
    target_include_directories(wb_tests PRIVATE src tests)
    target_link_libraries(wb_tests PRIVATE wb_core ${WB_STATIC_LINK_FLAGS})
    target_compile_definitions(wb_tests PRIVATE WB_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
                                                WB_EXAMPLE_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
    target_compile_options(wb_tests PRIVATE ${WB_WARNINGS})
    add_test(NAME unit_tests COMMAND wb_tests)
endif()
