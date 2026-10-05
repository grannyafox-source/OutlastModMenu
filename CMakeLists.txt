cmake_minimum_required(VERSION 3.16)
project(OutlastModMenu VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OMM_BUILD_TESTS "Build the host unit tests (non-Windows hosts)" ON)
option(OMM_BUILD_PLUGIN_EXAMPLE "Build examples/plugin_example" ON)

# ---------------------------------------------------------------------------
# Host build (Linux/macOS): only the unit tests for the engine scanner, the
# INI handling and the string helpers. The mod itself is Windows-only.
if(NOT WIN32)
    if(OMM_BUILD_TESTS)
        enable_testing()
        add_executable(omm_tests
            tests/test_main.cpp
            tests/test_files.cpp
            tests/stubs.cpp
            src/ue3/scanner.cpp
            src/ue3/engine.cpp
            src/core/ini.cpp
            src/core/strutil.cpp
            src/core/fileutil.cpp
            src/core/settings.cpp
            src/core/log.cpp
            src/core/paths.cpp
            src/game/initweaks.cpp
            src/game/modloader.cpp)
        target_compile_options(omm_tests PRIVATE -Wall -Wextra)
        add_test(NAME omm_tests COMMAND omm_tests)
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# Third-party libraries
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(MINHOOK_DIR ${CMAKE_SOURCE_DIR}/third_party/minhook)

add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
# The DX11 backend compiles its shaders at runtime; route D3DCompile through
# the mod (src/render/overlay.cpp) so no d3dcompiler import is needed.
set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    PROPERTIES COMPILE_DEFINITIONS "D3DCompile=OMM_D3DCompile")

# ---------------------------------------------------------------------------
# The mod: dinput8.dll (also usable as an .asi or injected DLL)
set(OMM_SOURCES
    src/app/app.cpp
    src/app/dllmain.cpp
    src/app/version.rc
    src/core/fileutil.cpp
    src/core/ini.cpp
    src/core/log.cpp
    src/core/memory.cpp
    src/core/paths.cpp
    src/core/settings.cpp
    src/core/strutil.cpp
    src/proxy/dinput8_proxy.cpp
    src/render/input.cpp
    src/render/overlay.cpp
    src/ue3/bootstrap.cpp
    src/ue3/call.cpp
    src/ue3/engine.cpp
    src/ue3/hooks.cpp
    src/ue3/scanner.cpp
    src/game/actions.cpp
    src/game/enemies.cpp
    src/game/esp.cpp
    src/game/features_player.cpp
    src/game/features_visuals.cpp
    src/game/features_world.cpp
    src/game/frame.cpp
    src/game/initweaks.cpp
    src/game/inspector.cpp
    src/game/modloader.cpp
    src/game/outlast.cpp
    src/game/performance.cpp
    src/game/plugins.cpp
    src/game/state.cpp
    src/game/data/checkpoints.cpp
    src/game/data/guide.cpp
    src/ui/esp_draw.cpp
    src/ui/pages_character.cpp
    src/ui/pages_guide.cpp
    src/ui/pages_mods.cpp
    src/ui/pages_player.cpp
    src/ui/pages_system.cpp
    src/ui/pages_teleport.cpp
    src/ui/pages_visuals.cpp
    src/ui/pages_world.cpp
    src/ui/ui_main.cpp
    src/ui/widgets.cpp)

if(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32-bit MinGW decorates __stdcall names (Proxy_DirectInput8Create@20).
    list(APPEND OMM_SOURCES src/proxy/dinput8_mingw32.def)
else()
    list(APPEND OMM_SOURCES src/proxy/dinput8.def)
endif()

add_library(OutlastModMenu SHARED ${OMM_SOURCES})
set_target_properties(OutlastModMenu PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_include_directories(OutlastModMenu PRIVATE src)
target_link_libraries(OutlastModMenu PRIVATE imgui minhook dxguid dwmapi shell32 user32 imm32 gdi32 ole32)
target_compile_definitions(OutlastModMenu PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MINGW)
    target_compile_definitions(OutlastModMenu PRIVATE __USE_MINGW_ANSI_STDIO=1)
    target_compile_options(OutlastModMenu PRIVATE -Wall -Wextra)
    # No runtime DLLs to ship: libgcc/libstdc++ are linked in.
    target_link_options(OutlastModMenu PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp
        $<$<CONFIG:Release>:-s>)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(OutlastModMenu PRIVATE -Wl,--enable-stdcall-fixup)
    endif()
elseif(MSVC)
    target_compile_options(OutlastModMenu PRIVATE /W4 /utf-8 /EHsc /permissive-)
    target_compile_definitions(OutlastModMenu PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(TARGET OutlastModMenu imgui minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The same binary renamed for ASI loaders and the injector.
add_custom_command(TARGET OutlastModMenu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OutlastModMenu> $<TARGET_FILE_DIR:OutlastModMenu>/OutlastModMenu.asi
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OutlastModMenu> $<TARGET_FILE_DIR:OutlastModMenu>/OutlastModMenu.dll)

# ---------------------------------------------------------------------------
# Injector (alternative to the dinput8.dll method)
add_executable(OMMInjector tools/injector/injector.cpp)
if(MINGW)
    target_link_options(OMMInjector PRIVATE -municode -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
elseif(MSVC)
    set_property(TARGET OMMInjector PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# ---------------------------------------------------------------------------
# Example plugin
if(OMM_BUILD_PLUGIN_EXAMPLE)
    add_library(omm_plugin_example SHARED examples/plugin_example/plugin_example.cpp)
    target_include_directories(omm_plugin_example PRIVATE sdk)
    target_link_libraries(omm_plugin_example PRIVATE imgui imm32)
    set_target_properties(omm_plugin_example PROPERTIES PREFIX "")
    if(MINGW)
        target_link_options(omm_plugin_example PRIVATE -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
endif()
