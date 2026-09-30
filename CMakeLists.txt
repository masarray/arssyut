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

add_library(arssyut_core STATIC
    src/core/session/session_state_machine.cpp
    src/core/time/monotonic_clock.cpp
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
        src/platform/windows/graphics/d3d11_compositor.cpp
        src/platform/windows/graphics/d3d11_device.cpp
        src/platform/windows/storage/recoverable_session.cpp
        src/platform/windows/video/native_video_pipeline.cpp
    )

    target_include_directories(arssyut_windows
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_definitions(arssyut_windows PRIVATE
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
            windowsapp
    )
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
    endif()
endif()
