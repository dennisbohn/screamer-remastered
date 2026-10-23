/*
 * gpu.h - THE GRAPHICS CARD DRAWS AND SHOWS THE PICTURES (the fork's GPU renderer, docs/GPU.md):
 * M1 the pictures (palette numbers in fb) turned into colours and scaled to the window; M2 a
 * race's 3D scene drawn from raster_job's triangle list, read back into fb for the HUD.  Only in
 * a build with OS_GPU, where it is the default (renderer= in openscreamer.cfg, -renderer,
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
/* M2: THE 3D SCENE ON THE CARD -- the triangles raster_job would fill, in the same order */
enum { GPU_KEYED = 1, GPU_GLASS = 2, GPU_WRAPS = 4, GPU_SPRITE = 8 };
typedef struct {
    float x[3], y[3], iz[3], u[3], v[3];    /* the corners: picture pixels, 1 / depth, texels */
    int   offset, avail;                    /* the texture frame's first byte in the material bytes, and the bytes from there */
    int   flat;                             /* a palette number, or -1: the texture */
    int   kind;                             /* GPU_KEYED (colour 0 a hole), GLASS (routine 224), WRAPS (160), SPRITE */
    int   wrap_mask;
    int   id;                               /* the object, -1 none */
    float zbias;                            /* the whole factor on 1 / depth, material's and object's */
    float ground_vy, eye_h;                 /* a sprite's (over_ground) */
} gpu_tri;

typedef struct {
    int    w, h, rows;                      /* the picture, and the rows the scene may fill (g_vh) */
    float  cy, focal_y;
    const float *obj_d2;                    /* by object: distance squared, carpet, ground (main.c's tables) */
    const unsigned char *obj_carpet, *obj_ground;
    int    nobj;
    const uint8_t *smg;                     /* the see-through table, 256 a row */
    int    smg_rows;
    int    tie_rule;
} gpu_view;

int  gpu_scene_ready(void);                                 /* the scene's shaders are there */
void gpu_materials(const uint8_t *bytes, long len);         /* the track's material bytes (MAPn.MLB as loaded) */
/* fb holds the backdrop; fb, zb and idb come back as raster_job would have left them */
void gpu_scene(const gpu_view *v, const gpu_tri *t, int n, uint8_t *fb, float *zb, int16_t *idb);

#endif
