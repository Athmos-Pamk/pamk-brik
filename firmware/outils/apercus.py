#!/usr/bin/env python3
"""
Aperçus PNG des écrans du Pamk Brik (540 x 960), sans la carte.

Le script relit les constantes de PamkPhone/config.h et les polices générées
(PamkPhone/src/polices/*.h) : le texte est dessiné glyphe par glyphe exactement comme
dans le firmware (mêmes bitmaps 4 bits, même mélange). Les écrans eux-mêmes sont une
transcription en Python du code des applis (même mise en page, données d'exemple).

Utilisation (depuis firmware/) :
  _ref\\.venv\\Scripts\\python.exe outils\\apercus.py
Résultat : firmware/apercus/*.png (+ planche.png avec tous les écrans).
"""
import math
import os
import re
import sys

from PIL import Image

ICI = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(ICI)
SKETCH = os.path.join(FIRMWARE, "PamkPhone")
SORTIE = os.path.join(FIRMWARE, "apercus")

# ---------------------------------------------------------------------------
#  Constantes de config.h
# ---------------------------------------------------------------------------
C = {}


def lire_config():
    texte = open(os.path.join(SKETCH, "config.h"), encoding="utf-8").read()
    for m in re.finditer(r"constexpr\s+(?:int|uint8_t|uint32_t|bool|float)\s+(\w+)\s*=\s*([^;]+);", texte):
        nom, expr = m.group(1), m.group(2)
        expr = re.sub(r"//.*", "", expr).strip().rstrip("f")
        expr = expr.replace("true", "True").replace("false", "False")
        try:
            C[nom] = eval(expr, {}, dict(C))
        except Exception:
            pass
    for nom in ("LIGNES_PAR_PAGE",):
        if nom in C:
            C[nom] = int(C[nom])


lire_config()
L, H = C["ECRAN_L"], C["ECRAN_H"]
NOIR, GRIS_FONCE, GRIS, GRIS_CLAIR, BLANC = C["NOIR"], C["GRIS_FONCE"], C["GRIS"], C["GRIS_CLAIR"], C["BLANC"]
MARGE = C["MARGE"]
BARRE_ETAT_H, BARRE_TITRE_H, CONTENU_Y = C["BARRE_ETAT_H"], C["BARRE_TITRE_H"], C["CONTENU_Y"]
PIED_Y, LIGNE_H, CHANFREIN, TRAIT = C["PIED_Y"], C["LIGNE_H"], C["CHANFREIN"], C["TRAIT"]
CASE_TAILLE, RETOUR_L = C["CASE_TAILLE"], C["RETOUR_L"]
CLAVIER_Y, CLAVIER_TOUCHE_H, CLAVIER_ECART = C["CLAVIER_Y"], C["CLAVIER_TOUCHE_H"], C["CLAVIER_ECART"]

# ---------------------------------------------------------------------------
#  Polices générées
# ---------------------------------------------------------------------------


class Police:
    def __init__(self, nom):
        t = open(os.path.join(SKETCH, "src", "polices", nom + ".h"), encoding="utf-8").read()
        bm = re.search(r"Bitmaps\[\d+\] = \{(.*?)\};", t, re.S).group(1)
        self.bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", bm))
        gl = re.search(r"Glyphs\[\] = \{(.*?)\n\};", t, re.S).group(1)
        self.glyphes = [tuple(int(v) for v in m.group(1).split(","))
                        for m in re.finditer(r"\{ ([-\d, ]+) \}", gl)]
        iv = re.search(r"Intervals\[\] = \{(.*?)\};", t, re.S).group(1)
        self.intervalles = [tuple(int(v, 16) for v in m.groups())
                            for m in re.finditer(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+) \}", iv)]
        fin = re.search(r"const GFXfont \w+ = \{(.*?)\};", t, re.S).group(1)
        champs = [x.strip() for x in fin.split(",") if x.strip()]
        self.compresse = int(champs[4])
        self.advance_y, self.ascender, self.descender = int(champs[5]), int(champs[6]), int(champs[7])

    def glyphe(self, cp):
        for debut, fin, decalage in self.intervalles:
            if debut <= cp <= fin:
                return self.glyphes[decalage + cp - debut]
            if cp < debut:
                break
        return None

    def remplacant(self, cp):
        g = self.glyphe(cp)
        if g:
            return g
        if cp == 0x2212:
            g = self.glyphe(ord("-"))
        elif cp == 0xA0:
            g = self.glyphe(ord(" "))
        elif 0xC0 <= cp <= 0xFF:
            base = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"
            g = self.glyphe(ord(base[cp - 0xC0]))
        return g or self.glyphe(ord("?"))


