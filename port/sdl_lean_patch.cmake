# run in SDL's source (sdl_lean.cmake): its dynamic API off -- linked into the program, it would
# only keep every SDL function in it.  SDL wants this switched in the header itself.
set(h src/dynapi/SDL_dynapi.h)
file(READ ${h} s)
if(NOT s MATCHES "screamer-remastered")
  string(REPLACE "/* everyone else." "#define SDL_DYNAMIC_API 0 /* screamer-remastered */\n/* everyone else." s "${s}")
  file(WRITE ${h} "${s}")
endif()
