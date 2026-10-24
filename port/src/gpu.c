/*
 * gpu.c - THE GRAPHICS CARD SHOWS THE PICTURE (the fork's GPU renderer; plan: docs/GPU.md).
 *
 * M1: the picture (palette numbers in fb) and the shown palette go up as textures; a shader
 * fills the window 4:3 with bars, nearest pixel, as SDL_RenderSetLogicalSize(960, 720).
 * M2: the 3D scene drawn here instead of by raster_job, from the same triangle list.
 * M3: that scene at any size (the window's), and the flat things in fb laid over it when shown;
 * FILTERING (the scene in colour too, its textures filtered) and MSAA (shaded once a pixel, keyed
 * edges by alpha to coverage).
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
    X(void, Uniform4i, (GLint, GLint, GLint, GLint, GLint)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(GLenum, CheckFramebufferStatus, (GLenum))

/* M3: those only FILTERING and MSAA use -- one missing, and only they are not there */
#define GPU_GL_COLOUR(X) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GenQueries, (GLsizei, GLuint *)) \
    X(void, QueryCounter, (GLuint, GLenum)) \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, uint64_t *)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(void, ColorMaski, (GLuint, GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, GenSamplers, (GLsizei, GLuint *)) \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
    X(void, SamplerParameterf, (GLuint, GLenum, GLfloat)) \
    X(void, BindSampler, (GLuint, GLuint)) \
    X(void, TexStorage3D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei)) \
    X(void, TexStorage2DMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, BindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum)) \
    X(void, DispatchCompute, (GLuint, GLuint, GLuint)) \
    X(void, MemoryBarrier, (GLbitfield)) \
    X(void, TextureView, (GLuint, GLenum, GLuint, GLenum, GLuint, GLuint, GLuint, GLuint))

/* (core in 4.6, an extension in 4.5: anisotropic filtering) */
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

/* a triangle as the scene's shaders take it: raster_job's planes, worked out as there (std430) */
typedef struct {
    float ax, ay;                       /* the planes' origin, the first corner */
    float ia, diz, dizy;                /* 1 / depth there, and its steps across and down */
    float ua, duz, duzy, va, dvz, dvzy; /* u / depth and v / depth the same */
    float zbias, hw, gvy, eh;           /* the software's depth factor, the card's, a sprite's ground and eye */
    int   offset, avail, flat, kind, wrap, id;
    int   rank;                         /* (pass 0's draws, not the shaders') */
    int   layer;                        /* M3: the texture frame's colour layer, -1 none (FILTERING) */
    int   pad;
} gpu_rec;

/* a corner, and whose: the triangle's number goes with each corner (no gl_PrimitiveID: one
   number for all the draws, whichever triangles each takes) */
typedef struct {
    float x, y;
    int   tri;
} gpu_corner;

/* a program and the places of its uniforms (-1: one it does not have) */
typedef struct {
    GLuint prog;
    GLint  size, factor, gscale, smg_rows, tie, half, cy, focal;     /* the scene's passes */
    GLint  mode, sky0, sky1, sky2;                                    /* the backdrop's */
    GLint  ssize, area, flip, rgb, samples, to, fade;                 /* the picture's (draw_mixed) */
} gpu_prog;

/* M3: a texture frame made a colour layer: where its bytes are, its extent, its kind */
typedef struct {
    int offset, avail, su, sv, kind, pad[3];
} gpu_layer;

