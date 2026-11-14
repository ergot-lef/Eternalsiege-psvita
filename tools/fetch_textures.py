#!/usr/bin/env python3
"""Télécharge des textures photo CC0 depuis Poly Haven et les convertit au format .tex du jeu.

Format .tex : b"ETX1" + largeur (u16) + hauteur (u16) + pixels RGB.
Chaque texture manquante est simplement ignorée : le jeu génère alors sa propre texture.
"""
import io
import json
import os
import struct
import sys
import urllib.request

from PIL import Image, ImageOps, ImageStat

API = "https://api.polyhaven.com"
UA = {"User-Agent": "EternalSiegeVita-CI/1.0 (github.com/ergot-lef/eternalsiege-psvita)"}

# nom dans le jeu -> identifiants préférés, mots-clés de secours, taille, désaturation
WANTED = {
    "stone":   (["castle_brick_01", "castle_wall_slates", "medieval_blocks_02", "medieval_blocks_03", "stone_brick_wall_001"], [["castle"], ["medieval", "block"], ["stone", "wall"], ["brick", "stone"]], 512, False),
    "grass":   (["aerial_grass_rock", "leafy_grass", "forrest_ground_01", "grass_path_2"], [["grass"], ["forest", "ground"]], 512, False),
    "dirt":    (["brown_mud_leaves_01", "brown_mud", "aerial_mud_1", "dirt_floor"], [["mud"], ["dirt"], ["soil"]], 256, False),
    "rock":    (["aerial_rocks_02", "rock_face", "rocky_trail"], [["rock"], ["cliff"]], 256, False),
    "bark":    (["bark_brown_02", "bark_brown_01", "bark_willow", "pine_bark"], [["bark"]], 256, False),
    "wood":    (["wood_table_001", "fine_grained_wood", "rough_wood"], [["wood"]], 256, False),
    "planks":  (["wood_planks", "weathered_planks", "old_planks_02", "wooden_planks"], [["plank"], ["wood", "floor"]], 256, False),
    "metal":   (["rusty_metal_02", "metal_plate", "rusty_metal"], [["metal"], ["rust"]], 256, True),
    "cloth":   (["fabric_pattern_07", "denim_fabric", "hessian_230"], [["fabric"], ["cloth"], ["hessian"]], 128, True),
    "leather": (["leather_red_03", "brown_leather"], [["leather"]], 128, False),
    "straw":   (["thatch_roof_angled", "thatch_roof"], [["thatch"], ["straw"], ["hay"]], 128, False),
}


def get_json(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def get_bytes(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def tokens(asset_id, info):
    t = set(asset_id.lower().split("_"))
    for k in ("tags", "categories"):
        for v in info.get(k, []) or []:
            t.update(str(v).lower().replace("-", " ").split())
    return t


def pick(assets, prefer, keywords):
    for p in prefer:
        if p in assets:
            return p
    for kws in keywords:
        cands = [a for a, info in assets.items() if all(any(k in tok for tok in tokens(a, info)) for k in kws)]
        if cands:
            cands.sort(key=lambda a: -int(assets[a].get("download_count", 0) or 0))
            return cands[0]
    return None


def diffuse_url(files):
    keys = [k for k in files if k.lower() in ("diffuse", "diff", "color", "col", "albedo", "basecolor")]
    for key in keys:
        d = files[key]
        for res in ("1k", "2k", "4k"):
            if res in d:
                for fmt in ("jpg", "png"):
                    if fmt in d[res]:
                        return d[res][fmt]["url"]
    return None


def convert(data, size, desat):
    img = Image.open(io.BytesIO(data)).convert("RGB")
    img = img.resize((size, size), Image.LANCZOS)
    if desat:
        g = ImageOps.grayscale(img)
        img = Image.merge("RGB", (g, g, g))
    # normalise la luminosité moyenne pour un rendu homogène avec les teintes du jeu
    mean = sum(ImageStat.Stat(img).mean) / 3.0
    if mean > 1:
        k = 140.0 / mean
        k = max(0.7, min(1.8, k))
        img = img.point(lambda v: max(0, min(255, int(v * k))))
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build_tex"
    os.makedirs(out, exist_ok=True)
    credits = ["Textures photo : Poly Haven (polyhaven.com), licence CC0.", ""]
    try:
        assets = get_json(API + "/assets?t=textures")
    except Exception as e:  # pas de réseau : le jeu utilisera ses textures générées
        print("Poly Haven inaccessible :", e)
        return 0
    for name, (prefer, kws, size, desat) in WANTED.items():
        try:
            aid = pick(assets, prefer, kws)
            if not aid:
                print(f"{name}: aucune texture trouvée")
                continue
            files = get_json(f"{API}/files/{aid}")
            url = diffuse_url(files)
            if not url:
                print(f"{name}: pas de diffuse pour {aid}")
                continue
            img = convert(get_bytes(url), size, desat)
            with open(os.path.join(out, name + ".tex"), "wb") as f:
                f.write(b"ETX1")
                f.write(struct.pack("<HH", size, size))
                f.write(img.tobytes())
            credits.append(f"{name}: {assets[aid].get('name', aid)} ({aid})")
            print(f"{name}: {aid} -> {size}px")
        except Exception as e:
            print(f"{name}: échec ({e})")
    with open(os.path.join(out, "CREDITS.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(credits) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
