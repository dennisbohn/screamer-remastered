# The GPU renderer

OpenGL 4.5 via SDL2, in `port/src/gpu.c`. Built when `OS_GPU` is on, the CMake default, and then
it is what draws: the menus' pictures are shown
through the card, a race's 3D scene is drawn on it -- at the window's own size (RES),
and so is the menu's 3D.

The software renderer is the fall-back, nothing to choose in the menus:

- no OpenGL 4.5 (no context, a shader that does not compile): the software way, as before;
- a start with the card that died before its first race picture: the next start sets
  `renderer=software` in `screamer-remastered.cfg` (a mark, `gpu_starting=1`, is in the file from just
  before the card is first used to the first race picture or an orderly end);
- by hand: `renderer=software` in `screamer-remastered.cfg`, `-renderer software` on the command
  line, `OS_RENDERER=software` for one run (and `gpu` the same way back).

## Status

| Step | What | State |
|---|---|---|
| M0 | build option, `renderer=` switch, fall-back to software | done |
| M1 | the picture through the GPU: `fb` (palette numbers) and the shown palette as textures, one shader scales 4:3 with bars, nearest pixel; vsync; DPI-aware window | done |
| M2 | the 3D scene on the GPU at the original resolution: `tri_job` list, materials 16/64/128/160/224, depth rules; A/B tool against the software renderer | done |
| M3 | the scene at the window's size (RES NATIVE, up to 4K), the menu's 3D too; GRAPHICS page (RES, DISPLAY, DETAIL) | done |
| M3 | FILTERING (a true-colour scene, its textures filtered) and MSAA 2X / 4X / 8X, both on the GRAPHICS page, off by default | done |
| M4 | 16:9 and wider: Screamer's races as wide as the window, the flat things at its edges, the cockpit's sides drawn out, ASPECT WIDE / 4:3 (main.c `pic_follow`, `g_ox`, `cockpit_widen`) | done |
| M5 | draw distance (far plane, keep range, animations started as far as is drawn) | put back: ULTRA is enough for now |
| M6 | interpolation between the 50 Hz physics steps | later |

## The picture's size (M3)

OPTIONS starts with three pages of the port's own (since 2026-10-24): DISPLAY (RES, WINDOW /
FULLSCREEN), GRAPHICS (DETAIL, then FILTERING and MSAA: next section) and AUDIO (the game's
SPEECH and MUSIC); then the game's DIFF., LAP NUMBER and VIEW RECORDS.

- **RES**: the rows the card draws the scene with, in steps -- 480P, 720P, 1080P, 1440P, 2160P,
  2880P, 4320P under the screen's height, and the screen's own last (the default). The scene is
  the window's 4:3 part, so 1440P is 1920 x 1440; a step larger than the window draws at the
  window's size. `resolution=native|<rows>` in `screamer-remastered.cfg` (the old words `svga`,
  `vga`, `low`, `high` are read as native). Without the card there is no RES line: the software
  draws 640 x 400. `OS_RES=svga` (a test, also `-res svga`) draws the scene at fb's own 640 x 400,
  exactly as the software way, pixel for pixel; 320 x 200 (STARTL, the low set) is gone.
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
- **The flat things over it** (`lay_begin`, `lay_dark`, `lay_end` in main.c): the routines
  that put flat pixels down mark them (`LAY_PUT`, `LAY_RUN`); a marked pixel shows fb's colour,
  any other one the card's scene -- as drawn, or through the darkenings done since (the radar's
  window, the dialog, the CD player), each a table (`gpu_overlay`: per pixel 0 the scene, 1
  fb's colour, 2 + k table k). (A pixel fb no longer shows the scene at counts as marked too.)
  The mirror cheats turn the card's scene over as well.
- **The menu's 3D** (`menu3d_begin`, `menu3d_end`): the letters' cubes and the models, on the
  card when the scene is drawn larger. Its see-through material has no `SMG`: the card gets
  `raster_job`'s half and half as a table. A screen being left stands still at its last
  picture, the card's too (`gpu_again`). A list's solid and see-through faces are drawn in
  runs in its own order (a race: all solid ones first).

## FILTERING and MSAA (M3)

Two more lines on the GRAPHICS page, **FILTERING OFF / ON** and **MSAA OFF / 2X / 4X / 8X**
(`filtering=0|1`, `msaa=0|2|4|8` in `screamer-remastered.cfg`; both off by default, the original's
look). Every change shows at once, in the menu's cubes too. Without the card the two lines are
not there; MSAA's line goes as far as the card can. Both off: the pictures are the same as
before, byte for byte (also the larger ones). With either on, the card's scene is what is shown,
at fb's own size too (OS_RES=svga), with the flat things over it as for M3.

- **The palette numbers stay.** The index target keeps exactly what it had (nearest texels,
  `SMG` on numbers): fb is still read back from it for the HUD and the darkenings, and pass 1
  still looks its tables up with it.
- **FILTERING: the scene in colour beside it.** Each frame of each material is a colour layer
  of a 2D array of 256 x 256 (a compute shader makes them from the material bytes and the
  palette, again when either changes; then the smaller copies). A layer is the frame's own
  rectangle (its `su` x `sv`, rows 256 bytes apart from its first byte) and beyond it its edge
  again, so that no other texture's texels come into the filtering; routine 160's is the whole
  256 x 256, as it wraps. Keyed ones (routine 64) have colour 0 as a hole, premultiplied, and
  their holes are taken from the bilinear of level 0 (thin things stay thin far off). The
  texel's place and its steps come from the planes (`u = uz / z`), anisotropic where the card
  has it. Filtering, the smaller copies and a pixel's samples are averaged as light (sRGB); the
  game's own blends stay in the palette's colours.
- **See-through and darkenings as lines.** Pass 1's `SMG[strength][what is there]` and the
  2D darkenings (radar, dialog, CD player) are tables of numbers; for a filtered colour each row
  is turned into a straight line on the colours, per channel, fitted over the palette
  (`rows_fit`; a darkening a factor alone), so nothing brings the palette's steps back into a
  smooth texture. The strength texels of smoke, shadows and glass are mixed four round the
  place, each through its own row's line.
- **MSAA: shaded once a pixel, covered per sample** (since 2026-10-24; before, every sample was
  shaded on its own, eight times the work at 8X: 14 ms a picture at 2880 x 2160, now 3). The
  solid faces and the backdrop are worked out at the pixel's middle, as without MSAA -- planes,
  texel, depth (`gl_FragDepth`), ties and carpets -- and written to the samples the face covers.
  A keyed texture's edge (trees, fences, the gantries' struts) covers as many samples as its four
  texels round the place are not holes, sharpened to one screen pixel (`fwidth`), the samples
  turned round from pixel to pixel: alpha to coverage, `gl_SampleMask` always written (else it is
  undefined: the faces left samples of the last picture behind). Pass 1 (`depth_keep`, `SMG`)
  still works per sample on what is under it. The picture is the mean of a pixel's samples
  (each through the shown palette, or its colour with FILTERING); fb is read back from each
  pixel's first sample. With MSAA no face is left out for being between two pixels' middles
  (`cull_set`): a sample may lie in it.
