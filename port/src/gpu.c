/*
 * gpu.c - THE GRAPHICS CARD SHOWS THE PICTURE (the fork's GPU renderer, step M1; plan: docs/GPU.md).
 *
 * The picture is made as before (palette numbers in fb); the card turns it into colours: the
 * numbers go up as an 8-bit texture, the shown palette (fades included) as 256 colours, and a
 * shader fills the window 4:3 with bars, nearest pixel, as SDL_RenderSetLogicalSize(960, 720).
 * OpenGL 4.5 core, its functions from SDL_GL_GetProcAddress: nothing to link; without it the
 * software way shows the pictures.  The state outlives the parts (os_slot 11, os_keep_calloc).
 */
#include "gpu.h"
#include "port.h"

#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPU_SLOT 11

/* the functions used: return type, name without "gl", arguments */
#define GPU_GL(X) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum))

typedef struct {
    SDL_Window   *win;
    SDL_GLContext ctx;
#define GPU_FIELD_(ret, name, args) ret (APIENTRY *name) args;
    GPU_GL(GPU_FIELD_)
#undef GPU_FIELD_
    GLuint prog, vao, picture, palette;
    GLint  u_size, u_area;
    int    pic_w, pic_h;            /* the picture texture's size; made again when the picture's changes */
} gpu_state;

static gpu_state *G;                /* (put back with a part's statics: found again in the slot) */

static gpu_state *gpu(void)
{
    if (!G && os_slot(GPU_SLOT)) G = os_slot(GPU_SLOT);
    return G;
}

int gpu_active(void) { return gpu() != NULL; }

/* a corner of one triangle that covers the whole viewport (no vertex data: gl_VertexID) */
static const char *const g_vertex_src =
    "#version 450 core\n"
    "void main() {\n"
    "    const vec2 corner[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));\n"
    "    gl_Position = vec4(corner[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

/* the picture pixel under this screen pixel (the nearest), and its colour from the palette */
static const char *const g_fragment_src =
    "#version 450 core\n"
    "layout(binding = 0) uniform usampler2D picture;\n"     /* palette numbers, row 0 at the top */
    "layout(binding = 1) uniform sampler2D palette;\n"      /* 256 x 1, as shown (the fades in it) */
    "uniform ivec2 size;\n"                                 /* the picture's columns and rows */
    "uniform vec4 area;\n"                                  /* where it goes: x, y from the bottom, width, height */
    "out vec4 colour;\n"
    "void main() {\n"
    "    vec2 at = (gl_FragCoord.xy - area.xy) / area.zw;\n"
    "    ivec2 p = clamp(ivec2(floor(vec2(at.x, 1.0 - at.y) * vec2(size))), ivec2(0), size - 1);\n"
    "    uint n = texelFetch(picture, p, 0).r;\n"
    "    colour = texelFetch(palette, ivec2(int(n), 0), 0);\n"
    "}\n";

static GLuint shader(GLenum kind, const char *src)
{
    const GLuint sh = G->CreateShader(kind);
    GLint ok = 0;
    G->ShaderSource(sh, 1, &src, NULL);
    G->CompileShader(sh);
    G->GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        G->GetShaderInfoLog(sh, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "gpu: a shader did not compile:\n%s\n", log);
        G->DeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint program(void)
{
    const GLuint vs = shader(GL_VERTEX_SHADER, g_vertex_src), fs = shader(GL_FRAGMENT_SHADER, g_fragment_src);
    if (!vs || !fs) return 0;
    const GLuint p = G->CreateProgram();
    GLint ok = 0;
    G->AttachShader(p, vs);
    G->AttachShader(p, fs);
    G->LinkProgram(p);
    G->DeleteShader(vs);
    G->DeleteShader(fs);
    G->GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        G->GetProgramInfoLog(p, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "gpu: the shaders did not link:\n%s\n", log);
        return 0;
    }
    return p;
}

/* a texture read with texelFetch only: no filtering, no smaller copies (an integer texture with
   the default filter would be incomplete, and read as 0) */
static GLuint texture(void)
{
    GLuint t = 0;
    G->GenTextures(1, &t);
    G->BindTexture(GL_TEXTURE_2D, t);
    G->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    G->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    G->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    G->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    G->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void gpu_hints(void)
{
    /* the window's pixels are the screen's: without this Windows stretches the picture of a
       program that says nothing about it (a 4K screen at 200 %: everything blurred twice).
       SDL keeps the window's sizes as they were; the drawable is the real one. */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
}

void gpu_attributes(void)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
}

Uint32 gpu_window_flags(void)
{
    /* (OS_GPUCHECK, a test: no window to be seen) */
    return SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (getenv("OS_GPUCHECK") ? SDL_WINDOW_HIDDEN : 0);
}

