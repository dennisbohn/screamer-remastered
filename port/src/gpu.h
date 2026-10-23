/*
 * gpu.h - THE GRAPHICS CARD SHOWS THE PICTURE (the fork's GPU renderer; this is its step M1:
 * the picture is still made as before, palette numbers in fb, and the card turns them into
 * colours and scales them to the window).  Only in a build with OS_GPU, and only when asked
 * for: renderer=gpu in openscreamer.cfg, -renderer gpu, or OS_RENDERER=gpu for one run.
 * Anything missing -- no OpenGL 4.5, a shader that does not compile -- and the software way
 * shows the pictures as before.
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

#endif
