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
    src/platform/Monitors.cpp
    src/platform/Monitors.h
    src/graphics/D3D11Context.cpp
    src/graphics/D3D11Context.h
    src/graphics/CaptureSession.cpp
    src/graphics/CaptureSession.h
    src/graphics/OverlayWindow.cpp
    src/graphics/OverlayWindow.h
    src/graphics/RenderEngine.cpp
    src/graphics/RenderEngine.h
    src/core/Settings.h
    src/core/SettingsStore.cpp
    src/core/SettingsStore.h
    src/ui/SettingsPanel.cpp
    src/ui/SettingsPanel.h
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
    comctl32
    d3d11
    dxgi
    d3dcompiler
    windowsapp
)

find_program(SCREENFX_FXC fxc.exe
    HINTS
        "$ENV{WindowsSdkDir}bin"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
)
if(NOT SCREENFX_FXC)
    message(FATAL_ERROR "fxc.exe was not found. Install the Windows SDK or set SCREENFX_FXC.")
endif()

set(SCREENFX_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/screenfx.hlsl")
set(SCREENFX_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(SCREENFX_VERTEX_SHADER "${SCREENFX_SHADER_OUTPUT_DIR}/screenfx_vs.cso")
set(SCREENFX_PIXEL_SHADER "${SCREENFX_SHADER_OUTPUT_DIR}/screenfx_ps.cso")
add_custom_command(
    OUTPUT "${SCREENFX_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SCREENFX_SHADER_OUTPUT_DIR}"
    COMMAND "${SCREENFX_FXC}" /nologo /T vs_5_0 /E VSMain /O3 /Fo "${SCREENFX_VERTEX_SHADER}" "${SCREENFX_SHADER_SOURCE}"
    DEPENDS "${SCREENFX_SHADER_SOURCE}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${SCREENFX_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SCREENFX_SHADER_OUTPUT_DIR}"
    COMMAND "${SCREENFX_FXC}" /nologo /T ps_5_0 /E PSMain /O3 /Fo "${SCREENFX_PIXEL_SHADER}" "${SCREENFX_SHADER_SOURCE}"
    DEPENDS "${SCREENFX_SHADER_SOURCE}"
    VERBATIM
)
add_custom_target(screenfx_shaders DEPENDS "${SCREENFX_VERTEX_SHADER}" "${SCREENFX_PIXEL_SHADER}")
add_dependencies(screenfx screenfx_shaders)
add_custom_command(TARGET screenfx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:screenfx>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SCREENFX_VERTEX_SHADER}" "$<TARGET_FILE_DIR:screenfx>/shaders/screenfx_vs.cso"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SCREENFX_PIXEL_SHADER}" "$<TARGET_FILE_DIR:screenfx>/shaders/screenfx_ps.cso"
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