int gpu_open(SDL_Window *win)
{
    if (gpu()) return 1;
    gpu_state *s = os_keep_calloc(1, sizeof *s);
    if (!s) return 0;
    s->win = win;
    s->ctx = SDL_GL_CreateContext(win);
    if (!s->ctx) {
        fprintf(stderr, "gpu: no OpenGL 4.5 here (%s) -- the software way shows the pictures\n", SDL_GetError());
        free(s);
        return 0;
    }
    const char *missing = NULL;
#define GPU_LOAD_(ret, name, args) \
    s->name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name); \
    if (!s->name && !missing) missing = "gl" #name;
    GPU_GL(GPU_LOAD_)
#undef GPU_LOAD_
    G = s;
    if (!missing) {
        s->prog = program();
        s->u_size = s->prog ? G->GetUniformLocation(s->prog, "size") : -1;
        s->u_area = s->prog ? G->GetUniformLocation(s->prog, "area") : -1;
        G->GenVertexArrays(1, &s->vao);             /* (the core profile draws nothing without one) */
        s->picture = texture();
        s->palette = texture();
        G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    }
    if (missing || !s->prog || G->GetError() != GL_NO_ERROR) {
        if (missing) fprintf(stderr, "gpu: OpenGL has no %s\n", missing);
        fprintf(stderr, "gpu: the software way shows the pictures\n");
        SDL_GL_DeleteContext(s->ctx);
        free(s);
        G = NULL;
        return 0;
    }
    os_slot_set(GPU_SLOT, s);
    const int vsync = SDL_GL_SetSwapInterval(1) == 0;
    printf("display: OpenGL %s on %s, vsync %s\n", (const char *)G->GetString(GL_VERSION),
           (const char *)G->GetString(GL_RENDERER), vsync ? "on" : "NOT given by the driver");
    return 1;
}

void gpu_picture(const uint8_t *pixels, int w, int h, const uint32_t pal[256])
{
    if (!gpu() || w <= 0 || h <= 0) return;
    G->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    G->BindTexture(GL_TEXTURE_2D, G->picture);
    if (w != G->pic_w || h != G->pic_h) {
        G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
        G->pic_w = w; G->pic_h = h;
    }
    G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
    /* (the palette as SDL's ARGB8888 has it: 0xAARRGGBB, read the way it lies in memory) */
    G->BindTexture(GL_TEXTURE_2D, G->palette);
    G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pal);
}

/* the picture into the viewport x, y, w, h (window pixels, y from the bottom) */
static void draw(int x, int y, int w, int h)
{
    G->Viewport(x, y, w, h);
    G->UseProgram(G->prog);
    G->Uniform2i(G->u_size, G->pic_w, G->pic_h);
    G->Uniform4f(G->u_area, (float)x, (float)y, (float)w, (float)h);
    G->ActiveTexture(GL_TEXTURE0);
    G->BindTexture(GL_TEXTURE_2D, G->picture);
    G->ActiveTexture(GL_TEXTURE1);
    G->BindTexture(GL_TEXTURE_2D, G->palette);
    G->BindVertexArray(G->vao);
    G->DrawArrays(GL_TRIANGLES, 0, 3);
}

void gpu_present(void)
{
    if (!gpu()) return;
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(G->win, &dw, &dh);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->Viewport(0, 0, dw, dh);
    G->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    G->Clear(GL_COLOR_BUFFER_BIT);
    if (G->pic_w > 0 && dw > 0 && dh > 0) {
        /* 4:3, as large as it fits, in the middle */
        int w = dw, h = dh;
        if ((double)dw * 3.0 > (double)dh * 4.0) w = (int)((double)dh * 4.0 / 3.0 + 0.5);
        else h = (int)((double)dw * 3.0 / 4.0 + 0.5);
        draw((dw - w) / 2, (dh - h) / 2, w, h);
    }
    SDL_GL_SwapWindow(G->win);
}

/*
 * A TEST (OS_GPUCHECK): the picture drawn once more, one screen pixel to one picture pixel, into
 * a texture of its own, read back, and every pixel compared with what the software way would
 * have shown, pal[pixels[i]].  Gives how many differ.
 */
int gpu_check(const uint8_t *pixels, int w, int h, const uint32_t pal[256])
{
    if (!gpu() || w <= 0 || h <= 0 || w != G->pic_w || h != G->pic_h) return -1;
    GLuint fbo = 0;
    const GLuint tex = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    G->GenFramebuffers(1, &fbo);
    G->BindFramebuffer(GL_FRAMEBUFFER, fbo);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    int differ = -1;
    uint32_t *got = malloc((size_t)w * (size_t)h * sizeof *got);
    if (got && G->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        draw(0, 0, w, h);
        G->PixelStorei(GL_PACK_ALIGNMENT, 4);
        G->ReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, got);
        differ = 0;
        for (int y = 0; y < h; y++)                  /* (the texture's row 0 is the picture's bottom) */
            for (int x = 0; x < w; x++)
                if ((got[(size_t)y * w + x] | 0xff000000u) != (pal[pixels[(size_t)(h - 1 - y) * w + x]] | 0xff000000u))
                    differ++;
    }
    free(got);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->DeleteFramebuffers(1, &fbo);
    G->DeleteTextures(1, &tex);
    return differ;
}
