/*
 * THE CD'S SONGS.  Both games play their music from the disc's audio tracks (Screamer 2: the
 * CD driver's "play track n", 0x4d5b4; the menu song 2, a race the song MISC.DTR's CD_TRACKS
 * has for its track.  Screamer: 0x2636c, song n of MENU.DTR's CD_TRACKS as track n + 1.  Round
 * and round, both).  The port has no disc: track n is a file
 *
 *     track_NN.ogg / track_NN.mp3 / track_NN.wav     (NN = 02 .. 99, two digits)
 *
 * looked for in the game's directory's "music" folder, then in the game's directory, then in
 * the port's home's "music" folder (the first found, in that order of folders, then of kinds);
 * or, with no file for it, the track in the disc's own image, where the game's directory has
 * one (GOG's copies: a cue sheet, SCREAM.DAT / GAME.DAT, and its sectors, SCREAM.gog /
 * GAME.GOG).  Both are the game's: never published.
 *
 * The song is read as it plays, in the mixer's thread (sound.c, under its lock): decoded a few
 * thousand frames at a time, each output frame the average of the song's frames it spans (the
 * mixer runs at 22050 a second, a CD at 44100), mono doubled to both sides.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include "port.h"
#include "sound.h"

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#include "third_party/dr_mp3.h"
#include "third_party/dr_wav.h"

enum { CD_NONE, CD_OGG, CD_MP3, CD_WAV, CD_IMAGE };
static struct {
    int kind, loop, track;
    FILE *img;                      /* CD_IMAGE: the sectors, the track's bytes, where it is */
    long img_start, img_end, img_at;
    int img_motorola;               /* (the cue's MOTOROLA: high byte first) */
    stb_vorbis *ogg;
    drmp3 mp3;
    drwav wav;
    int channels, rate;
    int16_t buf[4096 * 2];          /* decoded, as two sides */
    int have, at;                   /* frames in buf, the next one */
    float frac, step;               /* how far into frame `at`; song frames an output frame */
    float gain;
    int paused;                     /* (the race's CD player: PLAY again goes on from here) */
} g_cd;
static float g_cd_volume = 1.0f;    /* the CD's own volume (0x4d7c0: 0..255 of each side, here 0..1) */

/*
 * THE DISC'S IMAGE: a cue sheet in the game's directory (any file there that begins with FILE)
 * and the files of sectors it names, 2352 bytes a sector, an audio track's sound as it is
 * (44100 frames a second, 16 bits, two sides).  A track's bytes run from its INDEX 01 to the
 * next track's INDEX 00 or 01 in the same file, or to the file's end.
 */
#define CD_SECTOR 2352
static struct {
    int read;                       /* (looked for this part: the statics are put back between parts) */
    int motorola[100];
    char file[100][600];            /* by track: its file ("" no audio there) */
    long start[100], end[100];      /* its bytes in that file (end -1: to the file's end) */
} g_img;

/* an INDEX line's number and its place in sectors (mm:ss:ff, 75 to a second), or -1 */
static long cue_index(const char *p, int *which)
{
    int m = 0, sec = 0, f = 0;
    return sscanf(p, "INDEX %d %d:%d:%d", which, &m, &sec, &f) == 4 ? ((long)m * 60 + sec) * 75 + f : -1;
}

/* the cue sheet in `path`, its files in `dir`: 1 if it was one */
static int cue_read(const char *dir, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[700], file[600] = "", real[600] = "";
    int track = 0, audio = 0, motorola = 0, ok = 0, lines = 0, prev = 0;
    while (fgets(line, sizeof line, f) && lines++ < 400) {
        char *p = line;
        while (*p == ' ' || *p == '\t' || (unsigned char)*p == 0xef || (unsigned char)*p == 0xbb || (unsigned char)*p == 0xbf) p++;
        if (!strncmp(p, "FILE ", 5)) {
            char *a = strchr(p, '"'), *b = a ? strchr(a + 1, '"') : NULL;
            if (!a || !b) break;
            snprintf(file, sizeof file, "%.*s", (int)(b - a - 1), a + 1);
            motorola = strstr(b, "MOTOROLA") != NULL;
            real[0] = 0;                                    /* (the name as it is on the disk, any letter case) */
            DIR *d = opendir(dir);
            for (struct dirent *e; d && (e = readdir(d)) != NULL; )
                if (!strcasecmp(e->d_name, file)) { snprintf(real, sizeof real, "%s/%s", dir, e->d_name); break; }
            if (d) closedir(d);
            prev = 0;
            ok = 1;
        } else if (!strncmp(p, "TRACK ", 6) && ok) {
            track = atoi(p + 6);
            audio = strstr(p, "AUDIO") != NULL && real[0] && track > 0 && track < 100;
        } else if (!strncmp(p, "INDEX ", 6) && ok && track > 0 && track < 100) {
            int which = 0;
            const long at = cue_index(p, &which);
            if (at < 0) continue;
            if (prev > 0 && prev < track && g_img.end[prev] < 0 && !strcmp(g_img.file[prev], real))
                g_img.end[prev] = at * CD_SECTOR;           /* (the track before ends where this one begins) */
            if (which == 1) {
                if (audio) {
                    snprintf(g_img.file[track], sizeof g_img.file[track], "%s", real);
                    g_img.start[track] = at * CD_SECTOR; g_img.end[track] = -1;
                    g_img.motorola[track] = motorola;
                }
                prev = track;
            }
        } else if (!ok && *p && *p != '\r' && *p != '\n' && strncmp(p, "REM", 3)) break;   /* (not a cue sheet) */
    }
    fclose(f);
    return ok;
}

