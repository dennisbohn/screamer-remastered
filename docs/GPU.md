# The GPU renderer (this fork)

OpenGL 4.5 via SDL2, in `port/src/gpu.c`. Built when `OS_GPU` is on, which is the CMake default
everywhere except the MiSTer build. It is used only when asked for:

- `renderer=gpu` in `openscreamer.cfg`
- `-renderer gpu` (or `software`) on the command line
- `OS_RENDERER=gpu` for one run

If OpenGL 4.5 is not available, the software way shows the pictures, as before.

## Status

| Step | What | State |
|---|---|---|
| M0 | build option, `renderer=` switch, fall-back to software | done |
| M1 | the picture through the GPU: `fb` (palette numbers) and the shown palette as textures, one shader scales 4:3 with bars, nearest pixel; vsync; DPI-aware window | done |
| M2 | the 3D scene on the GPU at the original resolution: `tri_job` list, materials 16/64/128/160/224, depth rules; A/B tool against the software renderer | next |
| M5 | draw distance (far plane, keep range, animations started as far as is drawn) | after M2 |
| M3 / M4 | screen resolution up to 4K, 16:9 | later |
| M6 | interpolation between the 50 Hz physics steps | later |

The fork's design notes: the 3D work hooks in at OpenScreamer's triangle list (`raster_list`).
Rendering keeps palette numbers on the GPU until the last step, so fades, the `SMG` blend
tables and the darkenings stay exact.

## Test

`OS_GPUCHECK=<n>`: the first n pictures are drawn once more 1:1 into a texture, read back and
compared with `pal[fb[i]]`. The program prints the differing pixels per picture, then exits:
0 if all matched, 1 otherwise. The window stays hidden.

On Windows 11 with an RX 9070 XT (2026-10-23): menu and intro, races at 320 × 200 and 640 × 400,
1150 pictures, 0 pixels differing.
