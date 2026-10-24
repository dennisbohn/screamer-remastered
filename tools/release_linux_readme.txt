Screamer Remastered @VERSION@ for Linux (x86-64)
=======================================

A remaster of Screamer (Graffiti / Virgin Interactive, 1995), built on
OpenScreamer by Danik. Beta. Please report problems at
https://github.com/dennisbohn/screamer-remastered

YOU NEED THE ORIGINAL GAME
  No game files are included. Screamer is still sold (GOG). Put
  screamer-remastered into the game's folder (the one that holds DATA and
  GFX), or start it with --data and that folder:
      ./screamer-remastered --data ~/Games/Screamer
  Upper or lower case in the file names does not matter.

  The CD music is played from GOG's disc image (SCREAM.gog with SCREAM.DAT).
  Alternatively, put track_02.ogg, track_03.ogg ... (or .mp3 / .wav) into a
  "music" folder inside the game's folder.

YOU NEED SDL2
  Any x86-64 Linux of 2018 or later with SDL2 installed:
      Fedora:                 sudo dnf install SDL2
      Debian, Ubuntu, Mint:   sudo apt install libsdl2-2.0-0
      Arch:                   sudo pacman -S sdl2

RUN
      ./screamer-remastered
  If it says "permission denied":  chmod +x screamer-remastered

KEYS
  Menus:  arrows, ENTER, ESC.
  Race:   UP / DOWN accelerate / brake, LEFT / RIGHT steer,
          A / Z gears on cars with manual transmission,
          C next view (F1 bumper, F2 cockpit, F3 near chase, F4 far chase),
          M radar, P the CD player, BACKSPACE back onto the road,
          ESC leave (it asks first), F5 FPS, F11 full screen, F12 screenshot.
  Game controller: left stick or d-pad steers, triggers (or A / B) accelerate
          / brake, shoulders change gear, Y view, X radar, START = ENTER,
          BACK = ESC.

  OPTIONS has DISPLAY, GRAPHICS and AUDIO.

FILES IT KEEPS
  ~/.config/screamer-remastered/   settings, best times, championship, replays,
                                   ghosts (or -home <folder>)

  ./screamer-remastered --help lists the options.

LICENCE
  GPL-3.0 (LICENSE.txt). The FM chip is Nuked OPL3 by Nuke.YKT, LGPL-2.1 or
  later (LICENSE-nuked-opl3.txt).
  Source: https://github.com/dennisbohn/screamer-remastered
