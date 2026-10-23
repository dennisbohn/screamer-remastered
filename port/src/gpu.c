/*
 * gpu.c - THE GRAPHICS CARD SHOWS THE PICTURE (the fork's GPU renderer; plan: docs/GPU.md).
 *
 * M1: the picture (palette numbers in fb) and the shown palette go up as textures; a shader
 * fills the window 4:3 with bars, nearest pixel, as SDL_RenderSetLogicalSize(960, 720).
 * M2: the 3D scene drawn here instead of by raster_job, from the same triangle list.
 * OpenGL 4.5 core, its functions from SDL_GL_GetProcAddress: nothing to link; without it the
 * software way shows the pictures.  The state outlives the parts (os_slot 11, os_keep_calloc).
 */
#include "gpu.h"
#include "port.h"

#include <SDL_opengl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port.h's malloc and free are the port's own, freed between parts; what lives in the slot is
   the C library's: (free), (realloc) */

#define GPU_SLOT 11

/* the functions used: return type, name without "gl", arguments */
#define GPU_GL(X) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ClearBufferfv, (GLenum, GLint, const GLfloat *)) \
    X(void, ClearBufferiv, (GLenum, GLint, const GLint *)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, StencilFunc, (GLenum, GLint, GLuint)) \
    X(void, StencilOp, (GLenum, GLenum, GLenum)) \
    X(void, TextureBarrier, (void)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, TexBuffer, (GLenum, GLenum, GLuint)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, ReadBuffer, (GLenum)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, DrawBuffers, (GLsizei, const GLenum *)) \
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
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum))

/* a triangle as the scene's shaders take it: raster_job's planes, worked out as there (std430) */
typedef struct {
    float ax, ay;                       /* the planes' origin, the first corner */
    float ia, diz, dizy;                /* 1 / depth there, and its steps across and down */
    float ua, duz, duzy, va, dvz, dvzy; /* u / depth and v / depth the same */
    float zbias, hw, gvy, eh;           /* the software's depth factor, the card's, a sprite's ground and eye */
    int   offset, avail, flat, kind, wrap, id;
    int   rank;                         /* (pass 0's draws, not the shaders') */
    int   pad[2];
} gpu_rec;

/* a corner, and whose: the triangle's number goes with each corner (no gl_PrimitiveID: one
   number for all the draws, whichever triangles each takes) */
typedef struct {
    float x, y;
    int   tri;
} gpu_corner;

typedef struct {
    SDL_Window   *win;
    SDL_GLContext ctx;
#define GPU_FIELD_(ret, name, args) ret (APIENTRY *name) args;
    GPU_GL(GPU_FIELD_)
#undef GPU_FIELD_
    /* M1 */
    GLuint prog, vao, picture, palette;
    GLint  u_size, u_area;
    int    pic_w, pic_h;            /* the picture texture's size; made again when the picture's changes */
    /* M2 */
    int    scene_ok;
    GLuint prog0, prog1, vao3, vbo, ssbo;
    long   vbo_bytes, ssbo_bytes;
    GLint  u0_size, u1_size, u1_smg_rows, u1_tie, u1_cy, u1_focal;
    GLuint fbo0, fbo1, t_index, t_zsw, t_id, t_depth;
    int    sw, sh;                  /* the scene targets' size */
    GLuint mlb_buf, mlb_tex, d2_buf, d2_tex, fl_buf, fl_tex, smg_tex;
    gpu_rec *rec;                   /* by triangle, and its corners (C library memory) */
    gpu_corner *corner;
    long   rec_cap;
    float *tie;                     /* by object: the tie nudge */
    unsigned char *flags;           /* by object: 1 carpet, 2 ground */
    int    nobj_cap;
} gpu_state;

static gpu_state *G;                /* (put back with a part's statics: found again in the slot) */

static gpu_state *gpu(void)
{
    if (!G && os_slot(GPU_SLOT)) G = os_slot(GPU_SLOT);
    return G;
}

