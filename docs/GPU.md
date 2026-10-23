# The GPU renderer (this fork)

OpenGL 4.5 via SDL2, in `port/src/gpu.c`. Built when `OS_GPU` is on, which is the CMake default
everywhere except the MiSTer build, and then it is what draws: the menus' pictures are shown
through the card, a race's 3D scene is drawn on it -- at the window's own size (RES NATIVE),
and so is the menu's 3D.

The software renderer is the fall-back, nothing to choose in the menus:

- no OpenGL 4.5 (no context, a shader that does not compile): the software way, as before;
- a start with the card that died before its first race picture: the next start sets
  `renderer=software` in `openscreamer.cfg` (a mark, `gpu_starting=1`, is in the file from just
  before the card is first used to the first race picture or an orderly end);
- by hand: `renderer=software` in `openscreamer.cfg`, `-renderer software` on the command
  line, `OS_RENDERER=software` for one run (and `gpu` the same way back).

## Status

| Step | What | State |
|---|---|---|
| M0 | build option, `renderer=` switch, fall-back to software | done |
| M1 | the picture through the GPU: `fb` (palette numbers) and the shown palette as textures, one shader scales 4:3 with bars, nearest pixel; vsync; DPI-aware window | done |
| M2 | the 3D scene on the GPU at the original resolution: `tri_job` list, materials 16/64/128/160/224, depth rules; A/B tool against the software renderer | done |
| M3 | the scene at the window's size (RES NATIVE, up to 4K), the menu's 3D too; GRAPHICS page (RES, DISPLAY, DETAIL) | first step done |
| M3 | texture filtering, to be switched on and off (a true-colour scene); MSAA later | next |
| M4 | 16:9 and wider | later |
| M5 | draw distance (far plane, keep range, animations started as far as is drawn) | put back: ULTRA is enough for now |
| M6 | interpolation between the 50 Hz physics steps | later |

## The picture's size (M3)

OPTIONS has one line GRAPHICS in the place of DETAIL and RESOLUTION; its page has three:

- **RES NATIVE** (the default): the card draws the scene at the size of the window's 4:3 part,
  in its own pixels (a 4K screen in full: 2880 x 2160). **RES VGA** and **RES SVGA**: the
  original's two, 320 x 200 and 640 x 400, for its look -- exactly as before, pixel for pixel.
  NATIVE and SVGA use the high set's flat artwork, VGA the low set's (a change to or from VGA
  starts the menu again, as the resolution line did). `resolution=native|svga|vga` in
  `openscreamer.cfg` (the old `low` is VGA, `high` NATIVE); `-res native|svga|vga`.
- **WINDOW / FULLSCREEN**: the whole screen as the desktop has it, never another screen mode,
  so that no setting can leave the screen black; F11 the same. `display=window|fullscreen`.
- **DETAIL**: the game's (and ULTRA), as before.

Every change shows at once. How a larger scene is drawn:

- **The CPU's work stays at fb's size.** The triangles and their planes are worked out in fb's
  pixels as for M2; the fragment shaders take the pixel's middle in fb's units
  (`gl_FragCoord / u_factor`, 1 at fb's size). Only `emit_face`'s test for faces that hit no
  pixel's middle counts the card's pixels (`g_cull_fx`, `g_cull_fy`), or small far things
  would be missing.
- **The backdrop**: fb's picture made larger (the menu's background), then the sky drawn by a
  shader with `sky_paint`'s numbers at each of the card's pixels.
- **fb gets the scene back at its own size**: targets of fb's size, each pixel as the scene is
  at the middle of fb's (a blit, nearest), so that the HUD and everything else works on fb as
  ever.
- **The flat things over it** (`lay_begin`, `lay_dark`, `lay_end` in main.c): where fb still
  shows the scene after the HUD is drawn, the card shows its own; where fb shows the scene
  through a darkening (the radar's window, the dialog, the CD player), the card's scene goes
  through the same table (`gpu_overlay`: per pixel 0 the scene, 1 fb's colour, 2 + k table k).
  The mirror cheats turn the card's scene over as well.
- **The menu's 3D** (`menu3d_begin`, `menu3d_end`): the letters' cubes and the models, on the
  card when the scene is drawn larger. Its see-through material has no `SMG`: the card gets
  `raster_job`'s half and half as a table. A screen being left stands still at its last
  picture, the card's too (`gpu_again`). A list's solid and see-through faces are drawn in
  runs in its own order (a race: all solid ones first).

## How the scene is drawn (M2)

- **The same triangles.** `raster_list` hands every triangle of a scene to `gpu_list` instead of
  the filling threads, with what `raster_job` would work out for it: the texture frame, the
  material's kind, the depth factor. The menus' 3D stays with the software renderer at fb's own
  size (larger: M3).
- **The same sums.** For each triangle the CPU works out `raster_job`'s planes (1/z, u/z, v/z
  and their steps across and down, from the first corner) with the same expressions in the
  same order. The fragment shaders evaluate them at the pixel's centre with `precise` (nothing
  fused), take the texel as `raster_job` does (16.16, clamped or wrapped, the frame's bytes),
  and write the depth themselves: `gl_FragDepth = 1/z x factor x 8`, larger is nearer, `GEQUAL`
  so that a later face wins a tie, as `!(mine < there)`.
- **Palette numbers to the end.** The targets are an `R8UI` picture, the software's depth
  (`R32F`) and the owner (`R16I`); the backdrop is painted by the CPU as before and is the
  picture's first content. Afterwards `fb`, `zb` and `idb` are read back and the HUD is drawn
  on them as ever, so fades, the `SMG` blends and the 2D darkenings stay exact.
- **Depth rules.** Pass 0 (everything solid) uses factors on the card's depth: a carpet counts
  as twice as far (half its depth), and objects get a hair's nudge by their distance rank so
  that on a tie the one nearer the camera wins (objects at the same distance get the same
  nudge: the later face wins, as in `raster_job`). A car's blocks come in the engine's order,
  the last first, each block its own draw: the stencil holds the block's place, and a block
  does not paint over the pixels of the car's later ones (`raster_job`'s `rkb`: the tyres
  first, the body over them). Pass 1 (routine 224: smoke, glass, shadows, beams) runs
  `depth_keep` itself against pass 0's depth and owner, then `SMG[strength][what is there]`;
  triangles that overlap are drawn in turn with `glTextureBarrier` between them.