typedef struct {
    SDL_Window   *win;
    SDL_GLContext ctx;
#define GPU_FIELD_(ret, name, args) ret (APIENTRY *name) args;
    GPU_GL(GPU_FIELD_)
    GPU_GL_COLOUR(GPU_FIELD_)
#undef GPU_FIELD_
    /* M1 */
    GLuint prog, vao, picture, palette;
    GLint  u_size, u_area;
    int    pic_w, pic_h;            /* the picture texture's size; made again when the picture's changes */
    /* M2: the scene's passes and the backdrop's -- as drawn [0]; M3: FILTERING [1], MSAA [2], both [3] */
    int    scene_ok;
    gpu_prog pass0[4], pass1[4], back[4];
    GLuint vao3, vbo, ssbo;
    long   vbo_bytes, ssbo_bytes;
    GLuint fbo0, fbo1, t_index, t_zsw, t_id, t_depth;
    int    sw, sh;                  /* the scene targets' size */
    /* M3: a scene drawn larger than fb -- the targets at fb's size it is read back through, the
       backdrop's pass (fb's picture or the sky), the flat things over it as it is shown */
    GLuint fbo_lo, t_lo_index, t_lo_zsw, t_lo_id;
    int    lo_w, lo_h;
    GLuint fb_tex, sky_tex;
    int    fbt_w, fbt_h, sky_w, sky_h;
    const uint8_t *sky_pix;
    gpu_prog mix[2], readback;      /* the picture: one sample a pixel [0], MSAA's [1]; MSAA's read-back */
    GLuint layer_tex, lut_tex;
    int    layer_w, layer_h, flip;
    int    scene_card, overlay_ok;  /* this picture: the card's scene shown (larger, filtered, MSAA), and the flat things for it */
    int    last_card, last_overlay; /* the picture shown last (gpu_again) */
    int    vw, vh, scw, sch;        /* the scene's: fb's size, the card's */
    /* M3: FILTERING and MSAA (gpu_quality) */
    int    want_filter, want_msaa;  /* as set */
    int    colour_ok, max_samples;  /* the shaders are there; the most samples every kind of target can have */
    float  aniso;                   /* anisotropic filtering's most, 0 none */
    int    filter, samples;         /* the scene drawn last: in colour too (FILTERING), samples a pixel (1 none) */
    GLuint t_colour, fbo0c, fbo1c;  /* FILTERING without MSAA: the colour beside t_index, t_zsw, t_id, t_depth */
    int    cw, ch, c_stale;
    GLuint m_colour, m_index, m_zsw, m_id, m_depth, fbo0m, fbo1m;    /* MSAA's targets (the colour with FILTERING) */
    int    mw, mh, ms, m_rgb, ms_got;   /* their size, samples and colour asked for, the samples made */
    GLuint pal_tex, smg_fit, lut_fit;   /* the scene's palette (256 x 1); SMG's rows and the darkenings as lines (rows_fit) */
    uint32_t pal_now[256], smg_fit_sum;
    int    pal_set;
    float  fade, to_k;              /* the picture's fades (gpu_fades) */
    uint32_t to;
    /* M3: the materials' texture frames as colour layers (FILTERING), C library memory */
    GLuint layers, layers_rgba, layer_buf, samp_clamp, samp_wrap, prog_layers;   /* (layers_rgba: a view of them to write through) */
    int    nlayer, layers_cap, layers_made, layers_no;   /* layers_no: no room for them (till other materials) */
    gpu_layer *layer_rec;
    int   *mat_first, *mat_frames, *frame_layer;
    int    nmat, nframe;
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

/* every shader's first line; then its variant's #defines (M3: RGB the scene in colour too,
   FILTERING; MS once for each of a pixel's samples, MSAA), then its text */
#define GLSL_VERSION "#version 450 core\n"

/* M3: the palette's colours are the screen's (sRGB); what is averaged -- texels filtered, a pixel's
   samples, the sky's four texels -- is averaged as light and turned back (lin, enc) */
#define GPU_LIGHT_SRC \
    "vec3 lin(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }\n" \
    "vec3 enc(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }\n"

/* a corner of one triangle that covers the whole viewport (no vertex data: gl_VertexID) */
static const char *const g_vertex_src =
    "void main() {\n"
    "    const vec2 corner[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));\n"
    "    gl_Position = vec4(corner[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

/* the picture pixel under this screen pixel (the nearest), and its colour from the palette */
static const char *const g_fragment_src =
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
   under 0.36), larger = nearer.  M3: `at` is the pixel's centre in fb's pixels (u_factor of the
   card's to one of fb's; 1, 1 is raster_job's own) -- with MSAA the sample's place instead */
#define GPU_TEXEL_SRC \
    "struct tri_rec {\n" \
    "    float ax, ay, ia, diz, dizy, ua, duz, duzy, va, dvz, dvzy;\n" \
    "    float zbias, hw, gvy, eh;\n" \
    "    int offset, avail, colour, kind, wrap, id, rank, layer, pad;\n" \
    "};\n" \
    "layout(std430, binding = 0) readonly buffer tri_list { tri_rec tri[]; };\n" \
    "layout(binding = 0) uniform usamplerBuffer mlb;\n" \
    "uniform vec2 u_factor;\n" \
    "flat in int f_tri;\n" \
    "#define AT (gl_FragCoord.xy / u_factor)\n" \
    "tri_rec t;\n" \
    "float iz;\n" \
    "vec2 at, uv;\n" \
    "uint texel() {\n" \
    "    t = tri[f_tri];\n" \
    "    at = AT;\n" \
    "    precise float fx = at.x - t.ax, fy = at.y - t.ay;\n" \
    "    precise float z = t.ia + fx * t.diz + fy * t.dizy;\n" \
    "    precise float uz = t.ua + fx * t.duz + fy * t.duzy, vz = t.va + fx * t.dvz + fy * t.dvzy;\n" \
    "    precise float rz = 1.0 / z;\n" \
    "    precise float u = uz * rz, v = vz * rz;\n" \
    "    iz = z;\n" \
    "    uv = vec2(u, v);\n" \
    "    if (t.colour >= 0) return uint(t.colour);\n" \
    "    int tu = int(u * 65536.0) >> 16, tv = int(v * 65536.0) >> 16;\n" \
    "    if ((t.kind & 4) != 0) { tu &= t.wrap; tv &= t.wrap; }\n" \
    "    else { tu = clamp(tu, 0, 255); tv = clamp(tv, 0, 255); }\n" \
    "    int o = tv * 256 + tu;\n" \
    "    if (o >= t.avail) return 256u;\n" \
    "    return texelFetch(mlb, t.offset + o).r;\n" \
    "}\n" \
    "#if defined(RGB) || defined(MS)\n" \
    "uint fetch(int tu, int tv) {\n" \
    "    if ((t.kind & 4) != 0) { tu &= t.wrap; tv &= t.wrap; }\n" \
    "    else { tu = clamp(tu, 0, 255); tv = clamp(tv, 0, 255); }\n" \
    "    int o = tv * 256 + tu;\n" \
    "    return o >= t.avail ? 256u : texelFetch(mlb, t.offset + o).r;\n" \
    "}\n" \
    "#endif\n"

/* pass 0, everything solid: palette number, the software's depth, the owner.  M3, FILTERING (RGB):
   the colour too, first -- the frame's colour layer filtered at the texel's place, its steps per
   card pixel from the planes (u = uz / z), once a pixel with MSAA too; a keyed texture's
   holes by its level 0 (thin things stay thin far off) */
static const char *const g_scene0_src =
    GPU_TEXEL_SRC
    "#ifdef RGB\n"
    GPU_LIGHT_SRC
    "layout(binding = 1) uniform sampler2D pal;\n"
    "layout(binding = 10) uniform sampler2DArray frames;\n"
    "layout(binding = 11) uniform sampler2DArray frames_wrap;\n"
    "uniform float u_gscale;\n"
    "layout(location = 0) out vec4 o_colour;\n"
    "layout(location = 1) out uint o_index;\n"
    "layout(location = 2) out float o_zsw;\n"
    "layout(location = 3) out int o_id;\n"
    "#else\n"
    "layout(location = 0) out uint o_index;\n"
    "layout(location = 1) out float o_zsw;\n"
    "layout(location = 2) out int o_id;\n"
    "#endif\n"
    "void main() {\n"
    "    uint n = texel();\n"
    "#ifdef MS\n"                                        /* a keyed texture's edge: as many samples as it covers */
    "    bool a2c = (t.kind & 1) != 0 && t.colour < 0;\n"
    "    gl_SampleMask[0] = gl_SampleMaskIn[0];\n"             /* (written in every case: else it is undefined) */
    "    if (a2c) {\n"
    "        vec2 f = uv - 0.5, w = fract(f);\n"
    "        ivec2 q = ivec2(floor(f));\n"
    "        float a = 0.0, best = -1.0;\n"
    "        uint near = n;\n"
    "        for (int k = 0; k < 4; k++) {\n"
    "            uint m = fetch(q.x + (k & 1), q.y + (k >> 1));\n"
    "            float wk = ((k & 1) != 0 ? w.x : 1.0 - w.x) * ((k >> 1) != 0 ? w.y : 1.0 - w.y);\n"
    "            if (m != 0u && m != 256u) { a += wk; if (wk > best) { best = wk; near = m; } }\n"
    "        }\n"
    "        float e = clamp((a - 0.5) / max(fwidth(a), 1e-3) + 0.5, 0.0, 1.0);\n"
    "        int ns = gl_NumSamples, k = int(e * float(ns) + 0.5);\n"
    "        if (k <= 0 || n == 256u) discard;\n"
    "        if (n == 0u) n = near;\n"
    "        int r = (int(gl_FragCoord.x) * 3 + int(gl_FragCoord.y) * 5) % ns;\n"
    "        uint all = (1u << uint(ns)) - 1u, bits = k >= ns ? all : (1u << uint(k)) - 1u;\n"
    "        gl_SampleMask[0] = int(((bits << uint(r)) | (bits >> uint(ns - r))) & all) & gl_SampleMaskIn[0];\n"
    "    }\n"
    "#else\n"
    "    const bool a2c = false;\n"
    "#endif\n"
    "#ifdef RGB\n"
    "    bool keyed = (t.kind & 1) != 0, drop = n == 256u || (keyed && n == 0u && !a2c);\n"
    "    vec4 c = vec4(texelFetch(pal, ivec2(int(min(n, 255u)), 0), 0).rgb, 1.0);\n"
    "    if (t.colour < 0 && t.layer >= 0) {\n"
    "        float rz = 1.0 / iz;\n"
    "        vec2 gx = (vec2(t.duz, t.dvz) - uv * t.diz) * (rz * u_gscale / (256.0 * u_factor.x));\n"
    "        vec2 gy = (vec2(t.duzy, t.dvzy) - uv * t.dizy) * (rz * u_gscale / (256.0 * u_factor.y));\n"
    "        vec3 p = vec3(uv / 256.0, float(t.layer));\n"
    "        vec4 s = (t.kind & 4) != 0 ? textureGrad(frames_wrap, p, gx, gy) : textureGrad(frames, p, gx, gy);\n"
    "        if (keyed) {\n"
    "            vec4 s0 = textureLod(frames, p, 0.0);\n"
    "            drop = n == 256u || (s0.a < 0.5 && !a2c);\n"
    "            if (s.a < 1.0 / 255.0) s = s0;\n"
    "            if (n == 0u) {\n"                          /* a hole's place kept: the nearest of the four texels that is none */
    "                vec2 f = uv - 0.5, w = fract(f);\n"
    "                ivec2 q = ivec2(floor(f));\n"
    "                float best = -1.0;\n"
    "                for (int k = 0; k < 4; k++) {\n"
    "                    uint m = fetch(q.x + (k & 1), q.y + (k >> 1));\n"
    "                    float wk = ((k & 1) != 0 ? w.x : 1.0 - w.x) * ((k >> 1) != 0 ? w.y : 1.0 - w.y);\n"
    "                    if (m != 0u && m != 256u && wk > best) { best = wk; n = m; }\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        c = vec4(enc(s.rgb / max(s.a, 1.0 / 255.0)), 1.0);\n"
    "    }\n"
    "    if (drop) discard;\n"
    "    o_colour = c; o_index = n; o_zsw = iz * t.zbias; o_id = t.id;\n"
    "#else\n"
    "    if (n == 256u || ((t.kind & 1) != 0 && n == 0u && !a2c)) discard;\n"
    "    o_index = n; o_zsw = iz * t.zbias; o_id = t.id;\n"
    "#endif\n"
    "    gl_FragDepth = iz * t.hw * 8.0;\n"
    "}\n";

/* pass 1, routine 224: raster_job's rules of depth against pass 0, then SMG[strength][what is
   there] -- with MSAA for each sample against its own.  M3, FILTERING: the colour there through
   the rows' lines on the colours (rows_fit), the four strength texels round the place mixed */
static const char *const g_scene1_src =
    "#ifdef MS\n"
    "#define ONE gl_SampleID\n"
    "#else\n"
    "#define ONE 0\n"
    "#endif\n"
    GPU_TEXEL_SRC
    "#ifdef MS\n"
    "layout(binding = 1) uniform usampler2DMS t_index;\n"
    "layout(binding = 2) uniform sampler2DMS t_zsw;\n"
    "layout(binding = 3) uniform isampler2DMS t_id;\n"
    "#ifdef RGB\n"
    "layout(binding = 7) uniform sampler2DMS t_colour;\n"
    "#endif\n"
    "#else\n"
    "layout(binding = 1) uniform usampler2D t_index;\n"
    "layout(binding = 2) uniform sampler2D t_zsw;\n"
    "layout(binding = 3) uniform isampler2D t_id;\n"
    "#ifdef RGB\n"
    "layout(binding = 7) uniform sampler2D t_colour;\n"
    "#endif\n"
    "#endif\n"
    "layout(binding = 4) uniform samplerBuffer obj_d2;\n"
    "layout(binding = 5) uniform usamplerBuffer obj_flags;\n"   /* 1 carpet, 2 ground */
    "layout(binding = 6) uniform usampler2D smg;\n"
    "uniform int u_smg_rows, u_tie, u_half;\n"
    "uniform float u_cy, u_focal;\n"
    "#ifdef RGB\n"
    "layout(binding = 9) uniform sampler2D smg_fit;\n"          /* each row's line: a, b */
    "layout(location = 0) out vec4 o_colour;\n"
    "layout(location = 1) out uint o_index;\n"
    "#define FILTERING 1\n"
    "#else\n"
    "layout(location = 0) out uint o_index;\n"
    "#define FILTERING 0\n"
    "#endif\n"
    "bool over_ground(float mine, float there, float py, float gvy, float h) {\n"
    "    float below = gvy - (u_cy - py) / (u_focal * mine);\n"
    "    return below > -256.0 && mine > there * 0.85 * h / (h + max(below, 0.0));\n"
    "}\n"
    "void main() {\n"
    "    uint n = texel();\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    float mine = iz * t.zbias, there = texelFetch(t_zsw, p, ONE).r;\n"
    "    int other = texelFetch(t_id, p, ONE).r, id = t.id;\n"
    "    if (!(mine > there * 2.0)) {\n"
    "        uint fo = other >= 0 ? texelFetch(obj_flags, other).r : 0u;\n"
    "        bool co = (fo & 1u) != 0u, cm = id >= 0 && (texelFetch(obj_flags, id).r & 1u) != 0u;\n"
    "        if ((t.kind & 8) != 0 && other >= 0 && (fo & 2u) != 0u && over_ground(mine, there, at.y, t.gvy, t.eh)) {}\n"
    "        else if (mine * 2.0 < there) discard;\n"
    "        else if (u_tie != 0 && other >= 0 && cm != co && there > 0.0) {\n"
    "            if (cm ? mine < there * 2.0 : mine * 2.0 < there) discard;\n"
    "        } else if (u_tie != 0 && other >= 0 && other != id && id >= 0 && there > 0.0 &&\n"
    "                   mine > there * 0.9999 && mine < there * 1.0001 &&\n"
    "                   texelFetch(obj_d2, id).r != texelFetch(obj_d2, other).r) {\n"
    "            if (texelFetch(obj_d2, id).r > texelFetch(obj_d2, other).r) discard;\n"
    "        } else if (mine < there) discard;\n"
    "    }\n"
    "    bool hole = n == 0u && u_half == 0;\n"
    "    if (n == 256u || (hole && FILTERING == 0)) discard;\n"
    "    uint under = texelFetch(t_index, p, ONE).r;\n"
    "    uint m = hole ? under : texelFetch(smg, ivec2(int(under), min(int(n), u_smg_rows - 1)), 0).r;\n"
    "    o_index = m;\n"
    "#ifdef RGB\n"
    "    vec3 a = vec3(0.0), b = vec3(0.0);\n"
    "    vec2 f = uv - 0.5, w = fract(f);\n"
    "    ivec2 q = ivec2(floor(f));\n"
    "    for (int k = 0; k < 4; k++) {\n"
    "        uint s = t.colour >= 0 ? n : fetch(q.x + (k & 1), q.y + (k >> 1));\n"
    "        float wk = ((k & 1) != 0 ? w.x : 1.0 - w.x) * ((k >> 1) != 0 ? w.y : 1.0 - w.y);\n"
    "        int row = s == 256u || (s == 0u && u_half == 0) ? -1 : min(int(s), u_smg_rows - 1);\n"
    "        a += wk * (row < 0 ? vec3(1.0) : texelFetch(smg_fit, ivec2(0, row), 0).rgb);\n"
    "        b += wk * (row < 0 ? vec3(0.0) : texelFetch(smg_fit, ivec2(1, row), 0).rgb);\n"
    "    }\n"
    "    o_colour = vec4(a * texelFetch(t_colour, p, ONE).rgb + b, 1.0);\n"
    "#endif\n"
    "}\n";

/* M3: the scene's backdrop first, at the card's size: fb's picture (mode 0), the sky as sky_paint
   lays it, at the pixel's corner in fb's pixels (1), or one colour (2).  FILTERING: the palette's
   colours too, the sky's four texels round its place mixed; MSAA: once a pixel too */
static const char *const g_back_src =
    "layout(binding = 0) uniform usampler2D source;\n"
    "uniform int u_mode;\n"
    "uniform vec2 u_factor;\n"
    "uniform vec4 u_sky0;\n"            /* off, cos and sin of the roll, picture columns to one of the backdrop's */
    "uniform vec4 u_sky1;\n"            /* rows to one, the eye row, the backdrop's top, fb's width */
    "uniform ivec4 u_sky2;\n"           /* the backdrop's width and height, the ground's colour, the clear one */
    "#define AT gl_FragCoord.xy\n"
    "#ifdef RGB\n"
    GPU_LIGHT_SRC
    "layout(binding = 1) uniform sampler2D pal;\n"
    "layout(location = 0) out vec4 o_colour;\n"
    "layout(location = 1) out uint o_index;\n"
    "layout(location = 2) out float o_zsw;\n"
    "layout(location = 3) out int o_id;\n"
    "#define COLOUR(n) o_colour = vec4(texelFetch(pal, ivec2(int(n), 0), 0).rgb, 1.0)\n"
    "#else\n"
    "layout(location = 0) out uint o_index;\n"
    "layout(location = 1) out float o_zsw;\n"
    "layout(location = 2) out int o_id;\n"
    "#define COLOUR(n)\n"
    "#endif\n"
    "void main() {\n"
    "    vec2 at = AT / u_factor;\n"
    "    o_zsw = 0.0; o_id = -1;\n"
    "    if (u_mode == 0) { uint n = texelFetch(source, ivec2(at), 0).r; o_index = n; COLOUR(n); return; }\n"
    "    if (u_mode == 2) { o_index = uint(u_sky2.w); COLOUR(u_sky2.w); return; }\n"
    "    float x = at.x - 0.5, y = at.y - 0.5;\n"
    "    float cr = u_sky0.y, sr = u_sky0.z, sx = u_sky0.w, sy = u_sky1.x, cy = u_sky1.y;\n"
    "    float ax = -u_sky1.w * 0.5 / sx, ay = (cy - y) / sy;\n"
    "    float fu = u_sky0.x + u_sky1.w * 0.5 / sx + (ax * cr - ay * sr) + x * cr / sx;\n"
    "    float fv = cy / sy - (ax * sr + ay * cr) - u_sky1.z - x * sr / sx;\n"
    "    int v = int(floor(fv));\n"
    "    if (v >= u_sky2.y) { o_index = uint(u_sky2.z); COLOUR(u_sky2.z); return; }\n"
    "    int w = u_sky2.x, u = int(mod(floor(fu), float(w)));\n"
    "    uint n = texelFetch(source, ivec2(u, max(v, 0)), 0).r;\n"
    "    o_index = n;\n"
    "#ifdef RGB\n"
    "    vec2 f = vec2(fu, fv) - 0.5, k = fract(f);\n"
    "    ivec2 q = ivec2(floor(f));\n"
    "    vec3 c[4];\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        int cx = int(mod(float(q.x + (i & 1)), float(w))), cv = clamp(q.y + (i >> 1), 0, u_sky2.y - 1);\n"
    "        c[i] = lin(texelFetch(pal, ivec2(int(texelFetch(source, ivec2(cx, cv), 0).r), 0), 0).rgb);\n"
    "    }\n"
    "    o_colour = vec4(enc(mix(mix(c[0], c[1], k.x), mix(c[2], c[3], k.x), k.y)), 1.0);\n"
    "#endif\n"
    "}\n";

/* M3: the picture shown when the card's scene is: per pixel of fb (the layer) its own colour, or
   the scene's -- as drawn, or through one of the tables (fb's darkenings).  FILTERING (u_rgb): the
   scene's colour, a table as its line on the colours (rows_fit), then the fades.  MSAA: the mean
   of the pixel's samples, each as one of these */
static const char *const g_mix_src =
    "layout(binding = 0) uniform usampler2D picture;\n"
    "layout(binding = 1) uniform sampler2D palette;\n"
    "layout(binding = 2) uniform usampler2D layer;\n"
    "#ifdef MS\n"
    "layout(binding = 3) uniform usampler2DMS scene;\n"   /* row 0 the picture's top, as drawn */
    "layout(binding = 5) uniform sampler2DMS scene_colour;\n"
    "#define SAMPLES textureSamples(scene)\n"
    "#else\n"
    "layout(binding = 3) uniform usampler2D scene;\n"
    "layout(binding = 5) uniform sampler2D scene_colour;\n"
    "#define SAMPLES 1\n"
    "#endif\n"
    "layout(binding = 4) uniform usampler2D luts;\n"
    "layout(binding = 7) uniform sampler2D lut_fit;\n"
    "uniform ivec2 size, ssize, flip;\n"
    "uniform vec4 area;\n"
    "uniform int u_rgb;\n"
    "uniform vec4 u_to;\n"                                /* the fades: towards this colour so far, then */
    "uniform float u_fade;\n"                             /* ... to black (show_palette's) */
    "out vec4 colour;\n"
    GPU_LIGHT_SRC
    "void main() {\n"
    "    vec2 at = (gl_FragCoord.xy - area.xy) / area.zw;\n"
    "    vec2 pic = vec2(at.x, 1.0 - at.y);\n"
    "    ivec2 p = clamp(ivec2(floor(pic * vec2(size))), ivec2(0), size - 1);\n"
    "    uint mode = texelFetch(layer, p, 0).r, n;\n"
    "    if (mode == 1u) n = texelFetch(picture, p, 0).r;\n"
    "    else {\n"
    "        vec2 q = vec2(flip.x != 0 ? 1.0 - pic.x : pic.x, flip.y != 0 ? 1.0 - pic.y : pic.y);\n"
    "        ivec2 sp = clamp(ivec2(floor(q * vec2(ssize))), ivec2(0), ssize - 1);\n"
    "        if (u_rgb != 0 || SAMPLES > 1) {\n"
    "            vec3 sum = vec3(0.0), first = vec3(0.0);\n"
    "            bool alike = true;\n"
    "            for (int s = 0; s < SAMPLES; s++) {\n"
    "                uint m = texelFetch(scene, sp, s).r;\n"
    "                vec3 c;\n"
    "                if (u_rgb != 0) {\n"
    "                    c = texelFetch(scene_colour, sp, s).rgb;\n"
    "                    if (mode >= 2u) c *= texelFetch(lut_fit, ivec2(0, int(mode) - 2), 0).rgb;\n"
    "                } else {\n"
    "                    if (mode >= 2u) m = texelFetch(luts, ivec2(int(m), int(mode) - 2), 0).r;\n"
    "                    c = texelFetch(palette, ivec2(int(m), 0), 0).rgb;\n"
    "                }\n"
    "                if (s == 0) first = c; else alike = alike && c == first;\n"
    "                sum += lin(c);\n"
    "            }\n"
    "            vec3 c = alike ? first : enc(sum / float(SAMPLES));\n"
    "            if (u_rgb != 0) c = (c + (u_to.rgb - c) * u_to.a) * u_fade;\n"
    "            colour = vec4(c, 1.0);\n"
    "            return;\n"
    "        }\n"
    "        n = texelFetch(scene, sp, 0).r;\n"
    "        if (mode >= 2u) n = texelFetch(luts, ivec2(int(n), int(mode) - 2), 0).r;\n"
    "    }\n"
    "    colour = texelFetch(palette, ivec2(int(n), 0), 0);\n"
    "}\n";

/* M3, MSAA: the scene back at fb's size -- each pixel the first sample of the card's pixel at the
   middle of fb's (as the nearest blit for one sample a pixel) */
static const char *const g_readback_src =
    "layout(binding = 0) uniform usampler2DMS m_index;\n"
    "layout(binding = 1) uniform sampler2DMS m_zsw;\n"
    "layout(binding = 2) uniform isampler2DMS m_id;\n"
    "uniform vec2 u_factor;\n"
    "layout(location = 0) out uint o_index;\n"
    "layout(location = 1) out float o_zsw;\n"
    "layout(location = 2) out int o_id;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy * u_factor);\n"
    "    o_index = texelFetch(m_index, p, 0).r; o_zsw = texelFetch(m_zsw, p, 0).r; o_id = texelFetch(m_id, p, 0).r;\n"
    "}\n";

/* M3: a material's texture frame as a colour layer, 256 x 256 from its first byte (rows 256 bytes
   apart): its own rectangle, and beyond it its edge again -- no other texture's texels in the
   filtering or the smaller copies (routine 160: the whole, as it wraps).  Colour times alpha;
   alpha 0 for a keyed one's colour 0 and for what is past the file's end */
static const char *const g_layers_src =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(binding = 0) uniform usamplerBuffer mlb;\n"
    "layout(binding = 1) uniform sampler2D pal;\n"
    "layout(binding = 0, rgba8) uniform writeonly image2DArray layers;\n"
    "struct layer_rec { int offset, avail, su, sv, kind, pad0, pad1, pad2; };\n"
    "layout(std430, binding = 1) readonly buffer layer_list { layer_rec L[]; };\n"
    "void main() {\n"
    "    ivec3 g = ivec3(gl_GlobalInvocationID);\n"
    "    layer_rec r = L[g.z];\n"
    "    int x = g.x, y = g.y;\n"
    "    if ((r.kind & 4) == 0) { x = min(x, r.su - 1); y = min(y, r.sv - 1); }\n"
    "    int o = y * 256 + x;\n"
    "    vec4 c = vec4(0.0);\n"
    "    if (o < r.avail) {\n"
    "        uint n = texelFetch(mlb, r.offset + o).r;\n"
    "        float a = (r.kind & 1) != 0 && n == 0u ? 0.0 : 1.0;\n"
    "        c = vec4(texelFetch(pal, ivec2(int(n), 0), 0).rgb * a, a);\n"
    "    }\n"
    "    imageStore(layers, g, c);\n"
    "}\n";

static GLuint shader(GLenum kind, const char *defs, const char *src)
{
    const GLuint sh = G->CreateShader(kind);
    const GLchar *part[3] = { GLSL_VERSION, defs, src };
    GLint ok = 0;
    G->ShaderSource(sh, 3, part, NULL);
    G->CompileShader(sh);
    G->GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        G->GetShaderInfoLog(sh, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "gpu: a shader did not compile (%s):\n%s\n", defs[0] ? defs : "as drawn", log);
        G->DeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint program_of(GLuint vs, GLuint fs)
{
    if (!vs || !fs) {
        if (vs) G->DeleteShader(vs);
        if (fs && fs != vs) G->DeleteShader(fs);
        return 0;
    }
    const GLuint p = G->CreateProgram();
    GLint ok = 0;
    G->AttachShader(p, vs);
    if (fs != vs) G->AttachShader(p, fs);
    G->LinkProgram(p);
    G->DeleteShader(vs);
    if (fs != vs) G->DeleteShader(fs);
    G->GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        G->GetProgramInfoLog(p, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "gpu: the shaders did not link:\n%s\n", log);
        return 0;
    }
    return p;
}

/* the program of a vertex and a fragment shader, with the variant's #defines ("" none) */
static GLuint program_d(const char *vsrc, const char *fsrc, const char *defs)
{
    return program_of(shader(GL_VERTEX_SHADER, defs, vsrc), shader(GL_FRAGMENT_SHADER, defs, fsrc));
}

static GLuint program(const char *vsrc, const char *fsrc) { return program_d(vsrc, fsrc, ""); }

static GLuint program_compute(const char *src)
{
    const GLuint cs = shader(GL_COMPUTE_SHADER, "", src);
    return program_of(cs, cs);
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

/* M3: a target with `samples` samples a pixel (MSAA), w x h */
static GLuint texture_ms(int samples, GLenum format, int w, int h)
{
    GLuint t = 0;
    G->GenTextures(1, &t);
    G->BindTexture(GL_TEXTURE_2D_MULTISAMPLE, t);
    G->TexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, w, h, GL_TRUE);
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

/* an R8UI texture of w x h filled from `pixels` (made again when the size changes) */
static void bytes_up(GLuint tex, int *tw, int *th, int w, int h, const uint8_t *pixels)
{
    G->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    G->BindTexture(GL_TEXTURE_2D, tex);
    if (w != *tw || h != *th) {
        G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
        *tw = w; *th = h;
    }
    G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
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
    G->Disable(GL_DITHER);                          /* (filtered colours come out the same each time) */
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

/* M3, FILTERING: each row of a table of palette numbers (SMG's, a darkening's) as a straight line
   on the colours, per channel -- out = a x in + b, fitted over the palette's 256 -- so that a
   filtered colour goes through it smoothly (the numbers would bring the palette's steps back).
   through_0: a darkening, a factor alone (b 0) */
static void rows_fit(GLuint tex, const uint8_t *table, int rows, const uint32_t *pal, int through_0)
{
    float *out = malloc(sizeof *out * 8 * (size_t)(rows > 0 ? rows : 1));
    if (!out || rows <= 0) { free(out); return; }
    for (int r = 0; r < rows; r++) {
        for (int ch = 0; ch < 3; ch++) {
            const int sh = 16 - 8 * ch;
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (int i = 0; i < 256; i++) {
                const double x = (double)(pal[i] >> sh & 255) / 255.0, y = (double)(pal[table[r * 256 + i]] >> sh & 255) / 255.0;
                sx += x; sy += y; sxx += x * x; sxy += x * y;
            }
            const double var = sxx - sx * sx / 256.0, cov = sxy - sx * sy / 256.0;
            double a = through_0 ? (sxx > 1e-6 ? sxy / sxx : 1.0) : var > 1e-6 ? cov / var : 1.0;
            a = a < 0.0 ? 0.0 : a > 2.0 ? 2.0 : a;
            out[r * 8 + ch] = (float)a;
            out[r * 8 + 4 + ch] = through_0 ? 0.0f : (float)((sy - a * sx) / 256.0);
        }
        out[r * 8 + 3] = 1.0f; out[r * 8 + 7] = 0.0f;
    }
    G->BindTexture(GL_TEXTURE_2D, tex);
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2, rows, 0, GL_RGBA, GL_FLOAT, out);
    free(out);
}

/* M3: the picture with the card's scene under it (drawn larger, filtered, with MSAA), into the
   viewport x, y, w, h */
static void draw_mixed(int x, int y, int w, int h)
{
    const int ms = G->samples > 1;
    const gpu_prog *p = &G->mix[ms];
    G->Viewport(x, y, w, h);
    G->UseProgram(p->prog);
    G->Uniform2i(p->size, G->pic_w, G->pic_h);
    G->Uniform2i(p->ssize, G->scw, G->sch);
    G->Uniform4f(p->area, (float)x, (float)y, (float)w, (float)h);
    G->Uniform2i(p->flip, G->flip & 1, (G->flip >> 1) & 1);
    G->Uniform1i(p->rgb, G->filter);
    G->Uniform1i(p->samples, G->samples);
    G->Uniform4f(p->to, (float)(G->to >> 16 & 255) / 255.0f, (float)(G->to >> 8 & 255) / 255.0f, (float)(G->to & 255) / 255.0f, G->to_k);
    G->Uniform1f(p->fade, G->fade);
    const GLuint tex[8] = { G->picture, G->palette, G->layer_tex, ms ? G->m_index : G->t_index, G->lut_tex,
                            !G->filter ? 0 : ms ? G->m_colour : G->t_colour, 0, G->filter ? G->lut_fit : 0 };
    for (int k = 0; k < 8; k++) {
        G->ActiveTexture(GL_TEXTURE0 + (GLenum)k);
        G->BindTexture(ms && (k == 3 || k == 5) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, tex[k]);
    }
    G->BindVertexArray(G->vao);
    G->DrawArrays(GL_TRIANGLES, 0, 3);
    G->ActiveTexture(GL_TEXTURE0);
}

void gpu_overlay(const uint8_t *layer, const uint8_t *luts, int n, int flip)
{
    if (!gpu() || !G->scene_card || !layer) return;
    bytes_up(G->layer_tex, &G->layer_w, &G->layer_h, G->vw, G->vh, layer);
    static const uint8_t none[256];
    int tw = 0, th = 0;
    bytes_up(G->lut_tex, &tw, &th, 256, n > 0 ? n : 1, n > 0 ? luts : none);
    if (G->filter && n > 0) rows_fit(G->lut_fit, luts, n, G->pal_now, 1);
    G->flip = flip;
    G->overlay_ok = 1;
}

/* a test: the next picture shown also into `path` (PPM), at the size its scene was drawn at */
static char g_shot[512];
void gpu_shot_next(const char *path) { snprintf(g_shot, sizeof g_shot, "%s", path); }

static void shot_write(void)
{
    const int w = G->scene_card ? G->scw : G->pic_w, h = G->scene_card ? G->sch : G->pic_h;
    GLuint fbo = 0;
    const GLuint tex = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    G->GenFramebuffers(1, &fbo);
    G->BindFramebuffer(GL_FRAMEBUFFER, fbo);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    uint32_t *got = malloc((size_t)w * (size_t)h * sizeof *got);
    FILE *f = got ? fopen(g_shot, "wb") : NULL;
    if (f && G->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        if (G->scene_card && G->overlay_ok) draw_mixed(0, 0, w, h); else draw(0, 0, w, h);
        G->PixelStorei(GL_PACK_ALIGNMENT, 4);
        G->ReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, got);
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--)              /* (the texture's row 0 is the picture's bottom) */
            for (int x = 0; x < w; x++) {
                const uint32_t c = got[(size_t)y * w + x];
                const unsigned char rgb[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
                fwrite(rgb, 1, 3, f);
            }
    }
    if (f) fclose(f);
    free(got);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->DeleteFramebuffers(1, &fbo);
    G->DeleteTextures(1, &tex);
    g_shot[0] = 0;
}

void gpu_again(void)
{
    if (gpu() && G->last_card && G->last_overlay) G->scene_card = G->overlay_ok = 1;
}

void gpu_fades(float fade, uint32_t to, float to_k)
{
    if (!gpu()) return;
    G->fade = fade; G->to = to; G->to_k = to_k;
}

/* a test, OS_GPUTIME=1: the card's clock at each mark of a picture (0 the scene begins, 1 its
   backdrop is done, 2 its faces, 3 read back; 4 the picture shown begins, 5 shown), and every 120
   pictures the means of the parts, in ms */
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
static GLuint g_tq[6];
static int g_tq_on = -1, g_tq_n;
static double g_tq_sum[5];
static void gpu_mark(int k)
{
    if (g_tq_on < 0) g_tq_on = getenv("OS_GPUTIME") != NULL;
    if (!g_tq_on) return;
    if (!g_tq[0]) G->GenQueries(6, g_tq);
    G->QueryCounter(g_tq[k], GL_TIMESTAMP);
    if (k != 5) return;
    uint64_t at[6];
    for (int i = 0; i < 6; i++) G->GetQueryObjectui64v(g_tq[i], GL_QUERY_RESULT, &at[i]);
    for (int i = 0; i < 5; i++) if (i != 3) g_tq_sum[i] += (double)(at[i + 1] - at[i]) * 1e-6;
    if (++g_tq_n == 120) {
        printf("gpu time: backdrop %.2f, faces %.2f, read back %.2f, shown %.2f ms\n", g_tq_sum[0] / 120, g_tq_sum[1] / 120,
               g_tq_sum[2] / 120, g_tq_sum[4] / 120);
        fflush(stdout);
        g_tq_n = 0; memset(g_tq_sum, 0, sizeof g_tq_sum);
    }
}

void gpu_present(void)
{
    if (!gpu()) return;
    int dw = 0, dh = 0, w = 0, h = 0;
    if (g_shot[0]) shot_write();
    SDL_GL_GetDrawableSize(G->win, &dw, &dh);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->Viewport(0, 0, dw, dh);
    G->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    G->Clear(GL_COLOR_BUFFER_BIT);
    gpu_area(&w, &h);                   /* the picture's shape, as large as it fits, in the middle */
    if (g_tq_on > 0 && g_tq[0]) gpu_mark(4);
    if (G->pic_w > 0 && w > 0 && h > 0) {
        if (G->scene_card && G->overlay_ok && G->pic_w == G->layer_w && G->pic_h == G->layer_h)
            draw_mixed((dw - w) / 2, (dh - h) / 2, w, h);
        else draw((dw - w) / 2, (dh - h) / 2, w, h);
    }
    if (g_tq_on > 0 && g_tq[0]) gpu_mark(5);
    G->last_card = G->scene_card; G->last_overlay = G->overlay_ok;
    G->scene_card = G->overlay_ok = 0;
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

static void colour_open(void);

/* a program made, and its uniforms found (0: it did not compile) */
static int prog_make(gpu_prog *p, GLuint prog)
{
    memset(p, 0, sizeof *p);
    p->prog = prog;
    if (!prog) return 0;
    static const char *const name[] = { "u_size", "u_factor", "u_gscale", "u_smg_rows", "u_tie", "u_half", "u_cy", "u_focal",
                                        "u_mode", "u_sky0", "u_sky1", "u_sky2",
                                        "ssize", "area", "flip", "u_rgb", "u_samples", "u_to", "u_fade" };
    GLint *at[] = { &p->size, &p->factor, &p->gscale, &p->smg_rows, &p->tie, &p->half, &p->cy, &p->focal,
                    &p->mode, &p->sky0, &p->sky1, &p->sky2,
                    &p->ssize, &p->area, &p->flip, &p->rgb, &p->samples, &p->to, &p->fade };
    for (size_t k = 0; k < sizeof at / sizeof at[0]; k++) *at[k] = G->GetUniformLocation(prog, name[k]);
    if (p->size < 0) p->size = G->GetUniformLocation(prog, "size");      /* (the picture's is "size") */
    return 1;
}

static void scene_open(void)
{
    if (!prog_make(&G->pass0[0], program(g_scene_vertex_src, g_scene0_src)) ||
        !prog_make(&G->pass1[0], program(g_scene_vertex_src, g_scene1_src)) ||
        !prog_make(&G->back[0], program(g_vertex_src, g_back_src)) ||
        !prog_make(&G->mix[0], program(g_vertex_src, g_mix_src))) return;
    G->fb_tex = texture();
    G->sky_tex = texture();
    G->layer_tex = texture();
    G->lut_tex = texture();
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
    if (G->scene_ok) colour_open();
}

/* M3: FILTERING and MSAA -- their functions, shaders, the card's limits, the frames' samplers.
   Anything missing and only they are not there (colour_ok 0): the scene as drawn stays */
static void colour_open(void)
{
    const char *missing = NULL;
#define GPU_LOAD_(ret, name, args) \
    G->name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name); \
    if (!G->name && !missing) missing = "gl" #name;
    GPU_GL_COLOUR(GPU_LOAD_)
#undef GPU_LOAD_
    if (missing) { fprintf(stderr, "gpu: OpenGL has no %s -- no FILTERING, no MSAA\n", missing); return; }
    while (G->GetError() != GL_NO_ERROR) {}
    static const char *const defs[4] = { "", "#define RGB 1\n", "#define MS 1\n", "#define RGB 1\n#define MS 1\n" };
    int ok = 1;
    for (int v = 1; v < 4; v++) {
        ok &= prog_make(&G->pass0[v], program_d(g_scene_vertex_src, g_scene0_src, defs[v]));
        ok &= prog_make(&G->pass1[v], program_d(g_scene_vertex_src, g_scene1_src, defs[v]));
        ok &= prog_make(&G->back[v], program_d(g_vertex_src, g_back_src, defs[v]));
    }
    ok &= prog_make(&G->mix[1], program_d(g_vertex_src, g_mix_src, defs[2]));
    ok &= prog_make(&G->readback, program(g_vertex_src, g_readback_src));
    G->prog_layers = program_compute(g_layers_src);
    if (!ok || !G->prog_layers) { fprintf(stderr, "gpu: no FILTERING, no MSAA (a shader of theirs)\n"); return; }
    /* MSAA: as many samples as every kind of target can have */
    GLint n = 0, most = 8;
    const GLenum lim[4] = { GL_MAX_SAMPLES, GL_MAX_INTEGER_SAMPLES, GL_MAX_COLOR_TEXTURE_SAMPLES, GL_MAX_DEPTH_TEXTURE_SAMPLES };
    for (int k = 0; k < 4; k++) { n = 0; G->GetIntegerv(lim[k], &n); if (n < most) most = n; }
    G->max_samples = most >= 8 ? 8 : most >= 4 ? 4 : most >= 2 ? 2 : 1;
    /* anisotropic filtering, where the card has it (an extension in 4.5, core in 4.6) */
    GLfloat aniso = 0.0f;
    if (SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic") || SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
        G->GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
    G->aniso = aniso >= 2.0f ? (aniso < 16.0f ? aniso : 16.0f) : 0.0f;
    GLuint s[2];
    G->GenSamplers(2, s);
    for (int k = 0; k < 2; k++) {
        G->SamplerParameteri(s[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        G->SamplerParameteri(s[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        G->SamplerParameteri(s[k], GL_TEXTURE_WRAP_S, k ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        G->SamplerParameteri(s[k], GL_TEXTURE_WRAP_T, k ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        if (G->aniso > 0.0f) G->SamplerParameterf(s[k], GL_TEXTURE_MAX_ANISOTROPY, G->aniso);
    }
    G->samp_clamp = s[0]; G->samp_wrap = s[1];
    G->BindSampler(10, G->samp_clamp);         /* (units 10 and 11: the frames', no one else's) */
    G->BindSampler(11, G->samp_wrap);
    G->pal_tex = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    G->smg_fit = texture();
    G->lut_fit = texture();
    G->GenBuffers(1, &G->layer_buf);
    G->colour_ok = G->GetError() == GL_NO_ERROR;
    while (G->GetError() != GL_NO_ERROR) {}
    printf("display: FILTERING %s, MSAA up to %dX (anisotropic filtering %s)\n", G->colour_ok ? "there" : "NOT there",
           G->colour_ok ? G->max_samples : 0, G->aniso > 0.0f ? "too" : "not there");
}

int gpu_scene_ready(void) { return gpu() && G->scene_ok && G->mlb_buf; }

void gpu_quality(int filtering, int msaa)
{
    if (!gpu()) return;
    G->want_filter = filtering != 0;
    G->want_msaa = msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 0;
}

/* the samples the next scene is drawn with (1: none) */
static int samples_wanted(void)
{
    if (!G->colour_ok || G->want_msaa < 2) return 1;
    return G->want_msaa < G->max_samples ? G->want_msaa : G->max_samples;
}

int gpu_colour(void) { return gpu() && G->scene_ok && G->colour_ok && (G->want_filter || samples_wanted() > 1); }
int gpu_samples(void) { return gpu() && G->scene_ok ? samples_wanted() : 1; }
int gpu_msaa_max(void) { return gpu() && G->scene_ok && G->colour_ok ? G->max_samples : 0; }

/* M3: the materials' frames, each (place, extent, kind) one colour layer; made with the palette
   when FILTERING first needs them (layers_make) */
static int layer_of(int offset, int avail, int su, int sv, int kind, int *hash, int hash_n)
{
    su = su < 1 ? 1 : su > 256 ? 256 : su;
    sv = sv < 1 ? 1 : sv > 256 ? 256 : sv;
    kind &= GPU_KEYED | GPU_WRAPS;
    unsigned h = ((unsigned)offset * 2654435761u ^ (unsigned)(su * 977 + sv * 131 + kind)) % (unsigned)hash_n;
    for (;; h = (h + 1) % (unsigned)hash_n) {
        const int k = hash[h];
        if (k < 0) break;
        const gpu_layer *l = &G->layer_rec[k];
        if (l->offset == offset && l->su == su && l->sv == sv && l->kind == kind) return k;
    }
    gpu_layer *l = &G->layer_rec[G->nlayer];
    memset(l, 0, sizeof *l);
    l->offset = offset; l->avail = avail; l->su = su; l->sv = sv; l->kind = kind;
    hash[h] = G->nlayer;
    return G->nlayer++;
}

void gpu_materials(const uint8_t *bytes, long len, const gpu_mat *mat, int nmat, const int *frame_offset)
{
    if (!gpu() || !G->scene_ok || !bytes || len <= 0) return;
    buffer_fill(G->mlb_buf, G->mlb_tex, GL_R8UI, bytes, len);
    int total = 0;
    for (int i = 0; i < nmat; i++) total += mat[i].frames > 0 ? mat[i].frames : 0;
    const int hash_n = 2 * total + 1;
    G->mat_first = (realloc)(G->mat_first, sizeof(int) * (size_t)(nmat + 1));
    G->mat_frames = (realloc)(G->mat_frames, sizeof(int) * (size_t)(nmat + 1));
    G->frame_layer = (realloc)(G->frame_layer, sizeof(int) * (size_t)(total + 1));
    G->layer_rec = (realloc)(G->layer_rec, sizeof(gpu_layer) * (size_t)(total + 1));
    int *hash = malloc(sizeof *hash * (size_t)hash_n);
    G->nmat = G->nframe = G->nlayer = 0;
    G->layers_made = G->layers_no = 0;
    G->sky_pix = NULL;                              /* (a new part: its backdrop may lie where the last one's did) */
    if (!G->mat_first || !G->mat_frames || !G->frame_layer || !G->layer_rec || !hash) { free(hash); return; }
    for (int k = 0; k < hash_n; k++) hash[k] = -1;
    for (int i = 0; i < nmat; i++) {
        const int frames = mat[i].frames > 0 ? mat[i].frames : 0;
        G->mat_first[i] = G->nframe;
        G->mat_frames[i] = frames;
        for (int f = 0; f < frames; f++) {
            const int o = frame_offset[mat[i].first + f];
            G->frame_layer[G->nframe++] = o >= 0 && o < len && !(mat[i].kind & GPU_GLASS)
                ? layer_of(o, (int)(len - o), mat[i].su, mat[i].sv, mat[i].kind, hash, hash_n) : -1;
        }
    }
    free(hash);
    G->nmat = nmat;
}

/* the frames' colour layers gone (FILTERING off, or more of them) */
static void layers_free(void)
{
    if (G->layers) G->DeleteTextures(1, &G->layers);
    if (G->layers_rgba) G->DeleteTextures(1, &G->layers_rgba);
    G->layers = G->layers_rgba = 0;
    G->layers_cap = 0; G->layers_made = 0;
}

/* the frames' colour layers, for this palette (in the scene's palette texture): made on the card,
   then their smaller copies */
static void layers_make(void)
{
    if (G->nlayer <= 0 || !G->prog_layers) return;
    if (G->nlayer > G->layers_cap) {
        layers_free();
        while (G->GetError() != GL_NO_ERROR) {}
        G->GenTextures(1, &G->layers);
        G->BindTexture(GL_TEXTURE_2D_ARRAY, G->layers);
        G->TexStorage3D(GL_TEXTURE_2D_ARRAY, 9, GL_SRGB8_ALPHA8, 256, 256, G->nlayer);
        G->GenTextures(1, &G->layers_rgba);         /* (an image cannot be sRGB) */
        G->TextureView(G->layers_rgba, GL_TEXTURE_2D_ARRAY, G->layers, GL_RGBA8, 0, 1, 0, (GLuint)G->nlayer);
        G->layers_cap = G->nlayer;
        const GLenum e = G->GetError();
        if (e != GL_NO_ERROR) {
            fprintf(stderr, "gpu: no room for the textures' colour layers (%d, OpenGL error 0x%x): no FILTERING\n", G->nlayer, e);
            layers_free();
            G->layers_no = 1;
            while (G->GetError() != GL_NO_ERROR) {}
            return;
        }
    }
    G->BindBuffer(GL_SHADER_STORAGE_BUFFER, G->layer_buf);
    G->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(gpu_layer) * (size_t)G->nlayer), G->layer_rec, GL_STATIC_DRAW);
    G->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, G->layer_buf);
    G->UseProgram(G->prog_layers);
    G->ActiveTexture(GL_TEXTURE0); G->BindTexture(GL_TEXTURE_BUFFER, G->mlb_tex);
    G->ActiveTexture(GL_TEXTURE1); G->BindTexture(GL_TEXTURE_2D, G->pal_tex);
    G->ActiveTexture(GL_TEXTURE0);
    G->BindImageTexture(0, G->layers_rgba, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    G->DispatchCompute(16, 16, (GLuint)G->nlayer);
    G->MemoryBarrier(GL_ALL_BARRIER_BITS);
    G->BindTexture(GL_TEXTURE_2D_ARRAY, G->layers);
    G->GenerateMipmap(GL_TEXTURE_2D_ARRAY);
    G->layers_made = 1;
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
    G->c_stale = 1;                                 /* (the colour's targets hold the old ones) */
}

/* M3: the pass 0 and pass 1 targets: with a colour (FILTERING) it is at 0, then the palette
   number, the software's depth, the owner; without, those from 0 (pass 1: colour and number, or
   the number) */
static int scene_fbos(GLuint *fbo0, GLuint *fbo1, GLenum target, GLuint colour, GLuint index, GLuint zsw, GLuint id, GLuint depth)
{
    const GLenum all[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
    const GLuint t[4] = { colour, index, zsw, id };
    const int c = colour ? 0 : 1, n = 4 - c;
    G->GenFramebuffers(1, fbo0);
    G->BindFramebuffer(GL_FRAMEBUFFER, *fbo0);
    for (int k = 0; k < n; k++) G->FramebufferTexture2D(GL_FRAMEBUFFER, all[k], target, t[c + k], 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, target, depth, 0);
    G->DrawBuffers(n, all);
    const int ok = G->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    G->GenFramebuffers(1, fbo1);
    G->BindFramebuffer(GL_FRAMEBUFFER, *fbo1);
    for (int k = 0; k < 2 - c; k++) G->FramebufferTexture2D(GL_FRAMEBUFFER, all[k], target, t[c + k], 0);
    G->DrawBuffers(2 - c, all);
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

/* M3, FILTERING without MSAA: the colour beside scene_targets' */
static void colour_targets(int w, int h)
{
    if (w == G->cw && h == G->ch && !G->c_stale) return;
    if (G->fbo0c) {
        G->DeleteTextures(1, &G->t_colour);
        G->DeleteFramebuffers(1, &G->fbo0c);
        G->DeleteFramebuffers(1, &G->fbo1c);
    }
    G->t_colour = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    if (!scene_fbos(&G->fbo0c, &G->fbo1c, GL_TEXTURE_2D, G->t_colour, G->t_index, G->t_zsw, G->t_id, G->t_depth))
        fprintf(stderr, "gpu: the colour scene's targets are not complete\n");
    G->cw = w; G->ch = h; G->c_stale = 0;
}

/* M3: MSAA's targets gone (MSAA off, or another size) */
static void ms_free(void)
{
    if (G->fbo0m) {
        const GLuint t[5] = { G->m_colour, G->m_index, G->m_zsw, G->m_id, G->m_depth };
        G->DeleteTextures(5, t);
        G->DeleteFramebuffers(1, &G->fbo0m);
        G->DeleteFramebuffers(1, &G->fbo1m);
        G->fbo0m = G->fbo1m = 0;
        G->m_colour = 0;
    }
    G->mw = G->mh = G->ms = G->m_rgb = 0; G->ms_got = 1;
}

/* M3: MSAA's targets, `samples` a pixel, the colour too with FILTERING; fewer samples when the card
   has no room for them.  Gives the samples made (1: none) */
static int ms_targets(int w, int h, int samples, int rgb)
{
    if (w == G->mw && h == G->mh && samples == G->ms && rgb == G->m_rgb) return G->ms_got;
    ms_free();
    G->mw = w; G->mh = h; G->ms = samples; G->m_rgb = rgb;
    for (int s = samples; s >= 2; s /= 2) {
        while (G->GetError() != GL_NO_ERROR) {}
        G->m_colour = rgb ? texture_ms(s, GL_RGBA8, w, h) : 0;
        G->m_index = texture_ms(s, GL_R8UI, w, h);
        G->m_zsw = texture_ms(s, GL_R32F, w, h);
        G->m_id = texture_ms(s, GL_R16I, w, h);
        G->m_depth = texture_ms(s, GL_DEPTH32F_STENCIL8, w, h);
        GLenum e = G->GetError();
        if (e == GL_NO_ERROR) {
            if (scene_fbos(&G->fbo0m, &G->fbo1m, GL_TEXTURE_2D_MULTISAMPLE, G->m_colour, G->m_index, G->m_zsw, G->m_id, G->m_depth)) {
                G->ms_got = s;
                return s;
            }
            e = GL_INVALID_FRAMEBUFFER_OPERATION;
        }
        fprintf(stderr, "gpu: MSAA %dX at %d x %d: no room on the card (OpenGL error 0x%x), trying fewer\n", s, w, h, e);
        const GLuint t[5] = { G->m_colour, G->m_index, G->m_zsw, G->m_id, G->m_depth };
        G->DeleteTextures(5, t);
        if (G->fbo0m) { G->DeleteFramebuffers(1, &G->fbo0m); G->DeleteFramebuffers(1, &G->fbo1m); G->fbo0m = G->fbo1m = 0; }
        G->m_colour = 0;
    }
    return 1;
}

/* M3: fb's size, for a scene drawn larger to be read back through (each pixel as it is at the
   middle of fb's) */
static void scene_targets_lo(int w, int h)
{
    if (w == G->lo_w && h == G->lo_h) return;
    if (G->fbo_lo) {
        const GLuint t[3] = { G->t_lo_index, G->t_lo_zsw, G->t_lo_id };
        G->DeleteTextures(3, t);
        G->DeleteFramebuffers(1, &G->fbo_lo);
    }
    G->t_lo_index = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    G->t_lo_zsw = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);
    G->t_lo_id = texture();
    G->TexImage2D(GL_TEXTURE_2D, 0, GL_R16I, w, h, 0, GL_RED_INTEGER, GL_SHORT, NULL);
    G->GenFramebuffers(1, &G->fbo_lo);
    G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo_lo);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G->t_lo_index, 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, G->t_lo_zsw, 0);
    G->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, G->t_lo_id, 0);
    if (G->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "gpu: the read-back targets are not complete\n");
    G->BindFramebuffer(GL_FRAMEBUFFER, 0);
    G->lo_w = w; G->lo_h = h;
}

static int g_shape_w = 640, g_shape_h = 400;
void gpu_shape(int w, int h) { if (w > 0 && h > 0) { g_shape_w = w; g_shape_h = h; } }

/* the window's part the picture fills: as large as it fits, in the middle (640 x 400 is 4:3: fb's
   pixels are 6:5, taller than wide, as 320 x 200 always was) */
void gpu_area(int *w, int *h)
{
    int dw = 0, dh = 0;
    if (gpu()) SDL_GL_GetDrawableSize(G->win, &dw, &dh);
    const double a = (double)g_shape_w * 5.0 / ((double)g_shape_h * 6.0);
    if ((double)dw > (double)dh * a) dw = (int)((double)dh * a + 0.5);
    else dh = (int)((double)dw / a + 0.5);
    *w = dw; *h = dh;
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

/* pass 0 over records a..b: the solid faces, in order; later ones win ties (raster_job:
   !(mine < there)).  A car's blocks come the last in the engine's order first, and the stencil
   keeps each from the pixels of the ones before it: raster_job's rkb.  M3: vk the way it is drawn
   (1 FILTERING, 2 MSAA) */
static void draw_solid(int vk, int a, int b, int w, int h, int rw, int rows, float fx, float fy)
{
    const gpu_prog *p = &G->pass0[vk];
    G->BindFramebuffer(GL_FRAMEBUFFER, vk == 0 ? G->fbo0 : vk == 1 ? G->fbo0c : G->fbo0m);
    G->DepthMask(GL_TRUE);
    G->Scissor(0, 0, rw, rows);
    G->Enable(GL_SCISSOR_TEST);
    G->Enable(GL_DEPTH_TEST);
    G->DepthFunc(GL_GEQUAL);
    G->StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    G->BindVertexArray(G->vao3);
    G->ActiveTexture(GL_TEXTURE0);
    G->BindTexture(GL_TEXTURE_BUFFER, G->mlb_tex);
    G->UseProgram(p->prog);
    G->Uniform2f(p->size, (float)w, (float)h);
    G->Uniform2f(p->factor, fx, fy);
    if (vk & 1) {                                   /* M3, FILTERING: the frames' colour layers (a sample's share of a pixel's steps) */
        G->Uniform1f(p->gscale, 1.0f / sqrtf((float)G->samples));
        G->ActiveTexture(GL_TEXTURE1); G->BindTexture(GL_TEXTURE_2D, G->pal_tex);
        G->ActiveTexture(GL_TEXTURE10); G->BindTexture(GL_TEXTURE_2D_ARRAY, G->layers);
        G->ActiveTexture(GL_TEXTURE11); G->BindTexture(GL_TEXTURE_2D_ARRAY, G->layers);
        G->ActiveTexture(GL_TEXTURE0);
    }
    for (int i = a, car = -1; i < b; ) {
        const int rank = G->rec[i].rank, id = G->rec[i].id;
        int j = i + 1;
        while (j < b && G->rec[j].rank == rank && G->rec[j].id == id) j++;
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
}

/* pass 1 over records a..b: routine 224, reading what is there -- triangles that overlap are
   drawn one after another, with a barrier between */
static void draw_glass(int vk, const gpu_view *v, int a, int b, float fx, float fy)
{
    const gpu_prog *p = &G->pass1[vk];
    const int ms = (vk & 2) != 0;
    G->Disable(GL_DEPTH_TEST);
    G->DepthMask(GL_FALSE);
    G->BindFramebuffer(GL_FRAMEBUFFER, vk == 0 ? G->fbo1 : vk == 1 ? G->fbo1c : G->fbo1m);
    G->BindVertexArray(G->vao3);
    G->UseProgram(p->prog);
    G->Uniform2f(p->size, (float)v->w, (float)v->h);
    G->Uniform2f(p->factor, fx, fy);
    G->Uniform1i(p->smg_rows, v->smg_rows);
    G->Uniform1i(p->tie, v->tie_rule);
    G->Uniform1i(p->half, v->smg_half);
    G->Uniform1f(p->cy, v->cy);
    G->Uniform1f(p->focal, v->focal_y);
    G->ActiveTexture(GL_TEXTURE0); G->BindTexture(GL_TEXTURE_BUFFER, G->mlb_tex);
    const GLuint tex2d[4] = { ms ? G->m_index : G->t_index, ms ? G->m_zsw : G->t_zsw, ms ? G->m_id : G->t_id, ms ? G->m_colour : G->t_colour };
    const GLenum target = ms ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    for (int k = 0; k < 3; k++) { G->ActiveTexture(GL_TEXTURE1 + k); G->BindTexture(target, tex2d[k]); }
    if (vk & 1) {                                   /* M3, FILTERING: the colour there, SMG's rows as lines */
        G->ActiveTexture(GL_TEXTURE7); G->BindTexture(target, tex2d[3]);
        G->ActiveTexture(GL_TEXTURE9); G->BindTexture(GL_TEXTURE_2D, G->smg_fit);
    }
    G->ActiveTexture(GL_TEXTURE4); G->BindTexture(GL_TEXTURE_BUFFER, G->d2_tex);
    G->ActiveTexture(GL_TEXTURE5); G->BindTexture(GL_TEXTURE_BUFFER, G->fl_tex);
    G->ActiveTexture(GL_TEXTURE6); G->BindTexture(GL_TEXTURE_2D, G->smg_tex);
    float box[64][4];
    int nbox = 0, from = a;
    for (int i = a; i <= b; i++) {
        int clash = i == b || nbox == 64;
        float r[4] = { 0, 0, 0, 0 };
        if (i < b) {
            const gpu_corner *c = &G->corner[(size_t)i * 3];
            r[0] = r[2] = c[0].x; r[1] = r[3] = c[0].y;
            for (int k = 1; k < 3; k++) {
                if (c[k].x < r[0]) r[0] = c[k].x;
                if (c[k].x > r[2]) r[2] = c[k].x;
                if (c[k].y < r[1]) r[1] = c[k].y;
                if (c[k].y > r[3]) r[3] = c[k].y;
            }
            for (int j = 0; j < nbox && !clash; j++)
                if (r[0] <= box[j][2] + 1 && r[2] + 1 >= box[j][0] && r[1] <= box[j][3] + 1 && r[3] + 1 >= box[j][1]) clash = 1;
        }
        if (clash && i > from) {
            G->DrawArrays(GL_TRIANGLES, from * 3, (i - from) * 3);
            G->TextureBarrier();
            from = i; nbox = 0;
        }
        if (i < b) memcpy(box[nbox++], r, sizeof r);
    }
    G->ActiveTexture(GL_TEXTURE0);
}

int gpu_scene(const gpu_view *v, const gpu_tri *t, int n, uint8_t *fb, float *zb, int16_t *idb)
{
    if (!gpu() || !G->scene_ok) return 0;
    const int w = v->w, h = v->h;
    const int rw = v->rw > 0 ? v->rw : w, rh = v->rh > 0 ? v->rh : h, big = rw != w || rh != h;
    const float fx = (float)rw / (float)w, fy = (float)rh / (float)h;
    /* M3: FILTERING (the colour too, from the frames' colour layers of this palette) and MSAA */
    const int colour = G->colour_ok && v->pal;
    G->filter = colour && G->want_filter;
    G->samples = colour ? samples_wanted() : 1;
    if (G->filter && (!G->pal_set || memcmp(G->pal_now, v->pal, sizeof G->pal_now))) {
        memcpy(G->pal_now, v->pal, sizeof G->pal_now);
        G->pal_set = 1;
        G->BindTexture(GL_TEXTURE_2D, G->pal_tex);
        G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, G->pal_now);
        G->layers_made = 0;                         /* (the frames' colours are the palette's) */
    }
    if (G->filter && !G->layers_made && !G->layers_no) layers_make();
    if (!G->layers_made) G->filter = 0;
    if (!G->want_filter && G->layers) layers_free();    /* (FILTERING off: their room back) */
    if (G->samples > 1) G->samples = ms_targets(rw, rh, G->samples, G->filter);
    if (G->samples < 2) {
        G->samples = 1;
        if (G->fbo0m) ms_free();
        scene_targets(rw, rh);
        if (G->filter) colour_targets(rw, rh);
    }
    if (big || G->samples > 1) scene_targets_lo(w, h);
    G->vw = w; G->vh = h; G->scw = rw; G->sch = rh;
    const int vk = (G->filter ? 1 : 0) | (G->samples > 1 ? 2 : 0);
    scene_factors(v, t, n);

    /* the triangles, with raster_job's planes worked out as there (the same sums in the same
       order); m of them have pixels, the see-through ones from glass_from on */
    if ((long)n > G->rec_cap) {
        const long cap = (long)n + 4096;
        gpu_rec *r = (realloc)(G->rec, sizeof *r * (size_t)cap);
        if (r) G->rec = r;
        gpu_corner *p = (realloc)(G->corner, sizeof *p * 3 * (size_t)cap);
        if (p) G->corner = p;
        if (!r || !p) return 0;
        G->rec_cap = cap;
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        const gpu_tri *q = &t[i];
        const float ax = q->x[0], ay = q->y[0], bx = q->x[1], by = q->y[1], cx = q->x[2], cy = q->y[2];
        const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (fabsf(area) < 1e-6f) continue;
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
        r->layer = -1; r->pad = 0;
        if (G->filter && q->flat < 0 && q->mat >= 0 && q->mat < G->nmat && G->mat_frames[q->mat] > 0)
            r->layer = G->frame_layer[G->mat_first[q->mat] + (q->frame >= 0 && q->frame < G->mat_frames[q->mat] ? q->frame : 0)];
        gpu_corner *c = &G->corner[(size_t)m * 3];
        c[0].x = ax; c[0].y = ay; c[1].x = bx; c[1].y = by; c[2].x = cx; c[2].y = cy;
        c[0].tri = c[1].tri = c[2].tri = m;
        m++;
    }
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
    if (v->smg && v->smg_rows > 0)
        G->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, v->smg_rows, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, v->smg);
    if (G->filter && v->smg && v->smg_rows > 0) {
        uint32_t sum = (uint32_t)v->smg_rows;           /* (the table and the palette as they were last: no need) */
        for (int i = 0; i < 256 * v->smg_rows; i += 7) sum = sum * 31u + v->smg[i];
        for (int i = 0; i < 256; i++) sum = sum * 31u + G->pal_now[i];
        if (sum != G->smg_fit_sum) { rows_fit(G->smg_fit, v->smg, v->smg_rows, G->pal_now, 0); G->smg_fit_sum = sum; }
    }

    /* the backdrop, and nothing in front of it yet */
    gpu_mark(0);
    if (!big && vk == 0) {
        G->BindTexture(GL_TEXTURE_2D, G->t_index);
        G->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, fb);
    }
    G->BindFramebuffer(GL_FRAMEBUFFER, vk == 0 ? G->fbo0 : vk == 1 ? G->fbo0c : G->fbo0m);
    const GLfloat zero = 0.0f;
    const GLint none = -1;
    const int zi = G->filter ? 2 : 1;               /* (the software's depth's draw buffer; the owner's next) */
    G->DepthMask(GL_TRUE);                          /* (a clear of the depth obeys the mask: pass 1 turns it off) */
    G->ClearBufferfv(GL_COLOR, zi, &zero);
    G->ClearBufferiv(GL_COLOR, zi + 1, &none);
    G->ClearBufferfv(GL_DEPTH, 0, &zero);
    const int rows = big ? (int)((float)v->rows * fy + 0.5f) : v->rows;
    G->Viewport(0, 0, rw, rh);
    if (big || vk != 0) {
        /* M3: fb's picture made larger, then the sky over the scene's rows at the card's size (at
           fb's own size the sky stays the CPU's; FILTERING draws its colours again, filtered) */
        const gpu_prog *p = &G->back[vk];
        bytes_up(G->fb_tex, &G->fbt_w, &G->fbt_h, w, h, fb);
        G->Disable(GL_DEPTH_TEST);
        G->Disable(GL_SCISSOR_TEST);
        G->UseProgram(p->prog);
        G->Uniform2f(p->factor, fx, fy);
        G->Uniform1i(p->mode, 0);
        if (vk & 1) { G->ActiveTexture(GL_TEXTURE1); G->BindTexture(GL_TEXTURE_2D, G->pal_tex); }
        G->ActiveTexture(GL_TEXTURE0);
        G->BindTexture(GL_TEXTURE_2D, G->fb_tex);
        G->BindVertexArray(G->vao);
        G->DrawArrays(GL_TRIANGLES, 0, 3);
        const gpu_sky *s = v->sky;
        if (s && (big || ((vk & 1) && s->sx > 1.5f))) {
            if (s->pix && (s->pix != G->sky_pix || s->w != G->sky_w || s->h != G->sky_h)) {
                int tw = 0, th = 0;
                bytes_up(G->sky_tex, &tw, &th, s->w, s->h, s->pix);
                G->sky_pix = s->pix; G->sky_w = s->w; G->sky_h = s->h;
            }
            if (!big) for (GLuint k = 1; k < 4; k++) G->ColorMaski(k, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            G->Enable(GL_SCISSOR_TEST);
            G->Scissor(0, 0, rw, rows);
            G->Uniform1i(p->mode, s->pix ? 1 : 2);
            G->Uniform4f(p->sky0, s->off, s->cr, s->sr, s->sx);
            G->Uniform4f(p->sky1, s->sy, s->cy, s->top, (float)w);
            G->Uniform4i(p->sky2, s->w, s->h, s->ground, s->clear);
            G->BindTexture(GL_TEXTURE_2D, G->sky_tex);
            G->DrawArrays(GL_TRIANGLES, 0, 3);
            if (!big) for (GLuint k = 1; k < 4; k++) G->ColorMaski(k, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        G->TextureBarrier();                        /* (pass 1 reads what the backdrop wrote: the menu's lists may begin with it) */
    }

    gpu_mark(1);
    /* the runs of solid and see-through faces in the list's order (a race: all solid ones first,
       render_scene's two passes; the menu: an object's own after each other) */
    for (int i = 0; i < m; ) {
        const int glass = (G->rec[i].kind & GPU_GLASS) != 0;
        int j = i + 1;
        while (j < m && ((G->rec[j].kind & GPU_GLASS) != 0) == glass) j++;
        if (glass) draw_glass(vk, v, i, j, fx, fy);
        else draw_solid(vk, i, j, w, h, rw, rows, fx, fy);
        i = j;
    }

    gpu_mark(2);
    /* back to fb, zb and idb (a scene drawn larger: through targets of fb's size, each pixel
       as it is at the middle of fb's; FILTERING: the number, depth and owner are the colour's
       next three; MSAA: the first sample of each) */
    G->Disable(GL_SCISSOR_TEST);
    G->Disable(GL_DEPTH_TEST);
    G->DepthMask(GL_TRUE);
    GLenum from = GL_COLOR_ATTACHMENT0;
    if (vk & 2) {
        const GLenum all[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo_lo);
        G->DrawBuffers(3, all);
        G->Viewport(0, 0, w, h);
        G->UseProgram(G->readback.prog);
        G->Uniform2f(G->readback.factor, fx, fy);
        const GLuint ms[3] = { G->m_index, G->m_zsw, G->m_id };
        for (int k = 0; k < 3; k++) { G->ActiveTexture(GL_TEXTURE0 + (GLenum)k); G->BindTexture(GL_TEXTURE_2D_MULTISAMPLE, ms[k]); }
        G->ActiveTexture(GL_TEXTURE0);
        G->BindVertexArray(G->vao);
        G->DrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        const GLenum first = vk ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT0;
        G->BindFramebuffer(GL_FRAMEBUFFER, vk ? G->fbo0c : G->fbo0);
        if (big) {
            G->BindFramebuffer(GL_DRAW_FRAMEBUFFER, G->fbo_lo);
            for (int k = 0; k < 3; k++) {
                const GLenum at = GL_COLOR_ATTACHMENT0 + (GLenum)k, at3[3] = { k == 0 ? at : GL_NONE, k == 1 ? at : GL_NONE, k == 2 ? at : GL_NONE };
                G->ReadBuffer(first + (GLenum)k);
                G->DrawBuffers(3, at3);
                G->BlitFramebuffer(0, 0, rw, rh, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            G->BindFramebuffer(GL_FRAMEBUFFER, G->fbo_lo);
        } else from = first;
    }
    G->scene_card = big || vk != 0;
    G->PixelStorei(GL_PACK_ALIGNMENT, 1);
    G->ReadBuffer(from);
    G->ReadPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, fb);
    G->ReadBuffer(from + 1);
    G->ReadPixels(0, 0, w, h, GL_RED, GL_FLOAT, zb);
    G->ReadBuffer(from + 2);
    G->ReadPixels(0, 0, w, h, GL_RED_INTEGER, GL_SHORT, idb);
    gpu_mark(3);
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
        for (int i = 0; i < m; i++) puffs += (G->rec[i].kind & (GPU_SPRITE | GPU_GLASS)) == (GPU_SPRITE | GPU_GLASS);
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
    return G->scene_card;
}