int gpu_active(void) { return gpu() != NULL; }

/* ---------------------------------------------------------------- shaders */

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

/* M2: a corner of the scene, on the screen already.  Picture row 0 is GL row 0 (read back top
   first).  Depth and texture come from the triangle's planes, in the fragment shaders */
static const char *const g_scene_vertex_src =
    "#version 450 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in int a_tri;\n"
    "uniform vec2 u_size;\n"
    "flat out int f_tri;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos / u_size * 2.0 - 1.0, 0.0, 1.0);\n"
    "    f_tri = a_tri;\n"
    "}\n";

/* the triangle, and the texel as raster_job reads it with OS_UVRUN=1 (every pixel's place exact):
   its planes at the pixel's centre, the same sums in the same order ("precise": none run
   together).  256 = none there.  The card's depth is 1/depth x its factor x 8 (1/NEAR_Z x 8:
   under 0.36), larger = nearer */
#define GPU_TEXEL_SRC \
    "struct tri_rec {\n" \
    "    float ax, ay, ia, diz, dizy, ua, duz, duzy, va, dvz, dvzy;\n" \
    "    float zbias, hw, gvy, eh;\n" \
    "    int offset, avail, colour, kind, wrap, id, pad0, pad1, pad2;\n" \
    "};\n" \
    "layout(std430, binding = 0) readonly buffer tri_list { tri_rec tri[]; };\n" \
    "layout(binding = 0) uniform usamplerBuffer mlb;\n" \
    "flat in int f_tri;\n" \
    "tri_rec t;\n" \
    "float iz;\n" \
    "uint texel() {\n" \
    "    t = tri[f_tri];\n" \
    "    precise float fx = gl_FragCoord.x - t.ax, fy = gl_FragCoord.y - t.ay;\n" \
    "    precise float z = t.ia + fx * t.diz + fy * t.dizy;\n" \
    "    precise float uz = t.ua + fx * t.duz + fy * t.duzy, vz = t.va + fx * t.dvz + fy * t.dvzy;\n" \
    "    precise float rz = 1.0 / z;\n" \
    "    precise float u = uz * rz, v = vz * rz;\n" \
    "    iz = z;\n" \
    "    if (t.colour >= 0) return uint(t.colour);\n" \
    "    int tu = int(u * 65536.0) >> 16, tv = int(v * 65536.0) >> 16;\n" \
    "    if ((t.kind & 4) != 0) { tu &= t.wrap; tv &= t.wrap; }\n" \
    "    else { tu = clamp(tu, 0, 255); tv = clamp(tv, 0, 255); }\n" \
    "    int o = tv * 256 + tu;\n" \
    "    if (o >= t.avail) return 256u;\n" \
    "    return texelFetch(mlb, t.offset + o).r;\n" \
    "}\n"

/* pass 0, everything solid: palette number, the software's depth, the owner */
static const char *const g_scene0_src =
    "#version 450 core\n"
    GPU_TEXEL_SRC
    "layout(location = 0) out uint o_index;\n"
    "layout(location = 1) out float o_zsw;\n"
    "layout(location = 2) out int o_id;\n"
    "void main() {\n"
    "    uint n = texel();\n"
    "    if (n == 256u || ((t.kind & 1) != 0 && n == 0u)) discard;\n"
    "    o_index = n; o_zsw = iz * t.zbias; o_id = t.id;\n"
    "    gl_FragDepth = iz * t.hw * 8.0;\n"
    "}\n";

