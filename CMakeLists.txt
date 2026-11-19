cmake_minimum_required(VERSION 3.20)
project(twtw C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED CONFIG)

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
)
target_include_directories(twtw_core PUBLIC src)
target_link_libraries(twtw_core PUBLIC SDL3::SDL3)

# The player.
add_executable(twtw src/main.c src/platform/screen.c src/engine/ani_player.c)
target_link_libraries(twtw PRIVATE twtw_core)

# Command-line tool for checks and dumps (no window).
add_executable(twtw-tool src/tool.c)
target_link_libraries(twtw-tool PRIVATE twtw_core)