static void cd_image_read(void)
{
    if (g_img.read) return;
    g_img.read = 1;
    const char *dir = os_data_dir();
    DIR *d = opendir(dir);
    if (!d) return;
    char path[1200];
    for (struct dirent *e; (e = readdir(d)) != NULL; ) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (strcasecmp(dot, ".cue") && strcasecmp(dot, ".dat"))) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (cue_read(dir, path)) {
            if (getenv("OS_MUSICLOG")) printf("cd: the disc's image, %s\n", path);
            break;
        }
    }
    closedir(d);
}

/* the image has track n's sound */
static int cd_image_has(int track)
{
    cd_image_read();
    return track > 0 && track < 100 && g_img.file[track][0];
}

static int cd_open_file(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (!strcmp(dot, ".ogg")) {
        int err = 0;
        g_cd.ogg = stb_vorbis_open_filename(path, &err, NULL);
        if (!g_cd.ogg) return 0;
        const stb_vorbis_info in = stb_vorbis_get_info(g_cd.ogg);
        g_cd.kind = CD_OGG; g_cd.channels = in.channels; g_cd.rate = (int)in.sample_rate;
    } else if (!strcmp(dot, ".mp3")) {
        if (!drmp3_init_file(&g_cd.mp3, path, NULL)) return 0;
        g_cd.kind = CD_MP3; g_cd.channels = (int)g_cd.mp3.channels; g_cd.rate = (int)g_cd.mp3.sampleRate;
    } else if (!strcmp(dot, ".wav")) {
        if (!drwav_init_file(&g_cd.wav, path, NULL)) return 0;
        g_cd.kind = CD_WAV; g_cd.channels = (int)g_cd.wav.channels; g_cd.rate = (int)g_cd.wav.sampleRate;
    } else return 0;
    if (g_cd.channels < 1 || g_cd.rate < 1000) { cd_audio_stop(); return 0; }
    return 1;
}

void cd_audio_stop(void)
{
    if (g_cd.kind == CD_OGG && g_cd.ogg) stb_vorbis_close(g_cd.ogg);
    if (g_cd.kind == CD_MP3) drmp3_uninit(&g_cd.mp3);
    if (g_cd.kind == CD_WAV) drwav_uninit(&g_cd.wav);
    if (g_cd.img) fclose(g_cd.img);
    memset(&g_cd, 0, sizeof g_cd);
}

/* the file of track n (out ""; the disc's image has it), or 0 */
int cd_audio_find(int track, char *out, size_t n)
{
    static const char *const kinds[] = { "ogg", "mp3", "wav" };
    char dirs[3][512];
    snprintf(dirs[0], sizeof dirs[0], "%s/music", os_data_dir());
    snprintf(dirs[1], sizeof dirs[1], "%s", os_data_dir());
    snprintf(dirs[2], sizeof dirs[2], "%s/music", os_home_dir());
    for (int d = 0; d < 3; d++)
        for (int k = 0; k < 3; k++) {
            snprintf(out, n, "%s/track_%02d.%s", dirs[d], track, kinds[k]);
            FILE *f = fopen(os_path(out), "rb");
            if (f) { char real[600]; fclose(f); snprintf(real, sizeof real, "%s", os_path(out)); snprintf(out, n, "%s", real); return 1; }
        }
    if (cd_image_has(track)) { if (n) out[0] = 0; return 1; }
    return 0;
}

/* track n from its start, round and round if `loop`; 0 if there is no file for it */
int cd_audio_play(int track, int loop, int out_rate)
{
    char path[600];
    cd_audio_stop();
    if (!cd_audio_find(track, path, sizeof path)) {
        if (getenv("OS_MUSICLOG")) printf("cd: no file for track %d\n", track);
        return 0;
    }
    if (!path[0]) {                                 /* the disc's image */
        g_cd.img = fopen(g_img.file[track], "rb");
        if (!g_cd.img) { fprintf(stderr, "cd: %s cannot be read\n", g_img.file[track]); return 0; }
        long end = g_img.end[track];
        if (end < 0 && fseek(g_cd.img, 0, SEEK_END) == 0) end = ftell(g_cd.img);
        g_cd.img_start = g_cd.img_at = g_img.start[track];
        g_cd.img_end = end;
        g_cd.img_motorola = g_img.motorola[track];
        if (g_cd.img_end - g_cd.img_start < 4 || fseek(g_cd.img, g_cd.img_start, SEEK_SET) != 0) { cd_audio_stop(); return 0; }
        g_cd.kind = CD_IMAGE; g_cd.channels = 2; g_cd.rate = 44100;
        snprintf(path, sizeof path, "%s (%ld seconds)", g_img.file[track], (g_cd.img_end - g_cd.img_start) / 176400);
    } else if (!cd_open_file(path)) { fprintf(stderr, "cd: %s cannot be read\n", path); return 0; }
    g_cd.loop = loop; g_cd.track = track;
    g_cd.step = (float)g_cd.rate / (float)out_rate;
    if (getenv("OS_MUSICLOG")) printf("cd: track %d from %s (%d a second, %d side(s))%s\n", track, path, g_cd.rate, g_cd.channels, loop ? ", round and round" : "");
    return 1;
}