- **Safety.** FILTERING and MSAA have their own OpenGL functions and shaders: anything missing
  and only they are not there. No room for the targets: fewer samples, then none. A start that
  dies with either on (the mark in `screamer-remastered.cfg`) turns them off, not the card.

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
  picture's first content. The scene then **stays on the card** (since 2026-10-24): the HUD is
  drawn into `fb` as ever, every routine that puts a pixel down marks it (`LAY_PUT`, `LAY_RUN`),
  darkenings over the scene are noted (`lay_dark`), and the card shows its scene wherever no flat
  thing has drawn (`gpu_overlay`, the flat layer: 0 the scene, 1 fb, 1 + k the scene darkened by
  the k-th table) -- at fb's own size too, which is the exact way's picture all the same. `fb`,
  `zb` and `idb` are read back only for the tests' pictures and the developer's keys
  (`gpu_readback`; `OS_READBACK=0|1` over it); F12 saves the card's picture. Without the
  read-back the CPU and the card no longer wait for each other: at 2880 x 2160 the CPU's work is
  0.58 ms a picture whatever the settings, the card's 0.58 ms (both off) or 1.8 ms (FILTERING with
  8X MSAA).
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
  the setting; `OS_RES=native|svga` for one run.
- `OS_GPUSHOTHI=1` with `OS_SHOTEVERY` or `OS_MENU_SHOT`: each picture also as the card shows
  it, at its scene's size (`..._hi.ppm`).
- `OS_NOBLOCKORDER=1`: a car's blocks by depth alone, in both renderers.
- `OS_FILTER=0|1`, `OS_MSAA=0|2|4|8`: FILTERING and MSAA for one run, over the setting.
- `OS_GPUTIME=1`: the card's own time for the backdrop, the faces, the read-back and showing the
  picture, the mean of 120 pictures (timestamp queries).
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
- FILTERING and MSAA (2026-10-24): both off, a robot's race on track 1 at 1x and 3x gives the
  same pictures as before, byte for byte (fb's and the shown ones). FILTERING alone changes fb's
  read-back in 0.1 % of its pixels (the keyed holes' edges, by design). Looked at: trees and
  fences, smoke at the start, the radar's window, the start banner, the sky, the menu's cubes,
  the intro's cards, the mirror cheat, SVGA at 1x. Time for a picture at 2880 x 2160 (robot's
  race, track 1, no vsync, the port's work with the card's wait): both off 2.1 ms, FILTERING
  2.2, MSAA 2X 3.3, 4X 5.5, 8X 11.4, FILTERING with 4X 6.7, with 8X 14.0 (each sample shaded);
  once a pixel (2026-10-24): FILTERING with 2X 2.5, 4X 3.2, 8X 3.0. The card's own times
  (`OS_GPUTIME`), both off: backdrop 0.24, faces 0.32, read back 0.77, shown 0.04 ms -- the
  read-back is the most of it. The textures' colour layers take about 190 MB, MSAA 8X's targets
  at that size about 0.9 GB.