/* pass 1, routine 224: raster_job's rules of depth against pass 0, then SMG[strength][what is there] */
static const char *const g_scene1_src =
    "#version 450 core\n"
    GPU_TEXEL_SRC
    "layout(binding = 1) uniform usampler2D t_index;\n"
    "layout(binding = 2) uniform sampler2D t_zsw;\n"
    "layout(binding = 3) uniform isampler2D t_id;\n"
    "layout(binding = 4) uniform samplerBuffer obj_d2;\n"
    "layout(binding = 5) uniform usamplerBuffer obj_flags;\n"   /* 1 carpet, 2 ground */
    "layout(binding = 6) uniform usampler2D smg;\n"
    "uniform int u_smg_rows, u_tie;\n"
    "uniform float u_cy, u_focal;\n"
    "layout(location = 0) out uint o_index;\n"
    "bool over_ground(float mine, float there, float py, float gvy, float h) {\n"
    "    float below = gvy - (u_cy - py) / (u_focal * mine);\n"
    "    return below > -256.0 && mine > there * 0.85 * h / (h + max(below, 0.0));\n"
    "}\n"
    "void main() {\n"
    "    uint n = texel();\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    float mine = iz * t.zbias, there = texelFetch(t_zsw, p, 0).r;\n"
    "    int other = texelFetch(t_id, p, 0).r, id = t.id;\n"
    "    if (!(mine > there * 2.0)) {\n"
    "        uint fo = other >= 0 ? texelFetch(obj_flags, other).r : 0u;\n"
    "        bool co = (fo & 1u) != 0u, cm = id >= 0 && (texelFetch(obj_flags, id).r & 1u) != 0u;\n"
    "        if ((t.kind & 8) != 0 && other >= 0 && (fo & 2u) != 0u && over_ground(mine, there, gl_FragCoord.y, t.gvy, t.eh)) {}\n"
    "        else if (mine * 2.0 < there) discard;\n"
    "        else if (u_tie != 0 && other >= 0 && cm != co && there > 0.0) {\n"
    "            if (cm ? mine < there * 2.0 : mine * 2.0 < there) discard;\n"
    "        } else if (u_tie != 0 && other >= 0 && other != id && id >= 0 && there > 0.0 &&\n"
    "                   mine > there * 0.9999 && mine < there * 1.0001 &&\n"
    "                   texelFetch(obj_d2, id).r != texelFetch(obj_d2, other).r) {\n"
    "            if (texelFetch(obj_d2, id).r > texelFetch(obj_d2, other).r) discard;\n"
    "        } else if (mine < there) discard;\n"
    "    }\n"
    "    if (n == 256u || n == 0u) discard;\n"
    "    uint under = texelFetch(t_index, p, 0).r;\n"
    "    o_index = texelFetch(smg, ivec2(int(under), min(int(n), u_smg_rows - 1)), 0).r;\n"
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

static GLuint program(const char *vsrc, const char *fsrc)
{
    const GLuint vs = shader(GL_VERTEX_SHADER, vsrc), fs = shader(GL_FRAGMENT_SHADER, fsrc);
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

/* a buffer read through a texture (texelFetch by index) */
static void buffer_texture(GLuint *buf, GLuint *tex)
{
    G->GenBuffers(1, buf);
    G->GenTextures(1, tex);
}

static void buffer_fill(GLuint buf, GLuint tex, GLenum format, const void *data, long bytes)
{
    G->BindBuffer(GL_TEXTURE_BUFFER, buf);
    G->BufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)bytes, data, GL_DYNAMIC_DRAW);
    G->BindTexture(GL_TEXTURE_BUFFER, tex);
    G->TexBuffer(GL_TEXTURE_BUFFER, format, buf);
}

/* --------------------------------------------------------------- M0, M1 */

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
    /* (OS_GPUCHECK, OS_GPUHIDDEN: tests, no window to be seen) */
    const int hidden = getenv("OS_GPUCHECK") || getenv("OS_GPUHIDDEN");
    return SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : 0);
}

static void scene_open(void);

