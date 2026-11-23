# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(twtw VERSION 0.7.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# SDL3 and SDL3_ttf: the system's by default; TWTW_VENDOR_SDL builds them from source and links
# them statically (used for the release packages, so they run without SDL installed).
option(TWTW_VENDOR_SDL "Build SDL3 and SDL3_ttf from source and link them statically" OFF)
if(TWTW_VENDOR_SDL)
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
  set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)   # no text shaping needed: the Index draws plain Latin-2 text
  set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
  set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
  set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                       GIT_TAG release-3.4.20 GIT_SHALLOW TRUE)
  FetchContent_Declare(SDL3_ttf GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                       GIT_TAG release-3.2.2 GIT_SHALLOW TRUE GIT_SUBMODULES external/freetype)
  FetchContent_MakeAvailable(SDL3 SDL3_ttf)
else()
  find_package(SDL3 REQUIRED CONFIG)
  find_package(SDL3_ttf REQUIRED CONFIG)
endif()

if(MINGW)
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)   # C99 printf (%zu) also with the old msvcrt
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# Engine core: uses SDL only for portable file I/O (no window), so the tool runs headless.
add_library(twtw_core STATIC
  src/vfs/vfs.c
  src/vfs/iso9660.c
  src/formats/dib.c
  src/formats/ani.c
  src/formats/dkmm.c
  src/formats/ne_res.c
  src/util/bytes.c
  src/text/cp1250.c
)
target_include_directories(twtw_core PUBLIC src)
target_link_libraries(twtw_core PUBLIC SDL3::SDL3)

# The player.
add_executable(twtw src/main.c src/viewer.c src/platform/screen.c src/platform/settings.c src/engine/ani_player.c src/engine/game.c src/ui/font.c)
target_link_libraries(twtw PRIVATE SDL3_ttf::SDL3_ttf)
target_link_libraries(twtw PRIVATE twtw_core)
set_target_properties(twtw PROPERTIES WIN32_EXECUTABLE ON)   # no console window on Windows
if(MINGW)
  target_link_options(twtw PRIVATE -static)                 # no MinGW runtime DLLs to ship
endif()

# Command-line tool for checks and dumps (no window).
add_executable(twtw-tool src/tool.c src/platform/screen.c src/engine/ani_player.c src/engine/game.c src/ui/font.c)
target_link_libraries(twtw-tool PRIVATE SDL3_ttf::SDL3_ttf)
target_link_libraries(twtw-tool PRIVATE twtw_core)
if(MINGW)
  target_link_options(twtw-tool PRIVATE -static)
endif()

# Installation and packages: the two programs, the README, and on Linux a desktop entry.
# No game data is installed or packaged; the user points the player at their own CD image.
include(GNUInstallDirs)
if(WIN32)   # Windows: a flat folder, programs and documents side by side
  install(TARGETS twtw twtw-tool RUNTIME DESTINATION .)
  install(FILES README.md LICENSE DESTINATION .)
else()
  install(TARGETS twtw twtw-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
if(UNIX AND NOT APPLE)
  install(FILES packaging/twtw.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

set(CPACK_PACKAGE_NAME twtw)
set(CPACK_PACKAGE_VENDOR "twtw port")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Player for the 1996 CD-ROM 'Jak to dziala?' (The Way Things Work), C/SDL3 port")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_SYSTEM_NAME windows-x86_64)
else()
  set(CPACK_GENERATOR TGZ)
  set(CPACK_SYSTEM_NAME linux-x86_64)
endif()
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_IGNORE_FILES "/build/;/work/;/third_party/;/\\.git/;\\.iso$")
include(CPack)
