# Screamer Remastered

A remaster of *Screamer* (1995), optimised for today's PCs.

Built on [OpenScreamer](https://github.com/ItsDanik/openscreamer) by Danik. This project goes
its own way: it is not affiliated with OpenScreamer and does not run on the MiSTer. Please
report problems here, not to OpenScreamer. *Screamer 2* (1996) runs too, as in OpenScreamer,
without the changes below.

**Beta.** `screamer-remastered --version` prints the version.

## What differs from the original

- Rendering on the graphics card (OpenGL 4.5), resolution up to your screen's
- 16:9 and wider, the instruments at the edges, or 4:3
- Texture filtering and MSAA (2x / 4x / 8x), switchable
- Smooth motion on high refresh rates
- The whole track drawn, as far as it goes
- Window or borderless full screen
- Options in DISPLAY, GRAPHICS and AUDIO
- The CD music straight from GOG's disc image, no disc needed

## You need the original games

No game files are included. *Screamer* is still sold (GOG, Steam). The program finds GOG's and
Steam's copies by itself, or put it (with `SDL2.dll`) into the game's folder. Otherwise give it
the folder that holds `DATA` and `GFX`: `screamer-remastered -data "D:\Games\Screamer"`.

The CD music is played from GOG's disc image. Alternatively, put `track_02.ogg`, `track_03.ogg`
... (or `.mp3` / `.wav`) into a `music` folder inside the game's folder.

## Building

Windows, with [MSYS2](https://www.msys2.org/) (UCRT64):

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -S port -B port/build && cmake --build port/build
port/build/screamer-remastered -data "C:/GOG Games/Screamer"
```

Linux: SDL2, CMake and a C compiler (untested).

`screamer-remastered --help` lists options and keys. Settings and saves go to
`%APPDATA%\screamer-remastered` (the files of an earlier OpenScreamer are taken over once).

## Contents

| | |
|---|---|
| `port/` | the program (C, SDL2, OpenGL) |
| `tools/` | tools for reading the original files and programs |
| `docs/` | notes on both games and the renderer |
| `CLAUDE.md` | project summary |

## Credits

- *Screamer* by Graffiti, *Screamer 2* by Milestone, published by Virgin Interactive. Not
  affiliated with them.
- [OpenScreamer](https://github.com/ItsDanik/openscreamer) by Danik, with Claude (Anthropic).
- Screamer Remastered by Dennis Bohn, with Claude (Anthropic).
- [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT (LGPL 2.1),
  [stb_vorbis](https://github.com/nothings/stb) by Sean Barrett,
  [dr_mp3 / dr_wav](https://github.com/mackron/dr_libs) by David Reid.

## License

GPL-3.0 (see `LICENSE`), covering this source only, not the games. `port/src/nuked/` and
`port/src/third_party/` keep their own licences.
