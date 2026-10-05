# Lava Lamp Digits

A Pebble watchface for **Pebble Time 2 (emery)** and **Pebble Round 2 (gabbro)**.
Blobs of lava drift up and down a lamp, then pour themselves into the time,
hold for a few seconds, droop and melt apart again.

![Time 2](screenshots/preview_emery.gif) ![Round 2](screenshots/preview_gabbro.gif)

## Showing the time

The time forms when you:

- **tap the screen** (both watches have touchscreens)
- **flick your wrist** (the accelerometer's tap event)
- **raise your wrist** (optional, see below)
- open the face, or change its settings
- let the minute tick over while the time is showing (it melts and re-forms)
- wait: it also forms on its own every 5 minutes by default

After the time melts, the lava keeps drifting for 30 seconds and then freezes
to save battery. The next glance wakes it.

### Raise to show

Pebble has no wrist-raise event, so `main.c` reads the accelerometer at 10Hz
(batches of 5, so the watch wakes twice a second) and looks for the screen
swinging from facing sideways or down (`z > -200`) to facing up
(`z < -550`) within 1.5s, with two settled face-up samples in a row and a 3s
cooldown. It's a heuristic. A slow, smooth raise may not trigger it, and
swinging your arms might sometimes trigger it by mistake. It can be turned off
in settings. Tapping and flicking always work.

## Settings (Clay)

| Setting | Default | Notes |
|---|---|---|
| Lava | Orange `FF5500` | Glowing core = each channel +1; edge = midpoint with the liquid |
| Liquid | Imperial purple `550055` | Bottom glow = each channel +1, dithered |
| Raise your wrist to show the time | On | Accelerometer subscription, see above |
| Also form the time on its own | Every 5 minutes | Every minute / 5 / 15 / never |
| Keep the lava moving | 30s after a glance | "Always" never freezes. Heavy on battery |

Uses `@rebble/clay`, not `pebble-clay`, which refuses to build for gabbro.

## How it works

`src/c/lava.c`: 150 metaballs. 120 are packed (sunflower spiral) into 5
blobs whose centres follow slow sums of sines, stretching into a teardrop when
they travel fast. 30 form a pool along the bottom of the glass. Each ball adds
`(1 - d²/R²)²` to a field. Pixels above thresholds become lava (`T_MAIN`), the
blob's glowing core (`T_CORE`, only reached inside packed blobs), or a one-pixel
blended edge (`T_RIM`).

Rendering is one row at a time straight into the framebuffer, with balls
sorted by top edge so each row only visits the balls that reach it. Positions
are Q4 fixed point so slow motion doesn't step. Each frame takes about 2–3ms
in the emulator, and the app uses about 17KB of the 128KB RAM.

`src/c/digits.c`: numerals are polylines in a 0..100 box, sampled at even
spacing into one target point per ball. Forming the time matches each target
to its nearest free blob ball. Unused blob balls drain into the pool. Each
ball blends from where its blob would put it to its target, so blobs stretch
into streams on the way. Melting adds a per-ball gravity sag before letting go.
The time is stacked (hours over minutes). In 12-hour mode a single-digit hour
is centred.

## Build and test

```bash
pebble build                          # release bundle: build/lava-lamp-digits.pbw
pebble install --emulator emery       # or gabbro
python3 tools/shot.py emery out.png   # screenshot via the QEMU monitor
python3 tools/gif.py emery out.gif 70 0.18
pebble emu-tap --emulator emery       # flick
pebble emu-accel --emulator emery custom tools/accel_raise.txt   # wrist raise
```

Verification-only build flags (never ship a bundle built with them):

- `LAVA_DEBUG=1 pebble build` draws fps, render ms, phase, target count and
  input/raise counters on screen.
- `LAVA_HOLD=1 pebble build` keeps the time formed forever.

Emulator screendumps write colour channels on a reduced scale (emery
0/33/66/100, gabbro 0/60/120/180) under a `255` header, so raw captures look
dim and muddy. `tools/shot.py` and `tools/gif.py` rescale them to the real colours.
