cmake_minimum_required(VERSION 3.26)

project(ScreenFX VERSION 0.1.0 LANGUAGES CXX)

option(SCREENFX_BUILD_TESTS "Build ScreenFX tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "ScreenFX v1 targets Windows only")
endif()
add_executable(screenfx WIN32
    src/main.cpp
    src/platform/Win32App.cpp
    src/platform/Win32App.h
)

target_compile_definitions(screenfx PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    SCREENFX_VERSION=L"${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(screenfx PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

target_link_libraries(screenfx PRIVATE
    user32
    shell32
    shlwapi
)

set_target_properties(screenfx PROPERTIES
    OUTPUT_NAME "ScreenFX"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:screenfx>"
)

include(GNUInstallDirs)
install(TARGETS screenfx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(SCREENFX_BUILD_TESTS)
    enable_testing()
    add_test(NAME screenfx_smoke COMMAND screenfx --self-test)
    set_tests_properties(screenfx_smoke PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:screenfx>")
endif()
