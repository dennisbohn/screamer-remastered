# sdl_lean.cmake -- SDL2 for Windows, built from its source with what the program uses alone and
# linked into it (no SDL2.dll): no Direct3D 11 / 12, OpenGL ES, Vulkan, haptics, sensors, power,
# locale, file system, no dynamic API; its blitters cut to the plain ones (SDL_LEAN_AND_MEAN), the
# software renderer kept for the tests.  The source is fetched once into the build directory.
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)   # (the unpacked files dated when unpacked)
endif()
FetchContent_Declare(sdl2_lean
  URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
  URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
  PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/sdl_lean_patch.cmake)

foreach(opt SDL_SHARED SDL_TEST SDL_HAPTIC SDL_POWER SDL_SENSOR SDL_LOCALE SDL_MISC SDL_FILESYSTEM
            SDL_VULKAN SDL_OPENGLES)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(HAVE_D3D11_H OFF CACHE INTERNAL "")
set(HAVE_D3D12_H OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(sdl2_lean)

target_compile_definitions(SDL2-static PRIVATE SDL_LEAN_AND_MEAN=1 SDL_VIDEO_RENDER_SW=1)
target_compile_options(SDL2-static PRIVATE -Os -ffunction-sections -fdata-sections)
