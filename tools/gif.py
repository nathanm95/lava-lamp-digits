#!/usr/bin/env python3
"""Record an animated GIF from a running Pebble emulator via the QEMU monitor.

Holds one monitor connection open and screendumps repeatedly, so frames come
much faster than repeated shot.py calls. Colours are rescaled the same way as
shot.py. Usage: gif.py <platform> <out.gif> [frames] [interval_s] [scale]
"""
import os, socket, sys, time
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from shot import monitor_port, rescale  # noqa: E402


def main():
    platform, out = sys.argv[1], sys.argv[2]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    interval = float(sys.argv[4]) if len(sys.argv) > 4 else 0.2
    scale = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    tmp = '/tmp/pebble_gif'
    os.makedirs(tmp, exist_ok=True)
    s = socket.create_connection(('127.0.0.1', monitor_port(platform)), timeout=5)
    time.sleep(0.3)
    s.recv(65536)
    paths = []
    for i in range(frames):
        p = f'{tmp}/f{i:03d}.ppm'
        if os.path.exists(p):
            os.remove(p)
        s.sendall(f'screendump {p}\n'.encode())
        paths.append(p)
        time.sleep(interval)
    time.sleep(0.8)
    s.close()
    images = []
    for p in paths:
        if not os.path.exists(p):
            continue
        rescale(p)
        im = Image.open(p).convert('RGB')
        if scale > 1:
            im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        images.append(im.convert('P', palette=Image.ADAPTIVE, colors=64))
    images[0].save(out, save_all=True, append_images=images[1:],
                   duration=int(interval * 1000), loop=0, optimize=True)
    print(out, len(images), 'frames')


if __name__ == '__main__':
    main()
