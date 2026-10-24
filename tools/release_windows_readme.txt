Screamer Remastered @VERSION@ for Windows (64-bit)
==========================================

A remaster of Screamer (Graffiti / Virgin Interactive, 1995), built on
OpenScreamer by Danik. Beta. Please report problems at
https://github.com/dennisbohn/screamer-remastered

YOU NEED THE ORIGINAL GAME
  No game files are included. Screamer is still sold (GOG). The program
  finds an installed copy by itself, where its installer recorded it. Or put
  screamer-remastered.exe into the game's folder (the one that holds DATA and
  GFX), or start it with -data and that folder.

  The CD music is played from GOG's disc image. Alternatively, put
  track_02.ogg, track_03.ogg ... (or .mp3 / .wav) into a "music" folder
  inside the game's folder.

RUN
  Double-click screamer-remastered.exe. Windows 10 or 11, 64-bit.
  (Windows may warn about a program from an unknown publisher: "More info",
  then "Run anyway".)

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
  %APPDATA%\screamer-remastered\   settings, best times, championship, replays,
                                   ghosts, and screamer-remastered.log (look
                                   there if it does not start)

  screamer-remastered.exe --help lists the options.

LICENCE
  GPL-3.0 (LICENSE.txt). The FM chip is Nuked OPL3 by Nuke.YKT, LGPL-2.1 or
  later (LICENSE-nuked-opl3.txt). Built in: SDL2, the Simple DirectMedia
  Layer, zlib licence (LICENSE-SDL2.txt).
  Source: https://github.com/dennisbohn/screamer-remastered