int gpu_open(SDL_Window *win)
{
    if (gpu()) return 1;
    gpu_state *s = os_keep_calloc(1, sizeof *s);
    if (!s) return 0;
    s->win = win;
    s->ctx = SDL_GL_CreateContext(win);
    if (!s->ctx) {
        fprintf(stderr, "gpu: no OpenGL 4.5 here (%s) -- the software way shows the pictures\n", SDL_GetError());
        (free)(s);
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
        s->prog = program(g_vertex_src, g_fragment_src);
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
        (free)(s);
        G = NULL;
        return 0;
    }
    scene_open();
    os_slot_set(GPU_SLOT, s);
    const int vsync = SDL_GL_SetSwapInterval(getenv("OS_NOVSYNC") ? 0 : 1) == 0 && !getenv("OS_NOVSYNC");     /* (OS_NOVSYNC: a test, for timing) */
    printf("display: OpenGL %s on %s, vsync %s, the 3D scene %s\n", (const char *)G->GetString(GL_VERSION),
           (const char *)G->GetString(GL_RENDERER), vsync ? "on" : "NOT given by the driver",
           s->scene_ok ? "on the card" : "in software");
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

/* ------------------------------------------------------------------- M2 */

static void scene_open(void)
{
    G->prog0 = program(g_scene_vertex_src, g_scene0_src);
    G->prog1 = G->prog0 ? program(g_scene_vertex_src, g_scene1_src) : 0;
    if (!G->prog0 || !G->prog1) return;
    G->u0_size = G->GetUniformLocation(G->prog0, "u_size");
    G->u1_size = G->GetUniformLocation(G->prog1, "u_size");
    G->u1_smg_rows = G->GetUniformLocation(G->prog1, "u_smg_rows");
    G->u1_tie = G->GetUniformLocation(G->prog1, "u_tie");
    G->u1_cy = G->GetUniformLocation(G->prog1, "u_cy");
    G->u1_focal = G->GetUniformLocation(G->prog1, "u_focal");
    G->GenVertexArrays(1, &G->vao3);
    G->GenBuffers(1, &G->vbo);
    G->GenBuffers(1, &G->ssbo);
    G->BindVertexArray(G->vao3);
    G->BindBuffer(GL_ARRAY_BUFFER, G->vbo);
    G->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(gpu_corner), (const void *)offsetof(gpu_corner, x));
    G->VertexAttribIPointer(1, 1, GL_INT, (GLsizei)sizeof(gpu_corner), (const void *)offsetof(gpu_corner, tri));
    G->EnableVertexAttribArray(0);
    G->EnableVertexAttribArray(1);
    G->BindVertexArray(0);
    buffer_texture(&G->mlb_buf, &G->mlb_tex);
    buffer_texture(&G->d2_buf, &G->d2_tex);
    buffer_texture(&G->fl_buf, &G->fl_tex);
    G->smg_tex = texture();
    G->scene_ok = G->GetError() == GL_NO_ERROR;
}

int gpu_scene_ready(void) { return gpu() && G->scene_ok && G->mlb_buf; }

void gpu_materials(const uint8_t *bytes, long len)
{
    if (!gpu() || !G->scene_ok || !bytes || len <= 0) return;
    buffer_fill(G->mlb_buf, G->mlb_tex, GL_R8UI, bytes, len);
}

/* the targets: palette number, the software's depth, the owner, the card's depth (and a stencil:
   a car's blocks, pass 0) */
static void scene_targets(int w, int h)
{
    if (w == G->sw && h == G->sh) return;
    if (G->fbo0) {
        const GLuint t[4] = { G->t_index, G->t_zsw, G->t_id, G->t_depth };
        G->DeleteTextures(4, t);
        G->DeleteFramebuffers(1, &G->fbo0);
        G->DeleteFramebuffers(1, &G->fbo1);
    }
    G->t_index = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    G->t_zsw = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);
    G->t_id = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R16I, w, h, 0, GL_RED_INTEGER, GL_SHORT, NULL);
    G->t_depth = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, NULL);
    G->GenFramebuffers(1, &G->fbo0);
    G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G->t_index, 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, G->t_zsw, 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, G->t_id, 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, G->t_depth, 0);
    const GLenum all[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    G->DrawBuffers(3, all);
    if (G->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "gpu: the scene's targets are not complete\n");
    G->GenFramebuffers(1, &G->fbo1);                /* pass 1: the palette numbers alone */
    G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo1);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G->t_index, 0);
    const GLenum one = GL_COLOR_ATTACHMENT0;
    G->DrawBuffers(1, &one);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->sw = w; G->sh = h;
}

