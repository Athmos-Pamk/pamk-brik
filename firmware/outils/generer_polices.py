#!/usr/bin/env python3
"""
Génère les en-têtes de polices (.h) du Pamk Brik pour la bibliothèque LilyGo-EPD47.

Adapté de scripts/fontconvert.py de LilyGo-EPD47 (lui-même repris d'epdiy, licence GPL-3.0) :
même rendu FreeType, même codage des glyphes (4 bits par pixel, 2 pixels par octet,
pixel pair dans le quartet bas) et mêmes structures GFXfont / GFXglyph / UnicodeInterval
que epd_driver.h. Ajouts :
  - taille donnée directement en pixels (--px) au lieu de points à 150 ppp ;
  - plages Unicode choisies en ligne de commande (Latin-1 + œ Œ Ÿ € et la ponctuation
    française par défaut) ;
  - glyphes NON compressés par défaut (le firmware les lit directement, sans zlib).

Utilisation (depuis firmware/, avec le venv de _ref) :
  _ref\\.venv\\Scripts\\python.exe outils\\generer_polices.py            -> toutes les polices
  _ref\\.venv\\Scripts\\python.exe outils\\generer_polices.py BarlowTexte36 BarlowGras36
                                                                  -> seulement celles-là
  _ref\\.venv\\Scripts\\python.exe outils\\generer_polices.py --verifier  -> compare avec fontconvert.py

Les polices TTF (licence SIL OFL 1.1) sont attendues dans _ref/fonts/ (voir README).
"""
import argparse
import math
import os
import subprocess
import sys
import zlib
from collections import namedtuple

import freetype

ICI = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(ICI)
DOSSIER_TTF = os.path.join(FIRMWARE, "_ref", "fonts")
SORTIE = os.path.join(FIRMWARE, "PamkPhone", "src", "polices")

# Plages Unicode inclusives, croissantes et sans chevauchement.
PLAGES_TEXTE = [
    (0x20, 0x7E),      # ASCII
    (0xA0, 0xFF),      # Latin-1 : é è ê ë à â ä ç ù û ü î ï ô ö æ « » ° € ...
    (0x152, 0x153),    # Œ œ
    (0x178, 0x178),    # Ÿ
    (0x2013, 0x2014),  # – —
    (0x2018, 0x2019),  # ‘ ’
    (0x201C, 0x201D),  # “ ”
    (0x2022, 0x2022),  # •
    (0x2026, 0x2026),  # …
    (0x20AC, 0x20AC),  # €
    (0x2212, 0x2212),  # − (signe moins)
]
# Grands chiffres (Pomodoro, calculatrice, veille) : ASCII + × ÷ − (pas d'accents).
PLAGES_CHIFFRES = [
    (0x20, 0x7E),
    (0xD7, 0xD7),      # ×
    (0xF7, 0xF7),      # ÷
    (0x2212, 0x2212),  # −
]
# Horloge géante : chiffres, deux-points, espace et tiret seulement (gain de place).
PLAGES_HORLOGE = [
    (0x20, 0x20),
    (0x2D, 0x2D),
    (0x30, 0x3A),
]

# nom C, fichier TTF, taille en pixels (em), plages
# VT323 : un « pixel » du dessin vaut 40 unités sur 1000 -> tailles multiples de 25 px = net.
POLICES = [
    ("BarlowMoyen22",  "barlow/Barlow-Medium.ttf",   22, PLAGES_TEXTE),
    ("BarlowTexte28",  "barlow/Barlow-Regular.ttf",  28, PLAGES_TEXTE),
    ("BarlowGras28",   "barlow/Barlow-SemiBold.ttf", 28, PLAGES_TEXTE),
    # Taille du texte « grande » (Param.tailleTexte, 28/10/2026) : listes et notes.
    ("BarlowTexte36",  "barlow/Barlow-Regular.ttf",  36, PLAGES_TEXTE),
    ("BarlowGras36",   "barlow/Barlow-SemiBold.ttf", 36, PLAGES_TEXTE),
    ("Pixel50",        "vt323/VT323-Regular.ttf",    50, PLAGES_TEXTE),
    ("Pixel75",        "vt323/VT323-Regular.ttf",    75, PLAGES_TEXTE),
    ("Pixel125",       "vt323/VT323-Regular.ttf",   125, PLAGES_CHIFFRES),
    ("Pixel200",       "vt323/VT323-Regular.ttf",   200, PLAGES_HORLOGE),
]

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top",
                                       "compressed_size", "data_offset", "code_point"])


def norm_floor(val):
    return int(math.floor(val / (1 << 6)))


def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))


