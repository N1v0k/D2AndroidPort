"""Arma una imagen con los cuadros de un DC6 puestos en una grilla de 256x256,
como los dibuja el juego en los fondos y paneles partidos en pedazos.

uso: python dc6compose.py archivo.dc6 pal.dat columnas salida.bmp [desde] [cuantos]
"""
import struct
import sys


def read_dc6(data):
    ver, flags, enc, term, dirs, fpd = struct.unpack_from('<6i', data, 0)
    n = dirs * fpd
    ptrs = struct.unpack_from('<%dI' % n, data, 24)
    frames = []
    for p in ptrs:
        flip, w, h, ox, oy, _unk, _next, length = struct.unpack_from('<8i', data, p)
        raw = data[p + 32:p + 32 + length]
        px = bytearray(w * h)
        x, y = 0, (0 if flip else h - 1)
        i = 0
        while i < len(raw) and 0 <= y < h:
            b = raw[i]
            i += 1
            if b == 0x80:
                x = 0
                y += 1 if flip else -1
            elif b & 0x80:
                x += b & 0x7F
            else:
                for k in range(b):
                    if x < w and i < len(raw):
                        px[y * w + x] = raw[i]
                    x += 1
                    i += 1
        frames.append((w, h, ox, oy, px))
    return frames


def main():
    path, pal_path, cols, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    start = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    frames = read_dc6(open(path, 'rb').read())
    count = int(sys.argv[6]) if len(sys.argv) > 6 else len(frames) - start
    pal = open(pal_path, 'rb').read()
    sel = frames[start:start + count]
    rows = (len(sel) + cols - 1) // cols
    # ancho de cada columna y alto de cada fila: el del cuadro más grande
    colw = [max((sel[r * cols + c][0] for r in range(rows) if r * cols + c < len(sel)), default=0) for c in range(cols)]
    rowh = [max((sel[r * cols + c][1] for c in range(cols) if r * cols + c < len(sel)), default=0) for r in range(rows)]
    W, H = sum(colw), sum(rowh)
    img = bytearray(W * H * 3)
    for k, (w, h, ox, oy, px) in enumerate(sel):
        c, r = k % cols, k // cols
        bx, by = sum(colw[:c]), sum(rowh[:r])
        for y in range(h):
            for x in range(w):
                v = px[y * w + x]
                if not v:
                    continue
                o = ((by + y) * W + bx + x) * 3
                img[o:o + 3] = bytes((pal[v * 3], pal[v * 3 + 1], pal[v * 3 + 2]))  # pal.dat ya es BGR
    print('%d cuadros: %dx%d' % (len(sel), W, H), [(w, h, ox, oy) for (w, h, ox, oy, _) in sel])
    pad = (4 - W * 3 % 4) % 4
    with open(out, 'wb') as f:
        size = 54 + (W * 3 + pad) * H
        f.write(b'BM' + struct.pack('<IHHI', size, 0, 0, 54))
        f.write(struct.pack('<IiiHHIIiiII', 40, W, -H, 1, 24, 0, 0, 2835, 2835, 0, 0))
        for y in range(H):
            f.write(img[y * W * 3:(y + 1) * W * 3] + b'\0' * pad)


if __name__ == '__main__':
    main()
