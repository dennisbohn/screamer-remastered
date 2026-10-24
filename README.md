# Screamer Remastered

A remaster of *Screamer* (Graffiti / Virgin Interactive, 1995) and *Screamer 2* (Milestone /
Virgin Interactive, 1996) for today's PCs: drawn by the graphics card at your screen's
resolution, smoothed if you want it, smooth on fast screens, with the CD's music straight from
your copy of the game.

It is built on **[OpenScreamer](https://github.com/ItsDanik/openscreamer)** by Danik, a
reimplementation of both games written by reading the original programs. OpenScreamer keeps to
the original as closely as it can and also runs on the MiSTer FPGA; this project goes its own
way towards a modern remaster. It is not OpenScreamer, it is not affiliated with it, and it does
not run on the MiSTer.

**Beta.** `openscreamer --version` prints the version.

## Thanks

Almost everything that makes the games run here is Danik's work: reading both original programs,
every file format, the driving, the opponents, the cameras, the menus, the championship, the
replays, the sound and the FM music. His commits are kept in this repository's history as he
made them. This project adds to that; it would not exist without it.

If you like what you see, have a look at
[OpenScreamer](https://github.com/ItsDanik/openscreamer) and its
[MiSTer core](https://github.com/ItsDanik/openscreamer_MiSTer), and consider supporting Danik
on [Patreon](https://www.patreon.com/itsdanik).

Please report problems with this project here, not to OpenScreamer.

## What is new here

- **The graphics card draws the races** (OpenGL 4.5) — *Screamer*'s races and its 3D menu at
  the window's own resolution, up to 4K (RES NATIVE). Without OpenGL 4.5 the program draws the
  pictures itself, as before.
- **WINDOW / FULLSCREEN** — full screen is a borderless window at the desktop's resolution; the
  monitor's mode is never changed.
- **FILTERING** (smoothed textures) and **MSAA** 2x / 4x / 8x (smoothed edges), each switchable;
  off is the original's look.
- **Smooth motion on fast screens**: the race still moves in the original's 1/50 s steps, but
  every picture is drawn between the two steps around it, so a 120 Hz or 144 Hz screen shows
  every frame moving (*Screamer*).
- **Screamer's CD music** is played straight from GOG's image of the disc, as *Screamer 2*'s.
- Fixes: the cars' wheels showing through the body, the front wheels steering the wrong way,
  the track's detail set, the letters' spacing at 640 x 400, the nearest palette colour on
  Windows.
- The menu's GRAPHICS page holds all of it: RES, WINDOW / FULLSCREEN, DETAIL, FILTERING, MSAA.

**Planned:** 16:9 with the instruments in the corners and the cockpits extended to the sides;
high-resolution cockpits; a menu split into DISPLAY, GRAPHICS and AUDIO; smooth motion for
*Screamer 2* too. The 320 x 200 mode and the MiSTer code will be removed here — they stay in
OpenScreamer, where they belong.

## You need the original games

This project contains no part of either game. Both are still sold (GOG, Steam): buy them. The
program reads the games' own files as they are; nothing has to be unpacked. Give it the folder
that holds `DATA` and `GFX`:

- GOG's folders work as they are: `openscreamer -data "C:\GOG Games\Screamer"`, or
- copy the folders next to the program, *Screamer*'s as `screamer` and *Screamer 2*'s as
  `screamer2`. With both there, a first page asks which game to play; `-game 1` or `-game 2`
  starts one straight away.

**The music** was the CD's audio tracks. GOG's copies have the disc as an image (`SCREAM.gog` /
`GAME.GOG` with its cue sheet `SCREAM.DAT` / `GAME.DAT`), and it is played from there. Files
named `track_02.ogg` ... (`.mp3` or `.wav` do as well) in a folder `music` inside the game's
folder go first. *Screamer* without its CD tracks plays its FM songs.

## Building

Windows is what this is made and tested on. With [MSYS2](https://www.msys2.org/) (UCRT64):

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -S port -B port/build && cmake --build port/build
port/build/openscreamer -data "C:/GOG Games/Screamer"
```

Linux should build the same way with SDL2, CMake and a C compiler (not tested here).

`openscreamer --help` lists the options, the keys and the game controller's buttons. Settings and
saves go to `%APPDATA%\openscreamer` (Linux: `~/.config/openscreamer`, or `-home DIR`). The
program and its files are still called `openscreamer`; they will be renamed.

## What is in here

| | |
|---|---|
| `port/` | the program (C, SDL2, OpenGL) |
| `tools/` | what was used to read the originals: unpackers for their files, Ghidra scripts, viewers, test helpers |
| `CLAUDE.md` | the summary of the whole project: the working rules, the file formats, where to look |
| `docs/NOTES.md`, `docs/NOTES2.md` | the full record for each game: every file format, where each rule of the game is in the original program, how it was checked, what is exact and what is the port's own |
| `docs/GPU.md` | the graphics card's renderer: how it draws, how it is checked against the original's pictures |
| `VERSION`, `tools/version.sh` | the version: the number in `VERSION`, its last part raised by one with every commit since |

## Credits

- *Screamer* is by Graffiti (later Milestone), *Screamer 2* by Milestone, both published by
  Virgin Interactive Entertainment. This project is not affiliated with them.
- [OpenScreamer](https://github.com/ItsDanik/openscreamer), on which all of this is built: written
  by Danik with Claude (Anthropic); the first day's groundwork with DeepSeek.
- Screamer Remastered: Dennis Bohn with Claude (Anthropic).
- The music's FM chip is [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT
  (`port/src/nuked/`, LGPL 2.1 or later).
- The music files are read with [stb_vorbis](https://github.com/nothings/stb) by Sean Barrett and
  [dr_mp3 / dr_wav](https://github.com/mackron/dr_libs) by David Reid (`port/src/third_party/`,
  public domain or MIT-0).

## License

GPL-3.0, as OpenScreamer; see `LICENSE`. That covers this source only, not the games or their
data. `port/src/nuked/` keeps its own licence (LGPL 2.1 or later), and `port/src/third_party/`
its own (public domain or MIT-0), stated in their files.
