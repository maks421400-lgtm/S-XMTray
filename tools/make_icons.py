"""Generate original headphone glyphs, without image or build dependencies."""
import math
import struct
from pathlib import Path

out = Path(__file__).resolve().parents[1] / "assets"
out.mkdir(exist_ok=True)
for name, color in {"quiet": (120, 128, 140), "anc": (77, 143, 246),
                    "ambient": (38, 174, 135), "off": (155, 133, 186)}.items():
    images = []
    for size in (16, 32, 48, 64):
        pixels = bytearray()
        # 4x supersampling keeps the small tray icon readable at different DPI.
        for y in reversed(range(size)):
            for x in range(size):
                accum = [0, 0, 0, 0]
                for sy in range(4):
                    for sx in range(4):
                        u = (x + (sx + .5) / 4) / size
                        v = (y + (sy + .5) / 4) / size
                        if math.hypot(u - .5, v - .5) > .48:
                            continue
                        arc = v <= .52 and .22 <= math.hypot(u - .5, v - .49) <= .30
                        pads = .46 <= v <= .75 and (.20 <= u <= .32 or .68 <= u <= .80)
                        rgb = (255, 255, 255) if arc or pads else color
                        for k in range(3): accum[k] += rgb[k]
                        accum[3] += 255
                count = accum[3] / 255
                if count:
                    pixels.extend((round(accum[2]/count), round(accum[1]/count), round(accum[0]/count), round(accum[3]/16)))
                else: pixels.extend((0,0,0,0))
        mask = bytes(((size + 31) // 32) * 4 * size)
        dib = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
        images.append((size, dib + pixels + mask))
    offset = 6 + len(images) * 16
    entries = bytearray()
    for size, data in images:
        entries += struct.pack('<BBBBHHII', size, size, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    (out / f'{name}.ico').write_bytes(struct.pack('<HHH', 0, 1, len(images)) + entries + b''.join(d for _,d in images))
