#!/usr/bin/env python3
"""The GPU renderer against the software one: the same pictures from both, pixel by pixel.

Usage:  python3 tools/gpu_ab.py --exe port/build/openscreamer --data screamer --out /tmp/ab
            [--tracks 1-12] [--views 6] [--race 1500] [--every 250] [--res low,high] [--jobs 4]

Views along each track (--shot) and a robot's race (OS_DRIVE_DEMO, every --every-th picture,
cars and smoke in it).  The software side runs with OS_UVRUN=1: the texture's place exact every
pixel, as on the card.  A differing pixel is an EDGE when its colour is a neighbour's in the other
picture (an edge a hair to one side); the rest are ODD.  Sheets (software | card | differences)
are written for the pictures with odd pixels.  Standard library only.
"""
import argparse, concurrent.futures as cf, os, struct, subprocess, sys, zlib


def read_ppm(path):
    with open(path, "rb") as f:
        d = f.read()
    parts = d.split(maxsplit=4)
    if parts[0] != b"P6":
        raise ValueError(path + ": not a P6 picture")
    w, h, top = int(parts[1]), int(parts[2]), int(parts[3])
    return w, h, d[len(d) - w * h * 3:]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda kind, data: (struct.pack(">I", len(data)) + kind + data
                                + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def compare(a, b, w, h):
    """(edge, odd, the first odd pixels, a mask of the differing pixels: 1 edge, 2 odd)"""
    px = lambda d, i: d[i * 3:i * 3 + 3]
    mask = bytearray(w * h)
    edge = odd = 0
    first = []
    for i in range(w * h):
        if px(a, i) == px(b, i):
            continue
        x, y = i % w, i // w
        near = [(y + dy) * w + x + dx for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                if (dx or dy) and 0 <= x + dx < w and 0 <= y + dy < h]
        if any(px(a, i) == px(b, j) for j in near) or any(px(b, i) == px(a, j) for j in near):
            edge += 1
            mask[i] = 1
        else:
            odd += 1
            mask[i] = 2
            if len(first) < 8:
                first.append((x, y))
    return edge, odd, first, mask


def sheet(path, a, b, mask, w, h, k=2):
    out = bytearray(w * 3 * k * h * k * 3)
    row = w * 3 * k * 3
    for y in range(h):
        for x in range(w):
            i = y * w + x
            g = sum(a[i * 3:i * 3 + 3]) // 6
            diff = (bytes((g, g, g)), bytes((255, 200, 0)), bytes((255, 0, 255)))[mask[i]]
            for n, c in enumerate((a[i * 3:i * 3 + 3], b[i * 3:i * 3 + 3], diff)):
                for yy in range(k):
                    o = (y * k + yy) * row + ((n * w + x) * k) * 3
                    out[o:o + 3 * k] = c * k
    write_png(path, w * 3 * k, h * k, bytes(out))


def run(args, env, cwd, log):
    with open(log, "w") as f:
        r = subprocess.run(args, env=env, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, timeout=900)
    with open(log, errors="replace") as f:
        return r.returncode, f.read()


def case_env(opt, renderer, home, res):
    env = dict(os.environ, OPENSCREAMER_HOME=home, SDL_AUDIODRIVER="dummy",
               OS_DETAIL=str(opt.detail), OS_RES=res, OS_RENDERER=renderer)
    for k in ("OS_UVRUN", "OS_GPUHIDDEN", "OS_DRIVE_DEMO", "OS_SHOTEVERY", "SDL_VIDEODRIVER", "SDL_RENDER_DRIVER"):
        env.pop(k, None)
    if renderer == "software":
        env.update(OS_UVRUN="1", SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
    else:
        env.update(OS_GPUHIDDEN="1")
    return env


def one_case(opt, track, res, what):
    """what: ('view', t) or ('race',): runs both, gives [(name, sw.ppm, gpu.ppm)] and notes"""
    name = f"t{track:02d}_{res}_" + (f"view{what[1]}" if what[0] == "view" else "race")
    d = os.path.join(opt.out, name)
    os.makedirs(d, exist_ok=True)
    data_arg = f"build/dec/MAP{track - 6}I" if track > 6 else f"build/dec/MAP{track}"
    num = str(track - 6 if track > 6 else track)
    notes, pairs = [], []
    for renderer in ("software", "gpu"):
        home = os.path.join(d, "home_" + renderer)
        os.makedirs(home, exist_ok=True)
        env = case_env(opt, renderer, home, res)
        args = [opt.exe, "-data", opt.data, data_arg, num]
        if what[0] == "view":
            args += ["--shot", os.path.join(d, renderer + ".ppm"), str(what[1])]
        else:
            env.update(OS_DRIVE_DEMO=str(opt.race), OS_SHOTEVERY=f"{opt.every},{renderer}")
        code, text = run(args, env, d, os.path.join(d, renderer + ".log"))
        if code != 0:
            notes.append(f"{renderer} ended with {code}")
        if renderer == "gpu" and "the 3D scene on the card" not in text:
            notes.append("the card did NOT draw the scene")
        notes += sorted({l.strip() for l in text.splitlines() if l.startswith("gpu:")})
    if what[0] == "view":
        pairs.append((name, os.path.join(d, "software.ppm"), os.path.join(d, "gpu.ppm")))
    else:
        for f in sorted(os.listdir(d)):
            if f.startswith("software_") and f.endswith(".ppm"):
                pairs.append((name + "_" + f[9:-4], os.path.join(d, f), os.path.join(d, "gpu_" + f[9:])))
    return name, pairs, notes


def tracks(spec):
    out = []
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        out += range(int(lo), int(hi or lo) + 1)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tracks", default="1-12")
    ap.add_argument("--views", type=int, default=6)
    ap.add_argument("--race", type=int, default=1500, help="pictures of a robot's race (0: none)")
    ap.add_argument("--every", type=int, default=250)
    ap.add_argument("--res", default="low")
    ap.add_argument("--detail", type=int, default=2)
    ap.add_argument("--jobs", type=int, default=4)
    opt = ap.parse_args()
    opt.exe, opt.out = os.path.abspath(opt.exe), os.path.abspath(opt.out)
    os.makedirs(opt.out, exist_ok=True)

    cases = []
    for res in opt.res.split(","):
        for t in tracks(opt.tracks):
            cases += [(t, res, ("view", 5 + 47 * k)) for k in range(opt.views)]
            if opt.race > 0:
                cases.append((t, res, ("race",)))
    total = [0, 0, 0, 0]                 # pictures, pixels, edge, odd
    worst = []
    with cf.ThreadPoolExecutor(opt.jobs) as pool:
        for name, pairs, notes in pool.map(lambda c: one_case(opt, *c), cases):
            for note in notes:
                print(f"{name}: {note}")
            for pname, sw, gp in pairs:
                if not (os.path.exists(sw) and os.path.exists(gp)):
                    print(f"{pname}: a picture is missing")
                    continue
                w, h, a = read_ppm(sw)
                w2, h2, b = read_ppm(gp)
                if (w, h) != (w2, h2):
                    print(f"{pname}: {w}x{h} against {w2}x{h2}")
                    continue
                edge, odd, first, mask = compare(a, b, w, h)
                total[0] += 1; total[1] += w * h; total[2] += edge; total[3] += odd
                worst.append((odd, edge, pname))
                line = f"{pname}: {edge + odd} differ ({edge} edge, {odd} odd)"
                if odd:
                    line += "  odd at " + " ".join(f"{x},{y}" for x, y in first)
                    sheet(os.path.join(opt.out, pname + ".png"), a, b, mask, w, h)
                print(line, flush=True)
    if total[0]:
        print(f"\n{total[0]} pictures: {total[2] + total[3]} of {total[1]} pixels differ "
              f"({100.0 * (total[2] + total[3]) / total[1]:.3f} %), {total[2]} edge, {total[3]} odd")
        print("most odd: " + ", ".join(f"{n} ({o})" for o, e, n in sorted(worst, reverse=True)[:8]))


if __name__ == "__main__":
    main()
