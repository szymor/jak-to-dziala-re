cmake_minimum_required(VERSION 3.20)
project(twtw VERSION 0.6.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)

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

# Command-line tool for checks and dumps (no window).
add_executable(twtw-tool src/tool.c src/platform/screen.c src/engine/ani_player.c src/engine/game.c src/ui/font.c)
target_link_libraries(twtw-tool PRIVATE SDL3_ttf::SDL3_ttf)
target_link_libraries(twtw-tool PRIVATE twtw_core)

# Installation and packages: the two programs, the README, and on Linux a desktop entry.
# No game data is installed or packaged; the user points the player at their own CD image.
include(GNUInstallDirs)
install(TARGETS twtw twtw-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(UNIX AND NOT APPLE)
  install(FILES packaging/twtw.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

set(CPACK_PACKAGE_NAME twtw)
set(CPACK_PACKAGE_VENDOR "twtw port")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Player for the 1996 CD-ROM 'Jak to dziala?' (The Way Things Work), C/SDL3 port")
set(CPACK_GENERATOR TGZ ZIP)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_IGNORE_FILES "/build/;/work/;/third_party/;/\\.git/;\\.iso$")
include(CPack)
