cmake_minimum_required(VERSION 3.16)
project(screamer-remastered C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL2: on Windows built from its source and linked in (sdl_lean.cmake; OS_SDL_LEAN=OFF takes the
# system's SDL2 instead, as a DLL), elsewhere the system's
option(OS_SDL_LEAN "Windows: SDL2 built lean from its source and linked into the program" ON)
if(WIN32 AND OS_SDL_LEAN)
  include(sdl_lean.cmake)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# -fno-math-errno: sqrtf() and the like as the processor's own instruction
add_compile_options(-Wall -Wextra -O2 -fno-math-errno)

# os_begin.c first and os_end.c last: the program's own statics lie between their marks (the
# state save between the parts takes those alone; the libraries' lie outside)
option(OS_GPU "The GPU renderer (OpenGL 4.5), chosen with renderer=gpu" ON)
option(S2_REFERENCE "Screamer 2 from the converted routines (src/s2_gen.inc, the developer's reference)" OFF)
set(OS_SOURCES
  src/os_begin.c
  src/main.c
  src/mesh.c
  src/unr.c
  src/scene.c
  src/drive.c
  src/sound.c
  src/cdaudio.c
  src/third_party/decoders.c
  src/music.c
  src/fmchip.c
  src/nuked/opl3.c
  src/port.c
  src/controls.c
  src/s2_hooks.c
)
if(OS_GPU)
  list(APPEND OS_SOURCES src/gpu.c)
endif()
if(S2_REFERENCE)
  list(APPEND OS_SOURCES src/s2_exe.c)   # (the menu's data out of the player's STARTL.EXE: the reference's alone)
endif()
list(APPEND OS_SOURCES src/os_end.c)

add_executable(screamer-remastered ${OS_SOURCES})

# the version (../VERSION + commits since, see tools/version.sh), looked up at every build
add_custom_target(os_version
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/os_version.h
          -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/os_version.h)
add_dependencies(screamer-remastered os_version)

target_include_directories(screamer-remastered PRIVATE ${SDL2_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# the GPU renderer (src/gpu.c, docs/GPU.md): OpenGL 4.5 when renderer=gpu; its functions
# come from SDL_GL_GetProcAddress, so nothing more is linked
if(OS_GPU)
  target_compile_definitions(screamer-remastered PRIVATE OS_GPU=1)
endif()
if(WIN32 AND OS_SDL_LEAN)
  target_link_libraries(screamer-remastered PRIVATE SDL2::SDL2main SDL2::SDL2-static m)
else()
  target_link_directories(screamer-remastered PRIVATE ${SDL2_LIBRARY_DIRS})
  target_link_libraries(screamer-remastered PRIVATE ${SDL2_LIBRARIES} m)
endif()
# WINDOWS (MinGW-w64): no console window, nothing of the compiler's own to ship, SDL's unused
# parts dropped, the release without its symbols
if(WIN32)
  target_link_options(screamer-remastered PRIVATE -mwindows -static-libgcc -Wl,--gc-sections
                      $<$<CONFIG:Release>:-s>)
  target_compile_options(screamer-remastered PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

# the decoders of the CD's songs as files (src/third_party: stb_vorbis, dr_mp3, dr_wav) are
# others' code, taken as it is: their warnings are not ours to mend
set_source_files_properties(src/third_party/decoders.c PROPERTIES COMPILE_OPTIONS "-w")
# Screamer 2's race and menu routines: the game's own, written by hand (src/s2r_car.h, docs/NOTES2.md
# S2-8), the sums done on the x87 as the game's are.  S2_REFERENCE (the developer's): instead the
# routines converted to C from the game's program (src/s2_gen.inc, made by tools/s2c.py -- NOT in
# the repository, never published), the reference the hand-written ones are checked against.
target_compile_definitions(screamer-remastered PRIVATE S2_CONVERTED)
if(S2_REFERENCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/s2_gen.inc")
    message(FATAL_ERROR "S2_REFERENCE needs src/s2_gen.inc (tools/s2c.py)")
  endif()
  message(STATUS "Screamer 2: the reference -- the routines converted to C (src/s2_gen.inc)")
  target_compile_definitions(screamer-remastered PRIVATE S2_REFERENCE)
endif()
# the Linux release: pow / exp / log in their first versions, for glibc 2.27 (src/glibc_227.h)
option(OS_GLIBC_227 "Link pow, exp and log to their oldest glibc versions (the Linux release)" OFF)
if(OS_GLIBC_227)
  target_compile_options(screamer-remastered PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/glibc_227.h)
endif()
# the game's words are read both as floats and as whole numbers (F32 / I32 of one address): the
# compiler must not assume the two never meet (it moved a read of a word before the write of it)
set(S2_OPTS "-fno-strict-aliasing")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
  list(APPEND S2_OPTS "-mfpmath=387" "-fexcess-precision=standard")
endif()
set_source_files_properties(src/s2_hooks.c PROPERTIES COMPILE_OPTIONS "${S2_OPTS}")