/* depth_keep's rules for the solid faces, as factors on the card's depth: a carpet counts as
   twice as far; on a tie the object nearer the camera wins (a hair's nudge by distance) */
static const float *g_rank_d2;
static int by_far(const void *a, const void *b)
{
    const float da = g_rank_d2[*(const int *)a], db = g_rank_d2[*(const int *)b];
    return da < db ? 1 : da > db ? -1 : 0;
}

static void scene_factors(const gpu_view *v, const gpu_tri *t, int n)
{
    if (v->nobj > G->nobj_cap) {
        G->tie = (realloc)(G->tie, sizeof *G->tie * (size_t)v->nobj);
        G->flags = (realloc)(G->flags, (size_t)v->nobj);
        G->nobj_cap = v->nobj;
    }
    int *ids = malloc(sizeof *ids * (size_t)(v->nobj + 1)), nid = 0;
    if (!G->tie || !G->flags || !ids) { free(ids); return; }
    memset(G->flags, 0, (size_t)v->nobj);
    for (int i = 0; i < v->nobj; i++) G->tie[i] = 1.0f;
    for (int i = 0; i < n; i++) {
        const int id = t[i].id;
        if (id < 0 || id >= v->nobj || (G->flags[id] & 4)) continue;
        G->flags[id] |= 4;                          /* (seen: listed once) */
        ids[nid++] = id;
    }
    for (int i = 0; i < v->nobj; i++)
        G->flags[i] = (unsigned char)((v->obj_carpet[i] ? 1 : 0) | (v->obj_ground[i] ? 2 : 0));
    if (v->tie_rule && nid > 1) {
        g_rank_d2 = v->obj_d2;
        qsort(ids, (size_t)nid, sizeof *ids, by_far);
        const float step = 1.0e-4f / (float)nid < 4.0e-7f ? 1.0e-4f / (float)nid : 4.0e-7f;
        /* (objects at the same distance get the same nudge: there the later face wins, as in raster_job) */
        for (int r = 0, k = 0; r < nid; r++) {
            if (r > 0 && v->obj_d2[ids[r]] != v->obj_d2[ids[r - 1]]) k++;
            G->tie[ids[r]] = 1.0f + step * (float)k;
        }
    }
    free(ids);
}

