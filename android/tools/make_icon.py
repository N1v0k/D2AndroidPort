"""Draws the launcher icon (an original design: a sword over a dark red glow) at every density."""
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter

def icon(size):
    S = size * 4  # supersample
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(bg)
    r = S * 0.46
    c = S / 2
    for i in range(60, 0, -1):  # radial glow, dark red to black
        t = i / 60.0
        col = (int(20 + 150 * (1 - t) ** 2), int(4 + 18 * (1 - t) ** 3), int(4 + 8 * (1 - t) ** 3), 255)
        rr = r * t
        d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=col)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([c - r, c - r, c + r, c + r], fill=255)
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)
    ring = S * 0.018
    d.ellipse([c - r, c - r, c + r, c + r], outline=(150, 110, 50, 255), width=int(ring))
    gold, steel, dark = (205, 160, 70, 255), (215, 215, 225, 255), (60, 40, 20, 255)
    # blade
    bw, top, guard_y = S * 0.075, S * 0.16, S * 0.64
    d.polygon([(c, top), (c + bw, top + bw * 1.6), (c + bw, guard_y), (c - bw, guard_y), (c - bw, top + bw * 1.6)], fill=steel)
    d.line([(c, top + bw * 1.2), (c, guard_y - S * 0.02)], fill=(150, 150, 165, 255), width=int(S * 0.012))
    # guard, grip, pommel
    gw, gh = S * 0.26, S * 0.05
    d.rounded_rectangle([c - gw, guard_y, c + gw, guard_y + gh], radius=gh / 2, fill=gold)
    d.rectangle([c - S * 0.035, guard_y + gh, c + S * 0.035, S * 0.80], fill=dark)
    pr = S * 0.05
    d.ellipse([c - pr, S * 0.80 - pr * 0.3, c + pr, S * 0.80 + pr * 1.7], fill=gold)
    return img.resize((size, size), Image.LANCZOS)

out = sys.argv[1]
for name, px in [("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)]:
    p = os.path.join(out, "mipmap-" + name)
    os.makedirs(p, exist_ok=True)
    icon(px).save(os.path.join(p, "ic_launcher.png"))
icon(512).save(os.path.join(out, "..", "icon-512.png"))
print("icons in", out)