def convertir(nom, chemins, plages, px=None, pt=None, compresser=False):
    """Retourne le texte de l'en-tête C (même format que fontconvert.py)."""
    pile = [freetype.Face(c) for c in chemins]
    for face in pile:
        if pt is not None:
            face.set_char_size(pt << 6, pt << 6, 150, 150)   # comme fontconvert.py
        else:
            face.set_pixel_sizes(px, px)

    def charger(cp):
        for face in pile:
            idx = face.get_char_index(cp)
            if idx > 0:
                face.load_glyph(idx, freetype.FT_LOAD_RENDER)
                return face
        raise ValueError(f"caractère U+{cp:04X} absent de {chemins}")

    total = 0
    glyphes = []
    for debut, fin in plages:
        for cp in range(debut, fin + 1):
            face = charger(cp)
            bm = face.glyph.bitmap
            pixels = []
            px_courant = 0
            for i, v in enumerate(bm.buffer):
                x = i % bm.width
                if x % 2 == 0:
                    px_courant = (v >> 4)
                else:
                    px_courant = px_courant | (v & 0xF0)
                    pixels.append(px_courant)
                    px_courant = 0
                if x == bm.width - 1 and bm.width % 2 > 0:
                    pixels.append(px_courant)
                    px_courant = 0
            donnees = bytes(pixels)
            if compresser:
                donnees = zlib.compress(donnees)
            g = GlyphProps(width=bm.width, height=bm.rows,
                           advance_x=norm_floor(face.glyph.advance.x),
                           left=face.glyph.bitmap_left, top=face.glyph.bitmap_top,
                           compressed_size=len(donnees), data_offset=total, code_point=cp)
            if g.width > 255 or g.height > 255 or g.advance_x > 255:
                raise ValueError(f"glyphe U+{cp:04X} trop grand pour GFXglyph (uint8_t)")
            total += len(donnees)
            glyphes.append((g, donnees))

    face = charger(ord('|'))   # même heuristique que fontconvert.py pour les métriques
    octets = []
    for _, d in glyphes:
        octets.extend(d)

    lignes = []
    lignes.append("#pragma once")
    lignes.append("#include \"epd_driver.h\"")
    lignes.append(f"const uint8_t {nom}Bitmaps[{len(octets)}] = {{")
    for i in range(0, len(octets), 16):
        lignes.append("    " + " ".join(f"0x{b:02X}," for b in octets[i:i + 16]))
    lignes.append("};")
    lignes.append(f"const GFXglyph {nom}Glyphs[] = {{")
    for g, _ in glyphes:
        if g.code_point == 92:
            nom_car = "<backslash>"
        elif g.code_point < 0x80:
            nom_car = chr(g.code_point)
        else:
            nom_car = f"U+{g.code_point:04X}"
        lignes.append("    { " + ", ".join(str(a) for a in list(g[:-1])) + " }, " + f"// {nom_car}")
    lignes.append("};")
    lignes.append(f"const UnicodeInterval {nom}Intervals[] = {{")
    decalage = 0
    for debut, fin in plages:
        lignes.append(f"    {{ 0x{debut:X}, 0x{fin:X}, 0x{decalage:X} }},")
        decalage += fin - debut + 1
    lignes.append("};")
    lignes.append(f"const GFXfont {nom} = {{")
    lignes.append(f"    (uint8_t*){nom}Bitmaps,")
    lignes.append(f"    (GFXglyph*){nom}Glyphs,")
    lignes.append(f"    (UnicodeInterval*){nom}Intervals,")
    lignes.append(f"    {len(plages)},")
    lignes.append(f"    {1 if compresser else 0},")
    lignes.append(f"    {norm_ceil(face.size.height)},")
    lignes.append(f"    {norm_ceil(face.size.ascender)},")
    lignes.append(f"    {norm_floor(face.size.descender)},")
    lignes.append("};")
    return "\n".join(lignes) + "\n", len(octets)


def verifier():
    """Compare la sortie de ce script avec scripts/fontconvert.py de la bibliothèque (ASCII)."""
    fc = os.path.join(FIRMWARE, "_ref", "LilyGo-EPD47", "scripts", "fontconvert.py")
    ttf = os.path.join(DOSSIER_TTF, "barlow", "Barlow-Regular.ttf")
    for compresser in (False, True):
        args = [sys.executable, "-I", fc, "Essai", "14", ttf]
        if compresser:
            args.insert(3, "--compress")
        ref = subprocess.run(args, capture_output=True, text=True, check=True).stdout
        ref = ref.replace("\r\n", "\n")
        notre, _ = convertir("Essai", [ttf], [(32, 126)], pt=14, compresser=compresser)
        # fontconvert.py écrit les commentaires différemment : on compare les données seulement.
        def donnees(t):
            return [l.split("//")[0].rstrip() for l in t.splitlines()]
        ok = donnees(ref) == donnees(notre)
        print(f"compression={compresser} : {'IDENTIQUE' if ok else 'DIFFÉRENT'}")
        if not ok:
            return 1
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--verifier", action="store_true", help="compare avec fontconvert.py et s'arrête")
    p.add_argument("--compresser", action="store_true", help="glyphes compressés zlib (non géré par le firmware)")
    p.add_argument("noms", nargs="*", help="polices à générer (toutes si rien n'est donné)")
    a = p.parse_args()
    if a.verifier:
        return verifier()
    inconnus = [n for n in a.noms if n not in [q[0] for q in POLICES]]
    if inconnus:
        print(f"Police(s) inconnue(s) : {', '.join(inconnus)}")
        return 1
    os.makedirs(SORTIE, exist_ok=True)
    total = 0
    for nom, ttf, px, plages in POLICES:
        if a.noms and nom not in a.noms:
            continue
        texte, taille = convertir(nom, [os.path.join(DOSSIER_TTF, ttf)], plages, px=px, compresser=a.compresser)
        fichier = os.path.join(SORTIE, f"{nom}.h")
        with open(fichier, "w", encoding="utf-8", newline="\n") as f:
            f.write(f"// Généré par firmware/outils/generer_polices.py — ne pas modifier à la main.\n")
            f.write(f"// Police : {os.path.basename(ttf)} ({px} px), licence SIL OFL 1.1 (voir OFL-*.txt).\n")
            f.write(texte)
        total += taille
        print(f"{nom:15s} {px:4d} px  {taille:8d} octets de glyphes  -> {os.path.relpath(fichier, FIRMWARE)}")
    print(f"Total glyphes : {total} octets ({total / 1024:.0f} Kio)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