void gpu_scene(const gpu_view *v, const gpu_tri *t, int n, uint8_t *fb, float *zb, int16_t *idb)
{
    if (!gpu() || !G->scene_ok) return;
    const int w = v->w, h = v->h;
    scene_targets(w, h);
    scene_factors(v, t, n);

    /* the triangles, with raster_job's planes worked out as there (the same sums in the same
       order); m of them have pixels, the see-through ones from glass_from on */
    if ((long)n > G->rec_cap) {
        const long cap = (long)n + 4096;
        gpu_rec *r = (realloc)(G->rec, sizeof *r * (size_t)cap);
        if (r) G->rec = r;
        gpu_corner *p = (realloc)(G->corner, sizeof *p * 3 * (size_t)cap);
        if (p) G->corner = p;
        if (!r || !p) return;
        G->rec_cap = cap;
    }
    int m = 0, glass_from = -1;
    for (int i = 0; i < n; i++) {
        const gpu_tri *q = &t[i];
        const float ax = q->x[0], ay = q->y[0], bx = q->x[1], by = q->y[1], cx = q->x[2], cy = q->y[2];
        const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (fabsf(area) < 1e-6f) continue;
        if (q->kind & GPU_GLASS) { if (glass_from < 0) glass_from = m; }
        else if (glass_from >= 0) {                     /* (render_scene lists all solid faces first) */
            static int told;
            if (!told++) fprintf(stderr, "gpu: a solid face after a see-through one: drawn out of order\n");
        }
        if ((q->kind & (GPU_SPRITE | GPU_GLASS)) == GPU_SPRITE) {
            static int told;
            if (!told++) fprintf(stderr, "gpu: a solid face of a puff: drawn without over_ground\n");
        }
        gpu_rec *r = &G->rec[m];
        const float inv = 1.0f / area;
        const float e1x = bx - ax, e1y = by - ay, e2x = cx - ax, e2y = cy - ay;
        const float ia = q->iz[0], ib = q->iz[1], ic = q->iz[2];
        const float ua = q->u[0] * ia, ub = q->u[1] * ib, uc = q->u[2] * ic;
        const float va = q->v[0] * ia, vb = q->v[1] * ib, vc = q->v[2] * ic;
#define PLANE_(qa_, qb_, qc_, dx_, dy_) \
        (dx_ = (((qb_) - (qa_)) * e2y - ((qc_) - (qa_)) * e1y) * inv, \
         dy_ = (((qc_) - (qa_)) * e1x - ((qb_) - (qa_)) * e2x) * inv)
        PLANE_(ia, ib, ic, r->diz, r->dizy);
        PLANE_(ua, ub, uc, r->duz, r->duzy);
        PLANE_(va, vb, vc, r->dvz, r->dvzy);
#undef PLANE_
        r->ax = ax; r->ay = ay; r->ia = ia; r->ua = ua; r->va = va;
        const int id = q->id >= 0 && q->id < v->nobj ? q->id : -1;
        r->zbias = q->zbias;
        r->hw = q->zbias * (id >= 0 && (G->flags[id] & 1) ? 0.5f : 1.0f) * (id >= 0 ? G->tie[id] : 1.0f);
        r->gvy = q->ground_vy; r->eh = q->eye_h;
        r->offset = q->offset; r->avail = q->avail; r->flat = q->flat; r->kind = q->kind;
        r->wrap = q->wrap_mask; r->id = id; r->rank = q->rank;
        r->pad[0] = r->pad[1] = 0;
        gpu_corner *c = &G->corner[(size_t)m * 3];
        c[0].x = ax; c[0].y = ay; c[1].x = bx; c[1].y = by; c[2].x = cx; c[2].y = cy;
        c[0].tri = c[1].tri = c[2].tri = m;
        m++;
    }
    if (glass_from < 0) glass_from = m;
    /* (each a fresh store: the last picture's may still be read) */
    const long vbytes = (long)m * 3 * (long)sizeof(gpu_corner), rbytes = (long)m * (long)sizeof(gpu_rec);
    if (vbytes > G->vbo_bytes) G->vbo_bytes = vbytes;
    if (rbytes > G->ssbo_bytes) G->ssbo_bytes = rbytes;
    G->BindBuffer(GL_ARRAY_BUFFER, G->vbo);
    G->BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(G->vbo_bytes > 0 ? G->vbo_bytes : 16), NULL, GL_STREAM_DRAW);
    if (vbytes > 0) G->BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vbytes, G->corner);
    G->BindBuffer(GL_SHADER_STORAGE_BUFFER, G->ssbo);
    G->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(G->ssbo_bytes > 0 ? G->ssbo_bytes : 16), NULL, GL_STREAM_DRAW);
    if (rbytes > 0) G->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)rbytes, G->rec);
    G->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, G->ssbo);

    /* the tables pass 1 reads */
    buffer_fill(G->d2_buf, G->d2_tex, GL_R32F, v->obj_d2, (long)v->nobj * (long)sizeof(float));
    buffer_fill(G->fl_buf, G->fl_tex, GL_R8UI, G->flags, v->nobj);
    G->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    G->BindTexture(GL_TEXTURE_2D, G->smg_tex);
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, v->smg_rows, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, v->smg);

    /* the backdrop, and nothing in front of it yet */
    G->BindTexture(GL_TEXTURE_2D, G->t_index);
    G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, fb);
    G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo0);
    const GLfloat zero = 0.0f;
    const GLint none = -1;
    G->DepthMask(GL_TRUE);                          /* (a clear of the depth obeys the mask: pass 1 turns it off) */
    G->ClearBufferfv(GL_COLOR, 1, &zero);
    G->ClearBufferiv(GL_COLOR, 2, &none);
    G->ClearBufferfv(GL_DEPTH, 0, &zero);

    /* pass 0: the solid faces, in order; later ones win ties (raster_job: !(mine < there)).  A
       car's blocks come the last in the engine's order first, and the stencil keeps each from
       the pixels of the ones before it: raster_job's rkb */
    G->Viewport(0, 0, w, h);
    G->Scissor(0, 0, w, v->rows);
    G->Enable(GL_SCISSOR_TEST);
    G->Enable(GL_DEPTH_TEST);
    G->DepthFunc(GL_GEQUAL);
    G->StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    G->BindVertexArray(G->vao3);
    G->ActiveTexture(GL_TEXTURE0);
    G->BindTexture(GL_TEXTURE_BUFFER, G->mlb_tex);
    G->UseProgram(G->prog0);
    G->Uniform2f(G->u0_size, (float)w, (float)h);
    for (int i = 0, car = -1; i < glass_from; ) {
        const int rank = G->rec[i].rank, id = G->rec[i].id;
        int j = i + 1;
        while (j < glass_from && G->rec[j].rank == rank && G->rec[j].id == id) j++;
        if (rank < 0) { G->Disable(GL_STENCIL_TEST); car = -1; }
        else {
            if (id != car) { const GLint none_yet = 0; G->ClearBufferiv(GL_STENCIL, 0, &none_yet); car = id; }
            G->Enable(GL_STENCIL_TEST);
            G->StencilFunc(GL_GEQUAL, rank + 1, 0xff);
        }
        G->DrawArrays(GL_TRIANGLES, i * 3, (j - i) * 3);
        i = j;
    }
    G->Disable(GL_STENCIL_TEST);
    G->TextureBarrier();

    /* pass 1: routine 224, reading what is there -- triangles that overlap are drawn one after
       another, with a barrier between */
    if (glass_from < m) {
        G->Disable(GL_DEPTH_TEST);
        G->DepthMask(GL_FALSE);
        G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo1);
        G->UseProgram(G->prog1);
        G->Uniform2f(G->u1_size, (float)w, (float)h);
        G->Uniform1i(G->u1_smg_rows, v->smg_rows);
        G->Uniform1i(G->u1_tie, v->tie_rule);
        G->Uniform1f(G->u1_cy, v->cy);
        G->Uniform1f(G->u1_focal, v->focal_y);
        const GLuint tex2d[3] = { G->t_index, G->t_zsw, G->t_id };
        for (int k = 0; k < 3; k++) { G->ActiveTexture(GL_TEXTURE1 + k); G->BindTexture(GL_TEXTURE_2D, tex2d[k]); }
        G->ActiveTexture(GL_TEXTURE4); G->BindTexture(GL_TEXTURE_BUFFER, G->d2_tex);
        G->ActiveTexture(GL_TEXTURE5); G->BindTexture(GL_TEXTURE_BUFFER, G->fl_tex);
        G->ActiveTexture(GL_TEXTURE6); G->BindTexture(GL_TEXTURE_2D, G->smg_tex);
        float box[64][4];
        int nbox = 0, from = glass_from;
        for (int i = glass_from; i <= m; i++) {
            int clash = i == m || nbox == 64;
            float b[4] = { 0, 0, 0, 0 };
            if (i < m) {
                const gpu_corner *c = &G->corner[(size_t)i * 3];
                b[0] = b[2] = c[0].x; b[1] = b[3] = c[0].y;
                for (int k = 1; k < 3; k++) {
                    if (c[k].x < b[0]) b[0] = c[k].x;
                    if (c[k].x > b[2]) b[2] = c[k].x;
                    if (c[k].y < b[1]) b[1] = c[k].y;
                    if (c[k].y > b[3]) b[3] = c[k].y;
                }
                for (int j = 0; j < nbox && !clash; j++)
                    if (b[0] <= box[j][2] + 1 && b[2] + 1 >= box[j][0] && b[1] <= box[j][3] + 1 && b[3] + 1 >= box[j][1]) clash = 1;
            }
            if (clash && i > from) {
                G->DrawArrays(GL_TRIANGLES, from * 3, (i - from) * 3);
                G->TextureBarrier();
                from = i; nbox = 0;
            }
            if (i < m) memcpy(box[nbox++], b, sizeof b);
        }
    }

    /* back to fb, zb and idb */
    G->Disable(GL_SCISSOR_TEST);
    G->Disable(GL_DEPTH_TEST);
    G->DepthMask(GL_TRUE);
    G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo0);
    G->PixelStorei(GL_PACK_ALIGNMENT, 1);
    G->ReadBuffer(GL_COLOR_ATTACHMENT0);
    G->ReadPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, fb);
    G->ReadBuffer(GL_COLOR_ATTACHMENT1);
    G->ReadPixels(0, 0, w, h, GL_RED, GL_FLOAT, zb);
    G->ReadBuffer(GL_COLOR_ATTACHMENT2);
    G->ReadPixels(0, 0, w, h, GL_RED_INTEGER, GL_SHORT, idb);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->BindVertexArray(0);
    {
        static int told;
        const GLenum e = G->GetError();
        if (e != GL_NO_ERROR && !told++) fprintf(stderr, "gpu: the scene: OpenGL error 0x%x\n", e);
    }
    /* tests: OS_GPUPUFFS=1 -- the scenes with smoke (puffs' triangles) in them; OS_GPUPIXEL=x,y,n --
       in the n-th scene, what is at that pixel and every triangle over it */
    static int scenes;
    int px = -1, py = -1, at = -1;
    scenes++;
    if (getenv("OS_GPUPUFFS")) {
        int puffs = 0;
        for (int i = glass_from; i < m; i++) puffs += (G->rec[i].kind & GPU_SPRITE) != 0;
        if (puffs) printf("gpu scene %d: %d triangles of puffs\n", scenes, puffs);
    }
    if (getenv("OS_GPUPIXEL") && sscanf(getenv("OS_GPUPIXEL"), "%d,%d,%d", &px, &py, &at) == 3 && at == scenes &&
        px >= 0 && px < w && py >= 0 && py < h) {
        const float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
        printf("gpu pixel %d,%d: colour %d, depth %g, object %d\n", px, py, fb[py * w + px], (double)zb[py * w + px], idb[py * w + px]);
        for (int i = 0; i < m; i++) {
            const gpu_corner *c = &G->corner[(size_t)i * 3];
            const float p[6] = { c[0].x, c[0].y, c[1].x, c[1].y, c[2].x, c[2].y };
            const float d0 = (p[2] - p[0]) * (cy - p[1]) - (p[3] - p[1]) * (cx - p[0]);
            const float d1 = (p[4] - p[2]) * (cy - p[3]) - (p[5] - p[3]) * (cx - p[2]);
            const float d2 = (p[0] - p[4]) * (cy - p[5]) - (p[1] - p[5]) * (cx - p[4]);
            if (!((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))) continue;
            const gpu_rec *r = &G->rec[i];
            const float fx = cx - r->ax, fy = cy - r->ay, iz = r->ia + fx * r->diz + fy * r->dizy;
            printf("  triangle %d: object %d, block %d, kind %d, colour %d, 1/depth %g x %g (card %g), corners %.1f,%.1f %.1f,%.1f %.1f,%.1f\n",
                   i, r->id, r->rank, r->kind, r->flat, (double)iz, (double)r->zbias, (double)(iz * r->hw * 8.0f),
                   (double)p[0], (double)p[1], (double)p[2], (double)p[3], (double)p[4], (double)p[5]);
        }
    }
}
