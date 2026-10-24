# Screamer Remastered

A remaster of *Screamer* (1995) and *Screamer 2* (1996), optimised for today's PCs.

Built on [OpenScreamer](https://github.com/ItsDanik/openscreamer) by Danik. This project goes
its own way: it is not affiliated with OpenScreamer and does not run on the MiSTer.

**Beta.** `openscreamer --version` prints the version.

## Thanks

Almost everything that makes the games run is Danik's work in OpenScreamer; his commits are kept
in this repository's history. If you like it, have a look at
[OpenScreamer](https://github.com/ItsDanik/openscreamer) and its
[MiSTer core](https://github.com/ItsDanik/openscreamer_MiSTer), and consider supporting Danik on
[Patreon](https://www.patreon.com/itsdanik).

Please report problems with this project here, not to OpenScreamer.

## What is new

- Rendering on the graphics card (OpenGL 4.5), up to 4K
- Window or borderless full screen
- Texture filtering and MSAA (2x / 4x / 8x), switchable
- Smooth motion on high refresh rates (*Screamer*)
- *Screamer*'s CD music from GOG's disc image
- Fixes: wheels showing through the body, front wheels steering the wrong way, the track's
  detail set, text spacing at 640 x 400, palette colours on Windows

**Planned:** 16:9, high-resolution cockpits, a reorganised options menu, smooth motion for
*Screamer 2*. The 320 x 200 mode and the MiSTer code will be removed.

## You need the original games

No game files are included. Both games are still sold (GOG, Steam). Point the program at the
folder that holds `DATA` and `GFX`, e.g. `openscreamer -data "C:\GOG Games\Screamer"`, or copy
the folders next to the program as `screamer` and `screamer2`.

The CD music is played from GOG's disc image. Alternatively, put `track_02.ogg`, `track_03.ogg`
... (or `.mp3` / `.wav`) into a `music` folder inside the game's folder.

## Building

Windows, with [MSYS2](https://www.msys2.org/) (UCRT64):

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -S port -B port/build && cmake --build port/build
port/build/openscreamer -data "C:/GOG Games/Screamer"
```

Linux: SDL2, CMake and a C compiler (untested).

`openscreamer --help` lists options and keys. Settings and saves go to `%APPDATA%\openscreamer`.

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
