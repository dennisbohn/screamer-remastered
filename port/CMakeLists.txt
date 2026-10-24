cmake_minimum_required(VERSION 3.16)
project(screamer-remastered C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# -fno-math-errno: sqrtf() and the like as the processor's own instruction
add_compile_options(-Wall -Wextra -O2 -fno-math-errno)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(OS_SOURCES
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
option(OS_GPU "The GPU renderer (OpenGL 4.5), chosen with renderer=gpu" ON)
if(OS_GPU)
  target_sources(screamer-remastered PRIVATE src/gpu.c)
  target_compile_definitions(screamer-remastered PRIVATE OS_GPU=1)
endif()
target_link_directories(screamer-remastered PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(screamer-remastered PRIVATE ${SDL2_LIBRARIES} m)
if(WIN32)
  # WINDOWS (MinGW-w64, tools/release_windows.sh): no console window, and
  # nothing of the compiler's own to ship beside SDL2.dll.  SDL2 must stay a
  # DLL: linked in, its statics would be among those port.c puts back
  target_link_options(screamer-remastered PRIVATE -mwindows -static-libgcc)
  target_compile_options(screamer-remastered PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

# the decoders of the CD's songs as files (src/third_party: stb_vorbis, dr_mp3, dr_wav) are
# others' code, taken as it is: their warnings are not ours to mend
set_source_files_properties(src/third_party/decoders.c PROPERTIES COMPILE_OPTIONS "-w")
# Screamer 2's race and menu routines: the game's own, written by hand (src/s2r_car.h, docs/NOTES2.md
# S2-8), the sums done on the x87 as the game's are.  S2_REFERENCE (the developer's): instead the
# routines converted to C from the game's program (src/s2_gen.inc, made by tools/s2c.py -- NOT in
# the repository, never published), the reference the hand-written ones are checked against.
option(S2_REFERENCE "Screamer 2 from the converted routines (src/s2_gen.inc, the developer's reference)" OFF)
target_compile_definitions(screamer-remastered PRIVATE S2_CONVERTED)
if(S2_REFERENCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/s2_gen.inc")
    message(FATAL_ERROR "S2_REFERENCE needs src/s2_gen.inc (tools/s2c.py)")
  endif()
  message(STATUS "Screamer 2: the reference -- the routines converted to C (src/s2_gen.inc)")
  target_compile_definitions(screamer-remastered PRIVATE S2_REFERENCE)
  target_sources(screamer-remastered PRIVATE src/s2_exe.c)   # (the menu's data out of the player's STARTL.EXE: the reference's alone)
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
