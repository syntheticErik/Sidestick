# SPDX-FileCopyrightText: 2026 Erik Evans
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(sidestick VERSION 1.0.0 LANGUAGES CXX)
set(SIDESTICK_APP_ID io.github.syntheticErik.Sidestick)  # reverse-domain ID: .desktop and icon names

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
find_package(Threads REQUIRED)

# Bundled RtMidi, built with the platform's native MIDI backend.
add_library(rtmidi STATIC third_party/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi SYSTEM PUBLIC third_party/rtmidi)
target_link_libraries(rtmidi PUBLIC Threads::Threads)
if(APPLE)
    target_compile_definitions(rtmidi PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
    target_compile_definitions(rtmidi PRIVATE __WINDOWS_MM__)
    target_link_libraries(rtmidi PUBLIC winmm)
else()
    find_package(ALSA REQUIRED)
    target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA)
endif()

set(LMD_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

add_library(lmdcore STATIC
    src/core/protocol.cpp
    src/core/model.cpp
    src/core/sysexfile.cpp
    src/core/audio.cpp
    src/core/processing.cpp
    src/core/device.cpp
)
target_include_directories(lmdcore PUBLIC src/core)
target_link_libraries(lmdcore PUBLIC rtmidi PkgConfig::SNDFILE PkgConfig::SAMPLERATE)
target_compile_options(lmdcore PRIVATE ${LMD_WARNINGS})

add_executable(sidestick-cli src/cli/main.cpp)
install(TARGETS sidestick-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
target_link_libraries(sidestick-cli PRIVATE lmdcore)
target_compile_options(sidestick-cli PRIVATE ${LMD_WARNINGS})

# Tests use private device captures, so they only exist in the development tree.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_main.cpp)
    enable_testing()
    add_executable(lmdcore_tests tests/test_main.cpp)
    target_link_libraries(lmdcore_tests PRIVATE lmdcore)
    target_compile_options(lmdcore_tests PRIVATE ${LMD_WARNINGS})
    target_compile_definitions(lmdcore_tests PRIVATE LMD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME lmdcore_tests COMMAND lmdcore_tests)
endif()

# Qt 6 desktop app (optional: the library, CLI, and tests build without Qt).
find_package(Qt6 COMPONENTS Widgets Multimedia QUIET)
if(Qt6_FOUND)
    add_executable(sidestick src/gui/main.cpp src/gui/mainwindow.cpp src/gui/devicerunner.cpp src/gui/dndtree.h
        src/gui/uploaddialog.cpp src/gui/previewplayer.cpp src/gui/waveformview.cpp
        src/gui/icons.h assets/sidestick.qrc)
    set_target_properties(sidestick PROPERTIES AUTORCC ON)
    target_link_libraries(sidestick PRIVATE lmdcore Qt6::Widgets Qt6::Multimedia)
    target_compile_options(sidestick PRIVATE ${LMD_WARNINGS})
    target_compile_definitions(sidestick PRIVATE SIDESTICK_VERSION="${PROJECT_VERSION}" SIDESTICK_APP_ID="${SIDESTICK_APP_ID}")

    # Desktop integration: menu entry and themed icons (freedesktop hicolor).
    install(TARGETS sidestick RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Absolute Exec works for installs whose bin dir isn't on PATH (e.g. ~/.local); packages
    # (AppImage, distro packages) want the plain name: -DSIDESTICK_DESKTOP_EXEC_ABSOLUTE=OFF.
    option(SIDESTICK_DESKTOP_EXEC_ABSOLUTE "Use the full install path in the .desktop Exec line" ON)
    if(SIDESTICK_DESKTOP_EXEC_ABSOLUTE)
        set(SIDESTICK_DESKTOP_EXEC ${CMAKE_INSTALL_FULL_BINDIR}/sidestick)
    else()
        set(SIDESTICK_DESKTOP_EXEC sidestick)
    endif()
    configure_file(assets/sidestick.desktop.in ${CMAKE_BINARY_DIR}/${SIDESTICK_APP_ID}.desktop @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/${SIDESTICK_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 22 24 32 48 64 128 256 512)
        install(FILES assets/icons/hicolor/${size}x${size}/apps/sidestick.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME ${SIDESTICK_APP_ID}.png)
    endforeach()
    install(FILES assets/icons/sidestick.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${SIDESTICK_APP_ID}.svg)
else()
    message(STATUS "Qt6 Widgets/Multimedia not found: skipping sidestick")
endif()