F_PETITE = Police("BarlowMoyen22")
F_TEXTE = Police("BarlowTexte28")
F_GRAS = Police("BarlowGras28")
F_PIXEL = Police("Pixel50")
F_TITRE = Police("Pixel75")
F_GRANDE = Police("Pixel125")
F_HORLOGE = Police("Pixel200")

# ---------------------------------------------------------------------------
#  Dessin (transcription de dessin.cpp)
# ---------------------------------------------------------------------------
toile = bytearray([BLANC]) * (L * H)


def effacer(c=BLANC):
    toile[:] = bytearray([c]) * (L * H)


def rect_plein(x, y, l, h, c):
    x0, x1 = max(x, 0), min(x + l, L)
    y0, y1 = max(y, 0), min(y + h, H)
    if x1 <= x0 or y1 <= y0:
        return
    ligne = bytes([c & 15]) * (x1 - x0)
    for yy in range(y0, y1):
        toile[yy * L + x0: yy * L + x1] = ligne


def rect(x, y, l, h, ep, c):
    rect_plein(x, y, l, ep, c)
    rect_plein(x, y + h - ep, l, ep, c)
    rect_plein(x, y + ep, ep, h - 2 * ep, c)
    rect_plein(x + l - ep, y + ep, ep, h - 2 * ep, c)


def pixel(x, y, c):
    if 0 <= x < L and 0 <= y < H:
        toile[y * L + x] = c & 15


def ligne(x0, y0, x1, y1, c, ep=1):
    dx, sx = abs(x1 - x0), (1 if x0 < x1 else -1)
    dy, sy = -abs(y1 - y0), (1 if y0 < y1 else -1)
    err = dx + dy
    d = ep // 2
    while True:
        if ep <= 1:
            pixel(x0, y0, c)
        else:
            rect_plein(x0 - d, y0 - d, ep, ep, c)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def retrait(i, k, h):
    if i < k:
        return k - i
    if i >= h - k:
        return i - (h - k - 1)
    return 0


