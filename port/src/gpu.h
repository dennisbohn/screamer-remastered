/*
 * gpu.h - THE GRAPHICS CARD DRAWS AND SHOWS THE PICTURES (the fork's GPU renderer, docs/GPU.md):
 * M1 the pictures (palette numbers in fb) turned into colours and scaled to the window; M2 a
 * race's 3D scene drawn from raster_job's triangle list, read back into fb for the HUD; M3 that
 * scene drawn at any size, and the flat things in fb laid over it as the picture is shown, with
 * FILTERING and MSAA if wanted.  Only
 * in a build with OS_GPU, where it is the default (renderer= in screamer-remastered.cfg, -renderer,
 * OS_RENDERER).  Anything missing -- no OpenGL 4.5, a shader that does not compile -- and the
 * software way does it all as before.
 */
#ifndef GPU_H
#define GPU_H

#include <SDL.h>
#include <stdint.h>

void   gpu_hints(void);                 /* before SDL_Init: the screen's own pixels (no stretching by Windows) */
void   gpu_attributes(void);            /* after SDL_Init, before the window: the context wanted */
Uint32 gpu_window_flags(void);          /* what the window must be made with */
int    gpu_open(SDL_Window *win);       /* the context and what the picture needs: 1, or 0 = not here */
int    gpu_active(void);                /* 1: the pictures go through the card */
void   gpu_picture(const uint8_t *pixels, int w, int h, const uint32_t pal[256]);   /* the next picture */
void   gpu_present(void);               /* to the window, 4:3 with bars, and swapped */
int    gpu_check(const uint8_t *pixels, int w, int h, const uint32_t pal[256]);     /* a test: pixels shown otherwise than pal[pixels] (-1: not done) */
void   gpu_area(int *w, int *h);        /* M3: the window's 4:3 part, in its own pixels (what NATIVE draws at) */
void   gpu_shot_next(const char *path); /* a test: the next picture shown also into a PPM, at its scene's size */
/* M2: THE 3D SCENE ON THE CARD -- the triangles raster_job would fill, in the same order */
enum { GPU_KEYED = 1, GPU_GLASS = 2, GPU_WRAPS = 4, GPU_SPRITE = 8 };
typedef struct {
    float x[3], y[3], iz[3], u[3], v[3];    /* the corners: picture pixels, 1 / depth, texels */
    int   offset, avail;                    /* the texture frame's first byte in the material bytes, and the bytes from there */
    int   flat;                             /* a palette number, or -1: the texture */
    int   kind;                             /* GPU_KEYED (colour 0 a hole), GLASS (routine 224), WRAPS (160), SPRITE */
    int   wrap_mask;
    int   id;                               /* the object, -1 none */
    int   rank;                             /* a car's block: its place in the engine's order, -1 none */
    int   mat, frame;                       /* M3: the material (gpu_materials' number) and its frame */
    float zbias;                            /* the whole factor on 1 / depth, material's and object's */
    float ground_vy, eye_h;                 /* a sprite's (over_ground) */
} gpu_tri;

/* M3: the backdrop as sky_paint lays it, for the card to draw at its own size (sky_paint's
   numbers: the picture's column at the left, the roll's cos and sin, picture rows and
   columns to one of the backdrop's, the eye row, the backdrop's top) */
typedef struct {
    const uint8_t *pix;                     /* the backdrop's palette numbers, w x h; NULL: all `clear` */
    int   w, h;
    float off, cr, sr, sx, sy, cy, top;
    int   ground, clear;                    /* the colour under the backdrop; the one without a backdrop */
} gpu_sky;

typedef struct {
    int    w, h, rows;                      /* the picture, and the rows the scene may fill (g_vh) */
    int    rw, rh;                          /* M3: the size the scene is drawn at (w, h: as raster_job) */
    float  cy, focal_y;
    const float *obj_d2;                    /* by object: distance squared, carpet, ground (main.c's tables) */
    const unsigned char *obj_carpet, *obj_ground;
    int    nobj;
    const uint8_t *smg;                     /* the see-through table, 256 a row */
    int    smg_rows;
    int    smg_half;                        /* 1: raster_job's half and half for faces with no table (colour 0 mixed too) */
    int    tie_rule;
    const gpu_sky *sky;                     /* M3: drawn by the card when the scene is larger than fb (NULL: fb's picture) */
    const uint32_t *pal;                    /* M3: the palette fb's numbers are of (the scene's colours) */
} gpu_view;

/* M3: a material: its kind (GPU_KEYED, GLASS, WRAPS), its extent in texels and its frames' first
   bytes (frame_offset[first] on, `frames` of them) */
typedef struct { int kind, su, sv, frames, first; } gpu_mat;

int  gpu_scene_ready(void);                                 /* the scene's shaders are there */
/* the track's material bytes (MAPn.MLB as loaded) and its materials */
void gpu_materials(const uint8_t *bytes, long len, const gpu_mat *mat, int nmat, const int *frame_offset);
/* fb holds the backdrop; fb, zb and idb come back as raster_job would have left them (a scene
   drawn larger: as it is at their pixels' middles).  1: the card's own scene is what is shown
   (drawn larger, FILTERING, MSAA), and the flat things go over it (gpu_overlay) */
int  gpu_scene(const gpu_view *v, const gpu_tri *t, int n, uint8_t *fb, float *zb, int16_t *idb);
/* M3: over the scene drawn last, the next picture's flat things: per pixel of fb 0 the scene,
   1 fb's own colour, 2 + k the scene through table k (luts: n of 256); flip 1 across, 2 down */
void gpu_overlay(const uint8_t *layer, const uint8_t *luts, int n, int flip);
/* M3: the picture shown last once more, its scene and flat things as they were (fb holds it again) */
void gpu_again(void);
/* M3: FILTERING and MSAA -- with either on, the scene is drawn in colour as well as in palette
   numbers and shown as drawn: its textures filtered (mip-mapped, anisotropic), its edges sampled
   msaa times a pixel (0 none, 2, 4, 8).  Both off: as before, pixel for pixel */
void gpu_quality(int filtering, int msaa);
int  gpu_colour(void);                  /* 1: the scenes are the card's own as shown (FILTERING or MSAA on, and possible) */
int  gpu_samples(void);                 /* the samples a pixel the next scene is drawn with (1: none) */
int  gpu_msaa_max(void);                /* the most samples the card can give (0: no FILTERING, no MSAA here) */
void gpu_fades(float fade, uint32_t to, float to_k);   /* the next picture's fades, show_palette's numbers */

#endif