What still differs from the software renderer, by design:

- the software works the texture's place out exactly every 8th pixel and runs a straight line
  between (one division for eight pixels); the card does it at every pixel. `OS_UVRUN=1` makes
  the software do the same, for comparing;
- a pixel exactly on an edge: the card's rule (one of the two triangles) against the software's
  (both, the later wins);
- faces that lie in one plane and overlap (a decal on a decal of the same object, a lamp's arm
  in its own beam): in both renderers the rounding decides, and differently.

## Tests

- `OS_GPUCHECK=<n>`: the first n pictures are drawn once more 1:1 into a texture, read back and
  compared with `pal[fb[i]]`; the program prints the differing pixels per picture, then exits
  with 0 if all matched. The window stays hidden.
- `tools/gpu_ab.py`: both renderers, the same pictures, pixel by pixel -- views along every
  track (also reversed) and pictures of a robot's race (cars, smoke). A differing pixel whose
  colour is a neighbour's in the other picture is counted as an edge, the rest as odd; sheets
  (software | card | differences) for pictures with odd pixels.
- `OS_GPUPIXEL=x,y,n`: in the n-th scene, what is at that pixel and every triangle over it
  (owner, kind, depth on the card).
- `OS_GPU3D=0`: the race drawn by the software again, the pictures still shown by the card.
- `OS_GPURES=native|<w>x<h>|<n>`: the size the card draws the scene at (n: n times fb's), over
  the setting; `OS_RES=native` for RES NATIVE in one run.
- `OS_GPUSHOTHI=1` with `OS_SHOTEVERY` or `OS_MENU_SHOT`: each picture also as the card shows
  it, at its scene's size (`..._hi.ppm`).
- `OS_NOBLOCKORDER=1`: a car's blocks by depth alone, in both renderers.
- `OS_GPUPUFFS=1`: the scenes with smoke in them (how many of the puffs' triangles).

On Windows 11 with an RX 9070 XT (2026-10-23):

- M1: menu and intro, races at 320 x 200 and 640 x 400, 1150 pictures, 0 pixels differing.
- M2 (`tools/gpu_ab.py`, all twelve tracks, 30 views and 20 race pictures each, both
  resolutions): 1200 pictures, 0.025 % of their pixels differ, 84 % of those at edges. The
  rest are single pixels and short runs where a texel's border falls a hair otherwise (walls
  and ceilings seen at a grazing angle, a texture stretched far), and the faces in one plane
  listed above. Smoke over the road, the wheels in their arches, the carpets and Sandrock's
  hotel by the start show no difference beyond edges (the hotel: 100 pictures of the start,
  0.011 %).
- Time for a picture at 640 x 400 ULTRA, track 1, robot's race, no vsync: 1.72 ms with the
  software's scene, 1.59 ms with the card's (`OS_NOVSYNC=1`, a test switch). With vsync the
  card's wait for the screen falls into the reading back.
- 2026-10-24, with a car's blocks in the engine's order (both renderers) and one nudge for
  one distance: the same 1200 pictures, 0.024 % differ, 84 % of those at edges. Under the
  bridge on track 1 a picture had differed in 5039 pixels (two pieces in one place with
  different textures), now 39. The extra draws for the cars' blocks cost nothing measurable
  (about 1.8 ms for the card's scene, 2.0 ms for the software's, the same run).
- M3, the scene drawn larger: at fb's own size the pictures are the same as before, pixel for
  pixel (a replay of track 1). Drawn three times larger and read back, they differ from the
  one-to-one pictures in 0.012 % of their pixels (the card's middle pixel of each three is fb's
  middle): the same sums at the same places. Pictures at 2880 x 2160 and the menu at 1920 x
  1440 looked at: the HUD, the radar's darkened window, the replay's marks, the menu's cubes
  and cars.