def chanfrein_plein(x, y, l, h, k, c):
    k = min(k, min(l, h) // 2)
    for i in range(h):
        r = retrait(i, k, h)
        rect_plein(x + r, y + i, l - 2 * r, 1, c)


def chanfrein(x, y, l, h, k, ep, c):
    k = min(k, min(l, h) // 2)
    hi = h - 2 * ep
    ki = min(k, max(0, hi // 2))
    for i in range(h):
        a = retrait(i, k, h)
        if i < ep or i >= h - ep:
            rect_plein(x + a, y + i, l - 2 * a, 1, c)
            continue
        b = ep + retrait(i - ep, ki, hi)
        b = min(b, l // 2)
        rect_plein(x + a, y + i, b - a, 1, c)
        rect_plein(x + l - b, y + i, b - a, 1, c)


def barre(x, y, l, h, pour_mille, fond, plein):
    pour_mille = max(0, min(1000, pour_mille))
    rect_plein(x, y, l, h, fond)
    rect_plein(x, y, l * pour_mille // 1000, h, plein)


def largeur(p, s):
    return sum(p.remplacant(ord(ch))[2] for ch in s)


def haut_capitale(p):
    g = p.glyphe(ord("H")) or p.glyphe(ord("0"))
    return g[4] if g else p.ascender


def base_centree(p, y, h):
    return y + (h + haut_capitale(p)) // 2


def texte(p, x, y, s, c):
    c &= 15
    for ch in s:
        if ch == "\n":
            continue
        w, h, adv, left, top, _, off = p.remplacant(ord(ch))
        ol = (w + 1) // 2
        for j in range(h):
            yy = y - top + j
            if not 0 <= yy < H:
                continue
            for i in range(w):
                xx = x + left + i
                if not 0 <= xx < L:
                    continue
                v = p.bitmap[off + j * ol + i // 2]
                v = (v >> 4) if i & 1 else (v & 15)
                if not v:
                    continue
                idx = yy * L + xx
                if v == 15:
                    toile[idx] = c
                else:
                    o = toile[idx]
                    diff = c - o
                    toile[idx] = o + int((diff * v + (7 if diff >= 0 else -7)) / 15)
        x += adv
    return x


def texte_centre(p, cx, y, s, c):
    texte(p, cx - largeur(p, s) // 2, y, s, c)


def texte_droite(p, xd, y, s, c):
    texte(p, xd - largeur(p, s), y, s, c)


def texte_tronque(p, x, y, lmax, s, c):
    if largeur(p, s) <= lmax:
        texte(p, x, y, s, c)
        return largeur(p, s)
    le = largeur(p, "…")
    lc, n = 0, 0
    for i, ch in enumerate(s):
        a = p.remplacant(ord(ch))[2]
        if lc + a + le > lmax:
            break
        lc += a
        n = i + 1
    xf = texte(p, x, y, s[:n], c)
    xf = texte(p, xf, y, "…", c)
    return xf - x


def couper(p, s, lmax):
    lignes = []
    for para in s.split("\n"):
        mots, cour = para.split(" "), ""
        for m in mots:
            essai = (cour + " " + m) if cour else m
            if largeur(p, essai) <= lmax or not cour:
                cour = essai
            else:
                lignes.append(cour)
                cour = m
        lignes.append(cour)
    return lignes


def motif(lignes_m, x, y, t, c):
    for j, l in enumerate(lignes_m):
        for i, ch in enumerate(l):
            if ch in "#+":
                rect_plein(x + i * t, y + j * t, t, t, c if ch == "#" else GRIS)


ICONES = {
    "taches": ["############", "#..........#", "#.........##", "#........#.#", "#.......##.#", "#.#....##..#",
               "#.##..##...#", "#..####....#", "#...##.....#", "#..........#", "#..........#", "############"],
    "notes": [".##########.", ".#........#.", ".#.######.#.", ".#........#.", ".#.######.#.", ".#........#.",
              ".#.######.#.", ".#........#.", ".#.####...#.", ".#........#.", ".##########.", "............"],
    "pomodoro": ["....####....", "..########..", ".##......##.", ".#...#....#.", "#....#.....#", "#....#.....#",
                 "#....####..#", "#..........#", "#..........#", ".#........#.", ".##......##.", "..########.."],
    "calendrier": ["..#.....#...", "############", "#..........#", "############", "#..........#", "#.##.##.##.#",
                   "#.##.##.##.#", "#..........#", "#.##.##....#", "#.##.##....#", "#..........#", "############"],
    "calculatrice": ["############", "#..........#", "#.########.#", "#.########.#", "#..........#", "#.##.##.##.#",
                     "#..........#", "#.##.##.##.#", "#..........#", "#.##.##.##.#", "#..........#", "############"],
    "reglages": [".....##.....", "..#..##..#..", ".###.##.###.", "..########..", "..##....##..", "####....####",
                 "####....####", "..##....##..", "..########..", ".###.##.###.", "..#..##..#..", ".....##....."],
    "coche": ["......#", ".....##", "#...##.", "##.##..", ".###...", "..#...."],
    "chevron_g": ["...##", "..##.", ".##..", "##...", ".##..", "..##.", "...##"],
    "chevron_d": ["##...", ".##..", "..##.", "...##", "..##.", ".##..", "##..."],
    "maj": ["....#....", "...#.#...", "..#...#..", ".#.....#.", "###...###", "..#...#..", "..#...#..", "..#####.."],
    "effacer": ["..#.......", ".##.......", "##########", ".##.......", "..#......."],
    "entree": [".........#", ".........#", "..#......#", ".##......#", "##########", ".##.......", "..#......."],
}


def visage(cx, y, p, expr="normal", c=NOIR):
    x = cx - (11 * p) // 2
    g = max(1, p // 12)

    def bloc(col, demi):
        rect_plein(x + col * p + g, y + (demi * p) // 2 + g, p - 2 * g, p - 2 * g, c)

    for ox in (0, 7):
        for i in range(4):
            for j in range(4):
                if expr == "normal":
                    on = i in (0, 3) or j in (0, 3)
                elif expr in ("dort", "eteint"):
                    on = j == 3
                elif expr == "content":
                    on = (j == 1 and i in (1, 2)) or (j == 2 and i in (0, 3))
                else:
                    on = j == 3 or (j == 2 and i in (0, 3)) or j == 1
                if on:
                    bloc(ox + i, j * 2)
    for i in range(9):
        if expr in ("normal", "content"):
            demi, on = (13 if i % 2 else 11), True
        elif expr == "dort":
            demi, on = 12, 3 <= i <= 5
        else:
            demi, on = 12, 1 <= i <= 7
        if on:
            bloc(1 + i, demi)


def picto_batterie(x, y, pct, c=NOIR):
    rect(x, y, 34, 20, 3, c)
    rect_plein(x + 34, y + 6, 4, 8, c)
    p = max(0, min(100, pct)) * 26 // 100
    if p:
        rect_plein(x + 4, y + 4, p, 12, c)


def picto_wifi(x, y, barres, c=NOIR):
    for i in range(4):
        h = 7 + i * 5
        rect_plein(x + i * 8, y + 22 - h, 6, h, c if i < barres else GRIS_CLAIR)


def picto_plus(cx, cy, t, ep, c):
    rect_plein(cx - t // 2, cy - ep // 2, t, ep, c)
    rect_plein(cx - ep // 2, cy - t // 2, ep, t, c)


# ---------------------------------------------------------------------------
#  Éléments d'interface (transcription de ui.cpp)
# ---------------------------------------------------------------------------
def separateur(y, x0=MARGE, x1=L - MARGE):
    rect_plein(x0, y, x1 - x0, 2, GRIS_CLAIR)


def barre_etat(heure="14:32", info="", pct=72, wifi=0):
    base = 36
    if heure:
        texte(F_PIXEL, MARGE, base, heure, NOIR)
    if info:
        l = largeur(F_PETITE, info) + 22
        x = L // 2 - l // 2
        rect_plein(x, 16, 12, 12, NOIR)
        texte(F_PETITE, x + 22, 31, info, NOIR)
    x = L - MARGE
    s = f"{pct}%"
    lp = largeur(F_PIXEL, s)
    texte(F_PIXEL, x - lp, base, s, GRIS_FONCE)
    x -= lp + 8 + 38
    picto_batterie(x, 14, pct)
    if wifi:
        x -= 44
        picto_wifi(x, 12, wifi)
    rect_plein(0, BARRE_ETAT_H - 2, L, 2, GRIS_CLAIR)


def bouton(x, y, l, h, lib, style="contour", p=None):
    p = p or F_GRAS
    coul = NOIR
    if style == "plein":
        chanfrein_plein(x, y, l, h, CHANFREIN, NOIR)
        coul = BLANC
    elif style == "discret":
        chanfrein(x, y, l, h, CHANFREIN, 2, GRIS)
    else:
        chanfrein(x, y, l, h, CHANFREIN, TRAIT, NOIR)
    lt = min(largeur(p, lib), l - 16)
    texte_tronque(p, x + (l - lt) // 2, base_centree(p, y, h), l - 16, lib, coul)


def barre_titre(titre, retour=True, action=None, police=None):
    police = police or F_TITRE
    y = BARRE_ETAT_H
    x = MARGE
    if retour:
        motif(ICONES["chevron_g"], MARGE + 2, y + (BARRE_TITRE_H - 35) // 2, 5, NOIR)
        x = 72
    droite = L - MARGE
    if action:
        plus = action == "+"
        l = 68 if plus else max(84, largeur(F_GRAS, action) + 36)
        bx, by = L - MARGE - l, y + 10
        if plus:
            chanfrein(bx, by, l, 64, CHANFREIN, TRAIT, NOIR)
            picto_plus(bx + l // 2, by + 32, 30, 6, NOIR)
        else:
            bouton(bx, by, l, 64, action, "plein")
        droite = bx - 12
    texte_tronque(police, x, base_centree(police, y, BARRE_TITRE_H), droite - x, titre, NOIR)
    rect_plein(0, CONTENU_Y - 2, L, 2, GRIS_CLAIR)


def case(x, y, cochee):
    if cochee:
        chanfrein_plein(x, y, CASE_TAILLE, CASE_TAILLE, 7, NOIR)
        motif(ICONES["coche"], x + (CASE_TAILLE - 28) // 2, y + (CASE_TAILLE - 24) // 2, 4, BLANC)
    else:
        chanfrein(x, y, CASE_TAILLE, CASE_TAILLE, 7, TRAIT, NOIR)


def pagination(page, nb):
    if nb <= 1:
        return
    y, h, l = PIED_Y + 12, 64, 120
    av, ap = page > 0, page + 1 < nb
    chanfrein(MARGE, y, l, h, CHANFREIN, TRAIT if av else 2, NOIR if av else GRIS_CLAIR)
    motif(ICONES["chevron_g"], MARGE + (l - 20) // 2, y + (h - 28) // 2, 4, NOIR if av else GRIS_CLAIR)
    chanfrein(L - MARGE - l, y, l, h, CHANFREIN, TRAIT if ap else 2, NOIR if ap else GRIS_CLAIR)
    motif(ICONES["chevron_d"], L - MARGE - l + (l - 20) // 2, y + (h - 28) // 2, 4, NOIR if ap else GRIS_CLAIR)
    texte_centre(F_PIXEL, L // 2, base_centree(F_PIXEL, y, h), f"{page + 1} / {nb}", GRIS_FONCE)


def interrupteur(xd, yc, actif):
    l, h = 100, 48
    x, y = xd - l, yc - h // 2
    if actif:
        chanfrein_plein(x, y, l, h, 8, NOIR)
        texte_centre(F_GRAS, x + l // 2, base_centree(F_GRAS, y, h), "Oui", BLANC)
    else:
        chanfrein(x, y, l, h, 8, TRAIT, NOIR)
        texte_centre(F_GRAS, x + l // 2, base_centree(F_GRAS, y, h), "Non", NOIR)


def ligne_reglage(y, lib, val):
    h = 84
    base = base_centree(F_TEXTE, y, h)
    xd = L - MARGE - 24
    lv = min(largeur(F_GRAS, val), 230) if val else 0
    texte_tronque(F_TEXTE, MARGE, base, xd - lv - 16 - MARGE, lib, NOIR)
    if lv:
        texte_tronque(F_GRAS, xd - lv, base, 230, val, GRIS_FONCE)
    motif(ICONES["chevron_d"], L - MARGE - 15, y + (h - 21) // 2, 3, GRIS)
    separateur(y + h - 2)


def ligne_interrupteur(y, lib, actif):
    h = 84
    texte_tronque(F_TEXTE, MARGE, base_centree(F_TEXTE, y, h), L - 2 * MARGE - 116, lib, NOIR)
    interrupteur(L - MARGE, y + h // 2, actif)
    separateur(y + h - 2)


def etiquette(x, y, t):
    texte(F_PIXEL, x, y, t, GRIS_FONCE)


# ---------------------------------------------------------------------------
#  Écrans (transcriptions des applis, données d'exemple)
# ---------------------------------------------------------------------------
TACHES = [
    ("Rappeler le garage", False, "aujourd'hui"),
    ("Finir le montage de la vidéo 2", False, "demain"),
    ("Commander les aimants 6 x 2 mm", False, "ven. 30 oct."),
    ("Imprimer la plaque du Pamk Brik", False, ""),
    ("Répondre aux commentaires Reddit", False, ""),
]


def ecran_accueil():
    effacer()
    barre_etat(heure=None, info="18 min")
    yh = 72
    texte_centre(F_HORLOGE, L // 2, yh + 150, "14:32", NOIR)
    texte_centre(F_GRAS, L // 2, yh + 206, "Mardi 27 octobre", NOIR)
    y = yh + 248
    visage(MARGE + 48, y, 8)
    texte_tronque(F_GRAS, MARGE + 116, y + 26, L - 2 * MARGE - 116, "Bon après-midi Thomas", NOIR)
    texte(F_PETITE, MARGE + 116, y + 60, "5 tâches à faire", GRIS_FONCE)
    yt = 432
    separateur(yt - 32)
    etiquette(MARGE, yt + 8, "À FAIRE")
    texte_droite(F_GRAS, L - MARGE, yt + 4, "Tout voir", NOIR)
    yy = yt + 24
    for titre, _, ech in TACHES[:3]:
        case(MARGE, yy + (72 - CASE_TAILLE) // 2, False)
        ld = 0
        if ech:
            ld = largeur(F_PETITE, ech) + 12
            texte_droite(F_PETITE, L - MARGE, yy + 46, ech, GRIS_FONCE)
        texte_tronque(F_TEXTE, MARGE + 60, yy + 46, L - 2 * MARGE - 60 - ld, titre, NOIR)
        yy += 72
    yp = 740
    separateur(yp - 52)
    etiquette(MARGE, yp, "POMODORO")
    texte(F_GRAS, MARGE, yp + 50, "Travail · 18 min", NOIR)
    barre(MARGE, yp + 70, L - 2 * MARGE, 14, 280, GRIS_CLAIR, NOIR)
    bouton(MARGE, 860, L - 2 * MARGE, 76, "Applications", "plein")


def ecran_menu():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Applications")
    tl, th, y0 = (L - 3 * MARGE) // 2, 220, CONTENU_Y + 24
    tuiles = [("Tâches", "taches", "5 à faire"), ("Notes", "notes", "12 notes"),
              ("Pomodoro", "pomodoro", "en cours · 18 min"), ("Calendrier", "calendrier", "mar. 27 oct."),
              ("Calculatrice", "calculatrice", ""), ("Réglages", "reglages", "Wi-Fi, heure, écran")]
    for i, (nom, ic, info) in enumerate(tuiles):
        x = MARGE + (i % 2) * (tl + MARGE)
        y = y0 + (i // 2) * (th + 20)
        chanfrein(x, y, tl, th, 16, TRAIT, NOIR)
        motif(ICONES[ic], x + (tl - 72) // 2, y + 28, 6, NOIR)
        texte_centre(F_GRAS, x + tl // 2, y + 150, nom, NOIR)
        texte_centre(F_PETITE, x + tl // 2, y + 188, info, GRIS_FONCE)


def ecran_taches():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Tâches", True, "+")
    oy, oh = CONTENU_Y + 10, 56
    lo = (L - 2 * MARGE - 12) // 2
    bouton(MARGE, oy, lo, oh, "À faire (5)", "plein")
    bouton(MARGE + lo + 12, oy, lo, oh, "Faites (2)", "discret")
    y0 = oy + oh + 10
    for i, (titre, faite, ech) in enumerate(TACHES):
        y = y0 + i * LIGNE_H
        case(MARGE, y + 18, faite)
        x = MARGE + CASE_TAILLE + 22
        texte_tronque(F_TEXTE, x, y + 44, L - MARGE - x, titre, NOIR)
        if ech:
            texte(F_PETITE, x, y + 76, ech, GRIS_FONCE)
        separateur(y + LIGNE_H - 2, x, L - MARGE)


def ecran_clavier():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Nouvelle note", True, "OK", F_GRAS)
    zx, zy, zl, zh = MARGE, CONTENU_Y + 12, L - 2 * MARGE, CLAVIER_Y - CONTENU_Y - 24
    chanfrein(zx, zy, zl, zh, CHANFREIN, 2, GRIS)
    contenu = "Idées pour la plaque arrière :\nvisage en relief de 0,5 mm, deux couleurs à l'AMS, aimants de 6 mm."
    for i, l in enumerate(couper(F_TEXTE, contenu, zl - 32)[:10]):
        texte(F_TEXTE, zx + 16, zy + 16 + 30 + i * 38, l, NOIR)
    rangees = [
        [("a", 2), ("z", 2), ("e", 2), ("r", 2), ("t", 2), ("y", 2), ("u", 2), ("i", 2), ("o", 2), ("p", 2)],
        [("q", 2), ("s", 2), ("d", 2), ("f", 2), ("g", 2), ("h", 2), ("j", 2), ("k", 2), ("l", 2), ("m", 2)],
        [("MAJ", 3), ("w", 2), ("x", 2), ("c", 2), ("v", 2), ("b", 2), ("n", 2), ("'", 2), ("EFF", 3)],
        [("?123", 3), (",", 2), ("é", 2), ("espace", 6), (".", 2), ("àç", 3), ("ENTREE", 2)],
    ]
    for r, rg in enumerate(rangees):
        y = CLAVIER_Y + r * (CLAVIER_TOUCHE_H + CLAVIER_ECART)
        cumul = 0
        for lib, w in rg:
            x0 = 6 + cumul * (L - 6) // 20
            x1 = 6 + (cumul + w) * (L - 6) // 20 - CLAVIER_ECART
            cumul += w
            bl = x1 - x0
            cx, cy = x0 + bl // 2, y + CLAVIER_TOUCHE_H // 2
            special = lib in ("MAJ", "EFF", "?123", "espace", "àç", "ENTREE")
            if not special:
                chanfrein(x0, y, bl, CLAVIER_TOUCHE_H, 8, 2, GRIS_FONCE)
                texte_centre(F_GRAS, cx, base_centree(F_GRAS, y, CLAVIER_TOUCHE_H), lib, NOIR)
                continue
            chanfrein_plein(x0, y, bl, CLAVIER_TOUCHE_H, 8, GRIS_CLAIR)
            if lib == "MAJ":
                motif(ICONES["maj"], cx - 18, cy - 16, 4, NOIR)
            elif lib == "EFF":
                motif(ICONES["effacer"], cx - 20, cy - 10, 4, NOIR)
            elif lib == "ENTREE":
                motif(ICONES["entree"], cx - 20, cy - 14, 4, NOIR)
            elif lib == "espace":
                texte_centre(F_PETITE, cx, base_centree(F_PETITE, y, CLAVIER_TOUCHE_H), lib, GRIS_FONCE)
            else:
                texte_centre(F_GRAS, cx, base_centree(F_GRAS, y, CLAVIER_TOUCHE_H), lib, NOIR)


def grandes_minutes(secondes, yb):
    num = "<1" if 0 < secondes < 60 else str((secondes + 59) // 60)
    l1, l2 = largeur(F_GRANDE, num), largeur(F_TITRE, " min")
    x = (L - l1 - l2) // 2
    texte(F_GRANDE, x, yb, num, NOIR)
    texte(F_TITRE, x + l1, yb, " min", GRIS_FONCE)


def ecran_pomodoro():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Pomodoro")
    oy, oh = CONTENU_Y + 10, 56
    lo = (L - 2 * MARGE - 12) // 2
    bouton(MARGE, oy, lo, oh, "Pomodoro", "plein")
    bouton(MARGE + lo + 12, oy, lo, oh, "Minuteur", "discret")
    texte_centre(F_PIXEL, L // 2, 270, "TRAVAIL", GRIS_FONCE)
    grandes_minutes(18 * 60 - 20, 400)
    barre(MARGE, 430, L - 2 * MARGE, 16, 290, GRIS_CLAIR, NOIR)
    c, e, n = 28, 14, 4
    x0 = (L - (n * c + (n - 1) * e)) // 2
    for i in range(n):
        if i < 1:
            rect_plein(x0 + i * (c + e), 476, c, c, NOIR)
        else:
            rect(x0 + i * (c + e), 476, c, c, 3, NOIR)
    texte_centre(F_PETITE, L // 2, 540, "1 pomodoro terminé", GRIS_FONCE)
    texte_centre(F_TEXTE, L // 2, 592, "Fin à 14:50", NOIR)
    lb = (L - 2 * MARGE - 16) // 2
    bouton(MARGE, 630, lb, 84, "Pause", "plein")
    bouton(MARGE + lb + 16, 630, lb, 84, "Passer")
    bouton(MARGE, 730, L - 2 * MARGE, 76, "Arrêter la série", "discret")
    separateur(842)
    texte_tronque(F_PETITE, MARGE, 890, L - 2 * MARGE - 30, "25 min · pause 5 · grande pause 15 · 4 cycles", GRIS_FONCE)
    motif(ICONES["chevron_d"], L - MARGE - 15, 868, 3, GRIS)


def ecran_calendrier():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Calendrier", True, "Auj.")
    ey, eh = CONTENU_Y + 8, 72
    for x, ic in ((MARGE, "chevron_g"), (L - MARGE - 80, "chevron_d")):
        chanfrein(x, ey, 80, eh - 8, CHANFREIN, TRAIT, NOIR)
        motif(ICONES[ic], x + 30, ey + 18, 4, NOIR)
    texte_centre(F_GRAS, L // 2, base_centree(F_GRAS, ey, eh - 8), "Octobre 2026", NOIR)
    jy = ey + eh + 36
    gy, cl, ch = jy + 16, 72, 96
    gx = (L - 7 * cl) // 2
    for i, ini in enumerate("LMMJVSD"):
        texte_centre(F_PIXEL, gx + i * cl + cl // 2, jy, ini, GRIS if i >= 5 else GRIS_FONCE)
    decalage = 3   # 1er octobre 2026 : jeudi
    marques = {27: 1, 28: 1, 30: 2, 14: 0, 9: 3}
    for j in range(1, 32):
        k = decalage + j - 1
        x, y = gx + (k % 7) * cl, gy + (k // 7) * ch
        auj = j == 27
        if auj:
            chanfrein_plein(x + 4, y + 4, cl - 8, ch - 8, 8, NOIR)
        texte_centre(F_GRAS, x + cl // 2, y + 46, str(j), BLANC if auj else (GRIS_FONCE if k % 7 >= 5 else NOIR))
        if j in marques:
            n = marques[j]
            if n == 0:
                rect_plein(x + cl // 2 - 5, y + 62, 10, 10, GRIS)
            else:
                lm = n * 10 + (n - 1) * 6
                for i in range(n):
                    rect_plein(x + (cl - lm) // 2 + i * 16, y + 62, 10, 10, BLANC if auj else NOIR)
    texte_centre(F_PETITE, L // 2, PIED_Y + 50, "4 jours avec des tâches à faire", GRIS_FONCE)


def ecran_calculatrice():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Calculatrice")
    ay = CONTENU_Y
    texte_droite(F_PIXEL, L - MARGE, ay + 64, "= 137,5", GRIS_FONCE)
    texte_droite(F_GRANDE, L - MARGE, ay + 210, "12,5 × 11", NOIR)
    y0, th, te = 396, 92, 12
    separateur(y0 - 22)
    libs = ["C", "", "%", "÷", "7", "8", "9", "×", "4", "5", "6", "−", "1", "2", "3", "+", "±", "0", ",", "="]
    lt = (L - 2 * MARGE - 3 * te) // 4
    for i, lib in enumerate(libs):
        x, y = MARGE + (i % 4) * (lt + te), y0 + (i // 4) * (th + te)
        if i == 19:
            chanfrein_plein(x, y, lt, th, CHANFREIN, NOIR)
        elif i % 4 == 3 or i < 4:
            chanfrein_plein(x, y, lt, th, CHANFREIN, GRIS_CLAIR)
        else:
            chanfrein(x, y, lt, th, CHANFREIN, TRAIT, NOIR)
        if i == 1:
            motif(ICONES["effacer"], x + lt // 2 - 25, y + th // 2 - 12, 5, NOIR)
        else:
            texte_centre(F_TITRE, x + lt // 2, base_centree(F_TITRE, y, th), lib, BLANC if i == 19 else NOIR)


def ecran_reglages():
    effacer()
    barre_etat(info="18 min")
    barre_titre("Réglages")
    lignes = [("Wi-Fi", "auto"), ("Heure et date", "14:32"), ("Écran", "net"), ("Veille et batterie", "2 min"),
              ("Pomodoro", "25 / 5 / 15"), ("Boutons", "normaux"), ("Prénom", "Thomas"), ("À propos", "V0 0.1.0"),
              ("Données", "7 tâches · 12 notes")]
    for i, (lib, val) in enumerate(lignes):
        ligne_reglage(CONTENU_Y + i * 84, lib, val)


def ecran_veille():
    effacer()
    texte_centre(F_PIXEL, L // 2, 72, "EN VEILLE", GRIS_FONCE)
    visage(L // 2, 112, 20, "dort")
    texte(F_PIXEL, L // 2 + 124, 150, "z", GRIS)
    texte(F_TITRE, L // 2 + 148, 128, "z", GRIS_FONCE)
    y = 292
    texte_centre(F_GRANDE, L // 2, y + 112, "14:45", NOIR)
    texte_centre(F_GRAS, L // 2, y + 160, "Mardi 27 octobre", NOIR)
    texte_centre(F_PETITE, L // 2, y + 200, "Batterie 72 %", GRIS_FONCE)
    yy = 552
    rect_plein(MARGE, yy - 22, 16, 16, NOIR)
    texte(F_GRAS, MARGE + 30, yy, "Travail jusqu'à 14:50", NOIR)
    yy += 64
    texte(F_PIXEL, MARGE, yy, "À FAIRE", GRIS_FONCE)
    yy += 46
    for titre, _, _ in TACHES[:4]:
        chanfrein(MARGE, yy - 22, 24, 24, 4, 2, NOIR)
        texte_tronque(F_TEXTE, MARGE + 40, yy, L - 2 * MARGE - 40, titre, NOIR)
        yy += 46
    texte_centre(F_PETITE, L // 2, 932, "Appuie sur le bouton du dessus pour réveiller", GRIS_FONCE)


def ecran_alerte():
    effacer(NOIR)
    visage(L // 2, 150, 22, "content", BLANC)
    texte_centre(F_TITRE, L // 2, 470, "Pause !", BLANC)
    for i, l in enumerate(couper(F_TEXTE, "Pomodoro 2 terminé. Pause jusqu'à 14:55.", L - 2 * MARGE - 40)):
        texte_centre(F_TEXTE, L // 2, 540 + i * 40, l, BLANC)
    chanfrein_plein(MARGE, 700, L - 2 * MARGE, 84, CHANFREIN, BLANC)
    texte_centre(F_GRAS, L // 2, base_centree(F_GRAS, 700, 84), "OK", NOIR)
    chanfrein(MARGE, 808, L - 2 * MARGE, 84, CHANFREIN, TRAIT, BLANC)
    texte_centre(F_GRAS, L // 2, base_centree(F_GRAS, 808, 84), "Voir le Pomodoro", BLANC)


def ecran_demarrage():
    effacer()
    visage(L // 2, 280, 24)
    texte_centre(F_TITRE, L // 2, 560, "Pamk Brik", NOIR)
    texte_centre(F_PIXEL, L // 2, 612, "V0 - 0.1.0", GRIS_FONCE)


ECRANS = [
    ("01_demarrage", ecran_demarrage), ("02_accueil", ecran_accueil), ("03_applications", ecran_menu),
    ("04_taches", ecran_taches), ("05_clavier", ecran_clavier), ("06_pomodoro", ecran_pomodoro),
    ("07_calendrier", ecran_calendrier), ("08_calculatrice", ecran_calculatrice), ("09_reglages", ecran_reglages),
    ("10_veille", ecran_veille), ("11_alerte", ecran_alerte),
]


def image():
    # 16 niveaux -> 8 bits, teinte légèrement chaude comme le papier électronique
    lut = [round(v * 255 / 15) for v in range(16)]
    im = Image.frombytes("L", (L, H), bytes(lut[v] for v in toile))
    return im


def main():
    os.makedirs(SORTIE, exist_ok=True)
    vignettes = []
    for nom, f in ECRANS:
        f()
        im = image()
        im.save(os.path.join(SORTIE, nom + ".png"))
        vignettes.append(im)
        print("écrit", nom + ".png")
    # Planche : 6 colonnes, demi-taille, cadre gris
    col, ech = 6, 0.5
    w, h = int(L * ech), int(H * ech)
    lignes = math.ceil(len(vignettes) / col)
    planche = Image.new("L", (col * (w + 24) + 24, lignes * (h + 24) + 24), 200)
    for i, im in enumerate(vignettes):
        x = 24 + (i % col) * (w + 24)
        y = 24 + (i // col) * (h + 24)
        planche.paste(im.resize((w, h), Image.LANCZOS), (x, y))
    planche.save(os.path.join(SORTIE, "planche.png"))
    print("écrit planche.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
