cmake_minimum_required(VERSION 4.2)

project(
    arssyut
    VERSION 0.1.0
    DESCRIPTION "Lightweight native Windows presentation recorder"
    LANGUAGES CXX
)

option(ARSSYUT_BUILD_TESTS "Build Arssyut deterministic unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(arssyut_core STATIC
    src/core/session/session_state_machine.cpp
    src/core/time/monotonic_clock.cpp
    src/presentation/shortcut_visualizer.cpp
    src/visual/arvisual_scene_analysis.cpp
)

target_include_directories(arssyut_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(arssyut_core PRIVATE
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
    )
else()
    target_compile_options(arssyut_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(WIN32)
    add_library(arssyut_windows STATIC
        src/platform/windows/capture/captured_frame.cpp
        src/platform/windows/capture/wgc_capture_source.cpp
        src/platform/windows/graphics/d3d11_arvisual_scene_analyzer.cpp
        src/platform/windows/graphics/d3d11_compositor.cpp
        src/platform/windows/graphics/d3d11_device.cpp
        src/platform/windows/input/presentation_input_worker.cpp
        src/platform/windows/input/shortcut_keymap.cpp
        src/platform/windows/media/mf_h264_mp4_writer.cpp
        src/presentation/presentation_controller.cpp
        src/platform/windows/storage/recoverable_session.cpp
        src/platform/windows/video/native_video_pipeline.cpp
    )

    target_include_directories(arssyut_windows
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )

    target_compile_definitions(arssyut_windows PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )

    if(MSVC)
        target_compile_options(arssyut_windows PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    endif()

    target_link_libraries(arssyut_windows
        PUBLIC
            arssyut_core
            d3d11
            d3dcompiler
            dxgi
            gdi32
            user32
            mfplat
            mfreadwrite
            mfuuid
            windowsapp
    )

    add_library(arssyut_native_bridge SHARED
        src/bridge/native_bridge.cpp
        src/app/device_catalog.cpp
        src/app/lucide_icons.cpp
        src/app/recorder_overlay.cpp
        src/app/recorder_session.cpp
        src/app/region_geometry.cpp
        src/app/source_catalog.cpp
    )
    target_include_directories(arssyut_native_bridge
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(arssyut_native_bridge
        PRIVATE
            ARSSYUT_BRIDGE_BUILD
            UNICODE
            _UNICODE
        PUBLIC
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WIN32_WINNT=0x0A00
            WINVER=0x0A00
    )
    target_link_libraries(arssyut_native_bridge
        PRIVATE
            arssyut_windows
            mf
            dwmapi
            ole32
            psapi
            shell32
            uxtheme
            propsys
            uuid
    )
    if(MSVC)
        target_compile_options(arssyut_native_bridge PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    endif()

    add_executable(arssyut WIN32
        src/app/device_catalog.cpp
        src/app/lucide_icons.cpp
        src/app/region_geometry.cpp
        src/app/recorder_overlay.cpp
        src/app/recorder_session.cpp
        src/app/recorder_settings_window.cpp
        src/app/source_catalog.cpp
        src/app/windows_main.cpp
    )
    target_compile_definitions(arssyut PRIVATE
        UNICODE
        _UNICODE
    )

    target_link_libraries(arssyut PRIVATE
        arssyut_windows
        comctl32
        mf
        dwmapi
        ole32
        psapi
        shell32
        uxtheme
        propsys
        uuid
    )
    if(MSVC)
        target_compile_options(arssyut PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    endif()

    add_executable(arssyut_p1_probe
        tools/p1_capture_probe.cpp
    )
    target_link_libraries(arssyut_p1_probe PRIVATE arssyut_windows psapi)
    if(MSVC)
        target_compile_options(arssyut_p1_probe PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    endif()
endif()

if(ARSSYUT_BUILD_TESTS)
    enable_testing()

    add_executable(arssyut_core_tests
        tests/test_main.cpp
    )

    target_link_libraries(arssyut_core_tests PRIVATE arssyut_core)

    if(MSVC)
        target_compile_options(arssyut_core_tests PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_core_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    add_test(NAME arssyut_core_tests COMMAND arssyut_core_tests)

    add_executable(arssyut_audio_core_tests
        tests/test_audio_core.cpp
    )
    target_link_libraries(arssyut_audio_core_tests PRIVATE arssyut_core)
    if(MSVC)
        target_compile_options(arssyut_audio_core_tests PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_audio_core_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(
        NAME arssyut_audio_core_tests
        COMMAND arssyut_audio_core_tests
    )

    add_executable(arssyut_shortcut_tests
        tests/test_shortcut_visualizer.cpp
    )
    target_link_libraries(arssyut_shortcut_tests PRIVATE arssyut_core)
    if(MSVC)
        target_compile_options(arssyut_shortcut_tests PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_shortcut_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(
        NAME arssyut_shortcut_tests
        COMMAND arssyut_shortcut_tests
    )

    add_executable(arssyut_arvisual_scene_tests
        tests/test_arvisual_scene_analysis.cpp
    )
    target_link_libraries(
        arssyut_arvisual_scene_tests
        PRIVATE
            arssyut_core
    )
    if(MSVC)
        target_compile_options(arssyut_arvisual_scene_tests PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_arvisual_scene_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(
        NAME arssyut_arvisual_scene_tests
        COMMAND arssyut_arvisual_scene_tests
    )

    add_executable(arssyut_arzoom_parity_tests
        tests/test_arzoom_parity.cpp
    )
    target_include_directories(arssyut_arzoom_parity_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )
    if(MSVC)
        target_compile_options(arssyut_arzoom_parity_tests PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_arzoom_parity_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(
        NAME arssyut_arzoom_parity_tests
        COMMAND arssyut_arzoom_parity_tests
    )

    add_executable(arssyut_arzoom_upstream_phase1_gate
        tests/upstream/arzoom_phase1_gate.cpp
    )
    target_include_directories(arssyut_arzoom_upstream_phase1_gate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )
    if(MSVC)
        target_compile_options(arssyut_arzoom_upstream_phase1_gate PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
        )
    else()
        target_compile_options(arssyut_arzoom_upstream_phase1_gate PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(
        NAME arssyut_arzoom_upstream_phase1_gate
        COMMAND arssyut_arzoom_upstream_phase1_gate
    )


    # P6UI.6D-A: exact upstream Spotlight contracts are compiled directly
    # against the immutable vendored ArZoom snapshot before product integration.
    add_executable(arssyut_arzoom_spotlight_gate
        tests/upstream/arzoom_spotlight_gate.cpp
    )
    target_include_directories(arssyut_arzoom_spotlight_gate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )
    add_test(
        NAME arssyut_arzoom_spotlight_gate
        COMMAND arssyut_arzoom_spotlight_gate
    )

    add_executable(arssyut_arzoom_cinematic_spotlight_gate
        tests/upstream/arzoom_cinematic_spotlight_gate.cpp
    )
    target_include_directories(arssyut_arzoom_cinematic_spotlight_gate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )
    add_test(
        NAME arssyut_arzoom_cinematic_spotlight_gate
        COMMAND arssyut_arzoom_cinematic_spotlight_gate
    )

    add_executable(arssyut_arzoom_spotlight_zoom_resize_gate
        tests/upstream/arzoom_spotlight_zoom_resize_gate.cpp
    )
    target_include_directories(arssyut_arzoom_spotlight_zoom_resize_gate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/arzoom/include
    )
    add_test(
        NAME arssyut_arzoom_spotlight_zoom_resize_gate
        COMMAND arssyut_arzoom_spotlight_zoom_resize_gate
    )

    if(WIN32)
        add_executable(arssyut_windows_tests
            tests/windows/test_d3d11_device.cpp
        )
        target_link_libraries(arssyut_windows_tests PRIVATE arssyut_windows)
        if(MSVC)
            target_compile_options(arssyut_windows_tests PRIVATE
                /W4
                /permissive-
                /utf-8
                /Zc:__cplusplus
            )
        endif()
        add_test(NAME arssyut_windows_tests COMMAND arssyut_windows_tests)

        add_executable(arssyut_mf_av_writer_tests
            tests/windows/test_mf_av_writer.cpp
        )
        target_link_libraries(
            arssyut_mf_av_writer_tests
            PRIVATE
                arssyut_windows
                ole32
        )
        if(MSVC)
            target_compile_options(
                arssyut_mf_av_writer_tests
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /Zc:__cplusplus
            )
        endif()
        add_test(
            NAME arssyut_mf_av_writer_tests
            COMMAND arssyut_mf_av_writer_tests
        )

        add_executable(arssyut_region_geometry_tests
            tests/windows/test_region_geometry.cpp
            src/app/region_geometry.cpp
        )
        target_include_directories(
            arssyut_region_geometry_tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_compile_definitions(
            arssyut_region_geometry_tests
            PRIVATE
                WIN32_LEAN_AND_MEAN
                NOMINMAX
                _WIN32_WINNT=0x0A00
                WINVER=0x0A00
        )
        target_link_libraries(
            arssyut_region_geometry_tests
            PRIVATE
                arssyut_core
                user32
        )
        if(MSVC)
            target_compile_options(
                arssyut_region_geometry_tests
                PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /Zc:__cplusplus
            )
        endif()
        add_test(
            NAME arssyut_region_geometry_tests
            COMMAND arssyut_region_geometry_tests
        )

        add_executable(arssyut_native_bridge_tests
            tests/windows/test_native_bridge.cpp
        )
        target_link_libraries(
            arssyut_native_bridge_tests
            PRIVATE
                arssyut_native_bridge
        )
        if(MSVC)
            target_compile_options(arssyut_native_bridge_tests PRIVATE
                /W4
                /permissive-
                /utf-8
                /Zc:__cplusplus
            )
        endif()
        add_test(
            NAME arssyut_native_bridge_tests
            COMMAND arssyut_native_bridge_tests
        )

        add_executable(arssyut_presenter_zoom_tests
            tests/windows/test_presenter_zoom_controls.cpp
            src/app/region_geometry.cpp
        )
        target_link_libraries(
            arssyut_presenter_zoom_tests
            PRIVATE
                arssyut_windows
        )
        if(MSVC)
            target_compile_options(arssyut_presenter_zoom_tests PRIVATE
                /W4
                /permissive-
                /utf-8
                /Zc:__cplusplus
            )
        endif()
        add_test(
            NAME arssyut_presenter_zoom_tests
            COMMAND arssyut_presenter_zoom_tests
        )

        add_executable(arssyut_windows_shortcut_tests
            tests/windows/test_shortcut_keymap.cpp
        )
        target_link_libraries(
            arssyut_windows_shortcut_tests
            PRIVATE
                arssyut_windows
        )
        if(MSVC)
            target_compile_options(arssyut_windows_shortcut_tests PRIVATE
                /W4
                /permissive-
                /utf-8
                /Zc:__cplusplus
            )
        endif()
        add_test(
            NAME arssyut_windows_shortcut_tests
            COMMAND arssyut_windows_shortcut_tests
        )
    endif()
endif()
