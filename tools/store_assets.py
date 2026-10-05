#!/usr/bin/env python3
"""Build the appstore image set from emulator recordings.

Expects recordings made with tools/record.py from a `LAVA_SHOW_TIME=1008`
build, one per platform and palette, in <rec_dir>/<platform>_<palette>/:

  orange  12s, flick at 1.0s  (default colours: GIF, formed time, pour)
  red      6s, flick at 0.5s  (red lava in yellow: formed time)
  blue    13s, flick at 0.5s  (blue lava in navy: drifting blobs)

Writes into store/:
  <platform>_1_preview.gif   pour -> hold -> melt -> drift, real-time
  <platform>_2_time.png      formed time, default colours
  <platform>_3_pour.png      mid-pour
  <platform>_4_red.png       formed time, red/yellow
  <platform>_5_drift.png     drifting blobs, blue/navy
  icon_80x80.png, icon_144x144.png   (pebble publish: iconSmall / iconLarge)
  banner_720x320.png                 (store header image)

Screenshot filenames start with the platform because `pebble publish` infers
the platform from the text before the first underscore.

Usage: store_assets.py <rec_dir>
"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), '..')
OUT = os.path.join(ROOT, 'store')

# Frames chosen by eye from contact sheets of the 2026-10-05 recordings.
PICKS = {
    'emery': {'time': 70, 'pour': 36, 'red': 50, 'drift': 148, 'gif': (14, 150)},
    'gabbro': {'time': 70, 'pour': 37, 'red': 50, 'drift': 136, 'gif': (14, 150)},
}

# The watch's default palette (see lava_set_palette): lava, core, rim, liquid.
LAVA = (255, 85, 0)
CORE = (255, 170, 85)
RIM = (170, 85, 0)
LIQUID = (85, 0, 85)
GLOW = (170, 85, 170)


def frame(rec, name, i):
    return Image.open(os.path.join(rec, name, f'f{i:03d}.png')).convert('RGB')


def save_gif(rec, platform, start, end):
    d = os.path.join(rec, f'{platform}_orange')
    times = json.load(open(os.path.join(d, 'times.json')))
    frames = [frame(rec, f'{platform}_orange', i) for i in range(start, min(end, len(times)))]
    # Exact colours: the watch has at most 64, so build one shared palette
    # from them instead of letting the encoder dither.
    colours = sorted({c for f in frames for _, c in f.getcolors(4096)})
    pal_img = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal_img.putpalette(flat + [0] * (768 - len(flat)))
    pframes = [f.quantize(palette=pal_img, dither=Image.Dither.NONE) for f in frames]
    durations = [max(20, int(round((times[i + 1] - times[i]) * 1000)))
                 for i in range(start, start + len(frames) - 1)] + [600]
    path = os.path.join(OUT, f'{platform}_1_preview.gif')
    pframes[0].save(path, save_all=True, append_images=pframes[1:], duration=durations,
                    loop=0, optimize=False, disposal=1)
    return path


# ------------------------------------------------------------ metaballs --

def field_image(size, balls, liquid=LIQUID, glow_from=None, ss=4):
    """Renders metaballs with the watch's kernel and thresholds, supersampled
    so the phone-sized artwork has smooth edges. balls: (x, y, r) in output px."""
    W, H = size[0] * ss, size[1] * ss
    img = Image.new('RGB', (W, H), liquid)
    px = img.load()
    scaled = [(x * ss, y * ss, r * ss) for x, y, r in balls]
    for y in range(H):
        row_balls = [(bx, by, r) for bx, by, r in scaled if abs(y - by) < r]
        if glow_from is not None and y > glow_from * ss:
            t = (y - glow_from * ss) / (H - glow_from * ss)
            bg = tuple(int(liquid[k] + (GLOW[k] - liquid[k]) * 0.35 * t) for k in range(3))
        else:
            bg = liquid
        for x in range(W):
            v = 0.0
            for bx, by, r in row_balls:
                q = 1 - ((x - bx) ** 2 + (y - by) ** 2) / (r * r)
                if q > 0:
                    v += q * q * 256
            if v >= 1500:
                px[x, y] = CORE
            elif v >= 185:
                px[x, y] = LAVA
            elif v >= 150:
                px[x, y] = RIM
            elif bg != liquid:
                px[x, y] = bg
    return img.resize(size, Image.LANCZOS)


def blob(cx, cy, radius, n, r):
    """A packed blob like the watch's: n balls on a sunflower spiral."""
    import math
    out = []
    for k in range(n):
        rad = radius * math.sqrt((k + 0.5) / n)
        a = k * 2.39996
        out.append((cx + rad * math.cos(a), cy + rad * math.sin(a), r))
    return out


