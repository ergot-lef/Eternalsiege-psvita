#!/usr/bin/env python3
"""Génère les images de la LiveArea (icône, fond, image de démarrage) : silhouette de château au crépuscule."""
import math
import os
import random
import sys

from PIL import Image, ImageDraw


def castle(draw, cx, base, s, color):
    # courtine, tours et donjon en silhouette
    draw.rectangle([cx - 40 * s, base - 22 * s, cx + 40 * s, base], fill=color)
    for tx in (-40, 40):
        draw.rectangle([cx + (tx - 8) * s, base - 34 * s, cx + (tx + 8) * s, base], fill=color)
        for m in range(-8, 8, 4):
            draw.rectangle([cx + (tx + m) * s, base - 38 * s, cx + (tx + m + 2) * s, base - 34 * s], fill=color)
    draw.rectangle([cx - 13 * s, base - 50 * s, cx + 13 * s, base], fill=color)
    for m in range(-13, 13, 5):
        draw.rectangle([cx + m * s, base - 55 * s, cx + (m + 2.5) * s, base - 50 * s], fill=color)
    for m in range(-40, 40, 5):
        draw.rectangle([cx + m * s, base - 25 * s, cx + (m + 2.5) * s, base - 22 * s], fill=color)
    draw.polygon([(cx - 4 * s, base), (cx - 4 * s, base - 10 * s), (cx, base - 14 * s), (cx + 4 * s, base - 10 * s), (cx + 4 * s, base)], fill=(25, 15, 10))


def scene(w, h, title=None):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / h
        r = int(40 + 200 * t ** 1.6)
        g = int(30 + 90 * t ** 1.8)
        b = int(70 - 30 * t)
        for x in range(w):
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    # lueurs d'incendie et fumée
    for _ in range(int(w * h / 5000)):
        x, y = rnd.randrange(w), rnd.randrange(int(h * 0.6), int(h * 0.82))
        rad = rnd.randrange(1, max(2, h // 60))
        c = (rnd.randrange(160, 255), rnd.randrange(60, 120), 20)
        d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=c)
    # collines
    pts = [(0, h)]
    for x in range(0, w + 8, 8):
        pts.append((x, h * 0.78 + math.sin(x * 0.02) * h * 0.04 + math.sin(x * 0.007) * h * 0.05))
    pts.append((w, h))
    d.polygon(pts, fill=(20, 14, 18))
    s = h / 120.0
    castle(d, w * 0.5, h * 0.8, s, (12, 8, 12))
    return img


def save_indexed(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.quantize(colors=256, method=Image.MEDIANCUT).save(path, optimize=True)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "sce_sys"
    save_indexed(scene(128, 128), os.path.join(root, "icon0.png"))
    save_indexed(scene(840, 500), os.path.join(root, "livearea/contents/bg.png"))
    save_indexed(scene(280, 158), os.path.join(root, "livearea/contents/startup.png"))


if __name__ == "__main__":
    main()
