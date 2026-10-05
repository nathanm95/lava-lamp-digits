#!/usr/bin/env python3
"""Screenshot a running Pebble emulator via QEMU's own monitor socket.

`pebble screenshot` goes through pypkjs, which wedges permanently the first
time a button is pressed. The QEMU monitor is a separate path and keeps
working. Usage: shot.py <platform>[@sdk] <out.png>

Colours are rescaled to true 0..255 (see rescale()).
"""
import re, socket, subprocess, sys, time, os

def monitor_port(platform):
    """`platform` is e.g. `gabbro`, or `gabbro@4.17` to pick the emulator of a
    particular SDK when more than one instance of a platform is running (each
    SDK keeps its own flash image, so a second SDK gives an isolated watch)."""
    plat, _, sdk = platform.partition('@')
    ps = subprocess.run(['ps', 'ax', '-o', 'command'], capture_output=True, text=True).stdout
    for line in ps.splitlines():
        if 'qemu-pebble' not in line or plat not in line:
            continue
        if sdk and f'Pebble SDK/{sdk}/{plat}/' not in line:
            continue
        m = re.search(r'-monitor tcp::(\d+)', line)
        if m:
            return int(m.group(1))
    raise SystemExit(f'no running qemu-pebble for {platform}')

def rescale(ppm):
    """The dump claims maxval 255, but each platform writes its 2-bit channels
    on a smaller scale (emery 0/33/66/100, gabbro 0/60/120/180), so captures
    look dim and muddy. The lowest non-zero level is always a third of full
    scale, so stretch from that back to the colours the watch actually shows."""
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    if parts[0] != b'P6' or len(parts) < 4:
        return
    pixels = parts[3]
    levels = sorted(set(pixels) - {0})
    if not levels:
        return
    full = levels[0] * 3
    if full >= 255 or any(v % levels[0] > 1 and levels[0] - v % levels[0] > 1 for v in levels):
        return
    table = bytes(min(255, round(v * 255 / full)) for v in range(256))
    with open(ppm, 'wb') as f:
        f.write(b'\n'.join(parts[:3]) + b'\n' + pixels.translate(table))

def main():
    platform, out = sys.argv[1], sys.argv[2]
    ppm = '/tmp/pebble_shot.ppm'
    if os.path.exists(ppm):
        os.remove(ppm)
    s = socket.create_connection(('127.0.0.1', monitor_port(platform)), timeout=5)
    time.sleep(0.3)
    s.recv(65536)
    s.sendall(f'screendump {ppm}\n'.encode())
    time.sleep(0.8)
    s.close()
    if not os.path.exists(ppm):
        raise SystemExit('screendump produced no file')
    rescale(ppm)
    subprocess.run(['sips', '-s', 'format', 'png', ppm, '--out', out],
                   check=True, capture_output=True)
    print(out)

if __name__ == '__main__':
    main()