def pool(width, y, r, n):
    return [((i + 0.5) * width / n, y, r) for i in range(n)]


def make_icon(size):
    s = size / 144
    balls = []
    balls += blob(60 * s, 50 * s, 24 * s, 36, 21 * s)        # big blob
    balls += blob(100 * s, 86 * s, 13 * s, 14, 19 * s)       # splitting off it
    balls += blob(40 * s, 118 * s, 8 * s, 7, 16 * s)         # rising from the pool
    balls += pool(size, size + 4 * s, 19 * s, 10)            # pool along the bottom
    return field_image((size, size), balls, glow_from=size * 0.6)


def round_mask(im):
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, im.width - 1, im.height - 1), fill=255)
    return m


def make_banner(rec):
    W, H = 720, 320
    balls = []
    balls += blob(24, 34, 12, 9, 17)
    balls += blob(298, 40, 11, 7, 16)
    balls += blob(706, 20, 9, 6, 15)
    balls += pool(W, H + 4, 16, 50)
    bg = field_image((W, H), balls, glow_from=H * 0.55, ss=2)

    # Both screens at 85% so they sit side by side without hiding each other.
    scale = 0.85
    emery = frame(rec, 'emery_orange', PICKS['emery']['time'])
    gabbro = frame(rec, 'gabbro_blue', 65)
    emery = emery.resize((round(emery.width * scale), round(emery.height * scale)), Image.LANCZOS)
    gabbro = gabbro.resize((round(gabbro.width * scale), round(gabbro.height * scale)), Image.LANCZOS)
    gx, gy = W - gabbro.width - 22, (H - gabbro.height) // 2
    ex, ey = gx - emery.width - 24, (H - emery.height) // 2
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((ex - 6, ey - 6, ex + emery.width + 6, ey + emery.height + 6), 16,
                         fill=(0, 0, 0, 140))
    sd.ellipse((gx - 6, gy - 6, gx + gabbro.width + 6, gy + gabbro.height + 6), fill=(0, 0, 0, 140))
    shadow = shadow.filter(ImageFilter.GaussianBlur(8))
    out = bg.convert('RGBA')
    out.alpha_composite(shadow)
    # Watch bezels: a dark rim around each screen.
    bez = ImageDraw.Draw(out)
    bez.rounded_rectangle((ex - 5, ey - 5, ex + emery.width + 4, ey + emery.height + 4), 14,
                          fill=(30, 30, 34))
    bez.ellipse((gx - 5, gy - 5, gx + gabbro.width + 4, gy + gabbro.height + 4), fill=(30, 30, 34))
    out.paste(emery, (ex, ey))
    out.paste(gabbro, (gx, gy), round_mask(gabbro))

    d = ImageDraw.Draw(out)
    title = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf', 44)
    tag = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf', 18)
    d.text((30, 96), 'Lava Lamp', font=title, fill=CORE)
    d.text((30, 144), 'Digits', font=title, fill=LAVA)
    d.text((32, 206), 'Lava that pours itself', font=tag, fill=(235, 210, 235))
    d.text((32, 229), 'into the time', font=tag, fill=(235, 210, 235))
    return out.convert('RGB')


def main():
    rec = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    written = []
    for platform, p in PICKS.items():
        written.append(save_gif(rec, platform, *p['gif']))
        for slot, name, key in [(2, 'orange', 'time'), (3, 'orange', 'pour'),
                                (4, 'red', 'red'), (5, 'blue', 'drift')]:
            path = os.path.join(OUT, f'{platform}_{slot}_{key}.png')
            frame(rec, f'{platform}_{name}', p[key]).save(path)
            written.append(path)
    for size in (80, 144):
        path = os.path.join(OUT, f'icon_{size}x{size}.png')
        make_icon(size).save(path)
        written.append(path)
    path = os.path.join(OUT, 'banner_720x320.png')
    make_banner(rec).save(path)
    written.append(path)
    for w in written:
        im = Image.open(w)
        print(f'{os.path.relpath(w, ROOT):32s} {im.size[0]}x{im.size[1]}  {os.path.getsize(w) // 1024}KB')


if __name__ == '__main__':
    main()