int cd_audio_track(void) { return g_cd.kind ? g_cd.track : 0; }
void cd_audio_pause(int on) { g_cd.paused = on != 0; }
int cd_audio_paused(void) { return g_cd.kind && g_cd.paused; }
void cd_audio_volume(float v) { g_cd_volume = v < 0.0f ? 0.0f : v > 2.0f ? 2.0f : v; }

/* more of the song into buf; 0 at its end (when not looping) */
static int cd_decode(void)
{
    static int16_t raw[4096 * 8];
    const int want = 4096, ch = g_cd.channels > 8 ? 8 : g_cd.channels;
    for (int tries = 0; tries < 2; tries++) {
        long got = 0;
        if (g_cd.kind == CD_IMAGE) {
            long frames = (g_cd.img_end - g_cd.img_at) / 4;
            if (frames > want) frames = want;
            const size_t n = frames > 0 ? fread(raw, 4, (size_t)frames, g_cd.img) : 0;
            g_cd.img_at += (long)n * 4;
            const unsigned char *b = (const unsigned char *)raw;
            for (size_t i = 0; i < n * 2; i++)
                g_cd.buf[i] = (int16_t)(g_cd.img_motorola ? (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]) : (uint16_t)(b[2 * i] | b[2 * i + 1] << 8));
            got = (long)n;
        } else if (g_cd.kind == CD_OGG) {
            /* (stb_vorbis gives the sides it is asked for: one, or two) */
            got = stb_vorbis_get_samples_short_interleaved(g_cd.ogg, ch >= 2 ? 2 : 1, raw, want * (ch >= 2 ? 2 : 1));
            if (got > 0) {
                for (long i = 0; i < got; i++) {
                    g_cd.buf[2 * i] = raw[ch >= 2 ? 2 * i : i];
                    g_cd.buf[2 * i + 1] = raw[ch >= 2 ? 2 * i + 1 : i];
                }
            }
        } else {
            const long cap = (long)(sizeof raw / sizeof raw[0]) / ch, n = want < cap ? want : cap;
            got = g_cd.kind == CD_MP3 ? (long)drmp3_read_pcm_frames_s16(&g_cd.mp3, (drmp3_uint64)n, raw)
                                      : (long)drwav_read_pcm_frames_s16(&g_cd.wav, (drwav_uint64)n, raw);
            for (long i = 0; i < got; i++) {
                g_cd.buf[2 * i] = raw[i * ch];
                g_cd.buf[2 * i + 1] = raw[i * ch + (ch >= 2 ? 1 : 0)];
            }
        }
        if (got > 0) { g_cd.have = (int)got; g_cd.at = 0; return 1; }
        if (!g_cd.loop) return 0;
        /* the end: from the start again (0x4d630 plays it again as the drive stops) */
        if (g_cd.kind == CD_OGG) stb_vorbis_seek_start(g_cd.ogg);
        if (g_cd.kind == CD_MP3) drmp3_seek_to_pcm_frame(&g_cd.mp3, 0);
        if (g_cd.kind == CD_WAV) drwav_seek_to_pcm_frame(&g_cd.wav, 0);
        if (g_cd.kind == CD_IMAGE) { g_cd.img_at = g_cd.img_start; if (fseek(g_cd.img, g_cd.img_start, SEEK_SET) != 0) return 0; }
    }
    return 0;
}

/* one output frame, added to *left / *right at `gain` (the mixer's thread) */
void cd_audio_frame(float *left, float *right, float gain)
{
    if (!g_cd.kind || g_cd.paused) return;
    float need = g_cd.step, l = 0.0f, r = 0.0f;
    while (need > 0.0f) {
        if (g_cd.at >= g_cd.have && !cd_decode()) { cd_audio_stop(); return; }
        float part = 1.0f - g_cd.frac;
        if (part > need) part = need;
        l += (float)g_cd.buf[2 * g_cd.at] * part;
        r += (float)g_cd.buf[2 * g_cd.at + 1] * part;
        need -= part;
        g_cd.frac += part;
        if (g_cd.frac >= 0.9999f) { g_cd.frac = 0.0f; g_cd.at++; }
    }
    const float k = gain * g_cd_volume / g_cd.step;
    *left += l * k;
    *right += r * k;
}
