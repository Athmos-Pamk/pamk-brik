# -*- coding: utf-8 -*-
"""
Pamk Phone V0 - boîtier imprimable, entièrement paramétrique (CadQuery).

Régénérer (PowerShell) :
    cd A:\\bernon\\Robotique\\Pamk_Phone\\cad ; .\\.venv\\Scripts\\python.exe telephone_v0.py

Ça construit toutes les pièces, exporte les STEP (Inventor) et STL (Bambu Studio) dans
export/, lance les vérifications (verification_v0.py) et les aperçus (apercus_v0.py).
Options : --sans-verif, --sans-apercus.

Repère (le même que le visuel de concept) :
    X = largeur (vers la droite quand on regarde l'écran)
    Y = longueur (haut du téléphone = +Y, le bas porte l'USB-C)
    Z = épaisseur (écran vers +Z). Z = 0 : face arrière de la coque, sans la plaque.

D'où viennent les cotes : voir docs/MECANIQUE.md. Tout ce qui est marqué
« À MESURER » est une estimation à contrôler au pied à coulisse à la réception.
"""
import math
import os
import sys

import cadquery as cq

ICI = os.path.dirname(os.path.abspath(__file__))
DOSSIER_EXPORT = os.path.join(ICI, "export")

# =============================================================================
# PARAMÈTRES (mm)
# =============================================================================

# --- Carte LilyGO T5 4.7" V2.3/V2.4 ESP32-S3 (H716, tactile) -----------------
PCB_L = 118.07            # vérifié : contour du DXF LilyGO (ref/lilygo_github/shell__EPD47-S3.dxf)
PCB_W = 63.07             # vérifié : idem
PCB_EP = 1.2              # À MESURER (1,20 mm sur la carte du STEP LilyGO « style2 »)
# Encoche du PCB sous l'antenne de l'ESP32 (vérifié DXF) : x0, x1, y0, y1
ENCOCHE_ANTENNE = (-31.535, -24.29, -50.73, -30.69)

# --- Écran e-paper ED047TC1 (datasheet E Ink, ref/lilygo_github/datasheet__ED047TC1.pdf)
DALLE_L, DALLE_W = 113.9, 62.5     # vérifié datasheet (contour)
AA_L, AA_W = 103.68, 58.32         # vérifié datasheet (zone active 960 x 540 px)
DALLE_AA_BORD_NAPPE = 8.12         # vérifié datasheet : bord côté nappe -> zone active
EP_DALLE = 1.98                    # dalle 0,872 + adhésifs (STEP LilyGO) - À MESURER

# --- Verre tactile (option « With Touch Glass ») -----------------------------
VERRE_L, VERRE_W = 121.0, 67.0     # wiki LilyGO « 121x67 » ; boîtier LilyGO : 120,2 x 66,5 - À MESURER
VERRE_R = 3.0                      # rayon des coins (boîtier LilyGO style3 : 3,0) - À MESURER
VERRE_EP = 1.1                     # À MESURER (verre 2.5D, valeur courante)
AA_BORD_HAUT = 12.8                # bord haut du verre -> zone active (photo LilyGO) - À MESURER
VERRE_DECALAGE_Y = 0.0             # centre du verre - centre du PCB (boîtier LilyGO : centré) - À MESURER
EP_ADHESIF_VERRE = 0.15            # adhésif double face fin entre verre et lèvre du cadre
JEU_VERRE = 0.2                    # jeu par côté autour du verre dans le cadre

# --- Composants au dos de la carte ------------------------------------------
# (x0, x1, y0, y1, hauteur sous le PCB). x/y : empreintes du DXF LilyGO ramenées au
# repère téléphone (X = 31,535 - x_dxf ; Y = -y_dxf - 59,035). Hauteurs : valeurs
# usuelles des boîtiers, À MESURER.
COMPOSANTS = {
    "module_esp32s3": (-29.87, -4.05, -50.69, -31.01, 3.3),
    "usb_c": (2.98, 12.62, -60.09, -52.64, 3.3),
    "prise_batterie": (-0.44, 8.81, -28.53, -20.28, 4.9),
    "fiche_batterie": (8.81, 15.5, -27.6, -21.2, 4.9),     # fiche JST branchée (enveloppe)
    "lecteur_tf": (15.78, 30.96, -5.50, 9.36, 1.85),
    "bouton_rst": (28.17, 32.0, -30.58, -25.83, 3.0),
    "bouton_boot": (27.96, 32.0, -21.27, -16.51, 3.0),
    "bouton_io21": (27.97, 32.0, -12.91, -8.15, 3.0),
    "connecteur_hy20": (25.32, 30.57, 22.48, 31.46, 5.0),
    "pastilles_p4": (25.34, 30.59, 11.72, 20.70, 0.5),      # V2.4 : non peuplé
    "fpc_tactile": (21.02, 28.66, -42.75, -36.58, 2.0),
    "fpc_dalle_p1": (12.79, 23.64, 39.36, 51.88, 2.0),
    "soic16": (-18.49, -8.28, 37.47, 41.40, 1.75),
    "pile_rtc": (11.09, 17.06, -51.28, -46.03, 2.0),
}
H_COMPOSANTS_GENERIQUE = 2.0       # « nappe » des petits composants sur tout le dos de carte
MARGE_BORD_PCB = 0.5               # pas de petit composant à moins de 0,5 mm du bord (règle de routage usuelle)
USB_X = 7.80                       # vérifié DXF (centre de la prise USB-C)
USB_Z_SOUS_PCB = 1.6               # axe de la prise sous le dos du PCB - À MESURER
# Ouverture USB-C : assez grande pour l'embout surmoulé du câble (12,35 x 6,5 mm au plus,
# recommandation de la norme USB-C), sinon l'embout bute sur la paroi et la fiche n'entre
# que de 4,8 mm sur 6,65 (pas de verrouillage). Encoche ouverte vers le plan de joint dans
# la coque (pas de pont) + entaille dans le bas du cadre.
USB_ENCOCHE_L = 12.6               # largeur de l'ouverture
USB_ENCOCHE_SOUS_AXE = 3.4         # l'ouverture descend de 3,4 sous l'axe de la prise
USB_ENCOCHE_SUR_AXE = 3.4          # et monte de 3,4 au-dessus (entaille dans le cadre)
USB_ENCOCHE_R = 1.0                # rayon des coins de l'ouverture
TF_Y = 1.93                        # vérifié DXF (centre du lecteur TF)
TF_Z_SOUS_PCB = 0.95               # plan de la carte TF sous le dos du PCB - À MESURER
TF_FENTE = (11.6, 2.0)             # fente d'accès TF (longueur, hauteur)
BOUTONS_CARTE = {"RST": -28.21, "BOOT": -18.89, "IO21": -10.53}   # Y, vérifié DXF
TROUS_EPINGLE = ("RST", "BOOT")    # trous d'épingle sur le flanc droit (pour le flash)
TROU_EPINGLE_D = 1.8
BOUTONS_CARTE_Z_SOUS_PCB = 0.95    # axe des poussoirs latéraux - À MESURER
# Trous de fixation de la carte : les 7 trous du boîtier LilyGO « style3 » (a.stl, plaque
# à y = -2,5 ; repère téléphone X = z_stl, Y = -x_stl), recoupés à ±0,4 sur la photo du dos
# V2.4. On n'y visse rien : des plots viennent appuyer autour.
TROUS_PCB = [(27.05, -54.06), (19.05, -54.06), (-27.05, -54.06),
             (27.05, 47.04), (27.05, 54.04), (-16.55, 47.04), (-16.55, 54.04)]
PLOT_D = 4.5                       # diamètre des plots d'appui
EP_MOUSSE_PLOT = 1.0               # pastille de mousse EVA sur chaque plot
ECRASEMENT_MOUSSE = 0.3            # écrasement voulu de la mousse
JEU_PLI_NAPPE = 0.4                # dégagement en plus pour le pli des nappes aux deux bouts

# --- Batterie LiPo 103450 2000 mAh ------------------------------------------
BATT_L, BATT_W, BATT_EP = 50.0, 34.0, 10.0   # annonce vendeur - À MESURER
BATT_CENTRE = (0.0, 16.5)          # placée sous les composants bas (SOIC, nappe)
BATT_JEU_XY = 0.5                  # jeu par côté dans le berceau
BATT_GONFLEMENT = 0.8              # marge de gonflement au-dessus de la batterie
EP_MOUSSE_BATT = 0.5               # adhésif double face mousse sous la batterie
H_COMP_SOUS_BATT = 2.0             # plus haut composant au-dessus de la batterie (SOIC 1,75)
BERCEAU_EP = 1.2                   # parois du berceau
BERCEAU_H = 6.0                    # hauteur des parois au-dessus de la mousse
BERCEAU_ENCOCHE_FILS = (-10.0, 12.0)   # passage des fils (côté bas), en X
BERCEAU_ENCOCHE_HAUT = (-6.0, 6.0)     # encoche côté haut pour sortir la batterie

# --- Boutons CMS 3 x 6 x 4,3 (appui dessus), fiche HYP 1TS002B --------------
SW_L, SW_W = 6.0, 3.5              # corps (vérifié fiche HYP)
SW_H = 4.3                         # hauteur totale avec poussoir (vérifié fiche HYP)
SW_ACT = (3.0, 1.5, 0.8)           # poussoir : long, large, dépasse de 0,8
SW_PATTES = 8.0                    # longueur pattes comprises
SW_COURSE = 0.25                   # course (fiche : 0,25 ± 0,1)
JEU_SW = 0.15                      # jeu du bouton dans son logement
JEU_BOUTON = 0.10                  # jeu capuchon -> poussoir au repos
Z_BOUTONS = 7.0                    # hauteur des 3 boutons (même hauteur partout)
Y_VOLUME = 10.0                    # volume + (droite) et volume - (gauche)
CAP_VOL = (9.5, 3.4)               # capuchons volume (long, haut) : pont <= 10 mm
CAP_MARCHE = (9.5, 3.4)            # capuchon marche/veille
CAP_R = 0.8                        # rayon des coins des capuchons
CAP_SAILLIE = 0.6                  # dépasse de la coque
JEU_CAP = 0.15                     # jeu par côté dans le trou
COL_EP = 0.8                       # collerette (anti-chute) : épaisseur
COL_DEBORD = 0.9                   # collerette : débord autour du capuchon
BOSSAGE_MARGE = 1.0                # matière autour de la collerette
SIEGE_MIN = 0.4                    # épaisseur mini du bossage plat (siège de la collerette)
DOS_EP = 1.6                       # dos du berceau de bouton (seule référence précise de l'interrupteur)
NERV_EP = 1.2                      # largeur des appuis à côté de l'interrupteur
# Le capuchon se monte par l'intérieur : il doit pouvoir reculer de toute la longueur de son
# corps (3,8 mm) derrière le siège. Rien du berceau ne dépasse donc dans son passage : le dos
# est derrière, le socle reste sous la collerette, et l'interrupteur n'est guidé que par un
# rebord de REBORD mm sur la face du dos (il est ensuite collé).
REBORD = 0.4                       # rebord et butées sur la face du dos
JEU_PASSAGE = 0.4                  # jeu entre le socle et le bas de la collerette

# --- Forme « Lingot » --------------------------------------------------------
PAROI = 1.6                        # 4 périmètres de 0,4
FOND = 1.6                         # fond de coque
CHANF_AV = 2.0                     # chanfrein avant 45°
CHANF_AR = 1.5                     # chanfrein arrière 45°
DEPOUILLE_DEG = 12.0               # dépouille des flancs (plus large côté écran)
K_COIN = 3.0                       # pan coupé des coins au plus large (limité par les coins du verre)
LEVRE = CHANF_AV                   # épaisseur de cadre au-dessus du verre
FENETRE_MARGE = 0.8                # fenêtre = zone active + marge par côté (ne touche pas le tactile)
FENETRE_DROIT = 0.8                # partie droite du bord de fenêtre (au-dessus du verre)
RAINURE_L, RAINURE_P = 0.6, 0.4    # rainures façon Pamk
RAINURE_Y = 38.0                   # position des deux rainures (± Y)
JOINT_V = 0.4                      # petit V entre cadre et coque (et prise d'ongle)
FENTES_AUDIO = (12.0, 1.1)         # fentes écouteur / micro dans les chanfreins haut et bas
FENTES_AUDIO_TRAVERSANTES = False  # V0 : borgnes (cosmétiques). True pour la V1 avec son
FENTES_AUDIO_PROF = 0.6

# --- Jonction cadre / coque (clipsage façon liseuse) --------------------------
T_JUPE = 1.5                       # jupe du cadre sous le verre
JEU_JUPE = 0.1
T_LANG = 1.2                       # languettes de la coque qui montent dans la jupe
PERLE = 0.35                       # bourrelet des languettes
GORGE = 0.45                       # gorge correspondante dans la jupe
# Languettes : (côté, position le long du côté, longueur). Seulement sur les flancs : aux deux
# bouts, le verre ne dépasse le PCB que de 1,47 mm, il n'y a pas la place.
LANGUETTES = [(c, y, 16) for c in ("droite", "gauche") for y in (-48, -16, 16, 48)]

# --- Plaque aimantée + visage pixel ------------------------------------------
PLAQUE = (28.5, 34.0, 6.0)         # demi-largeur, demi-longueur, pan coupé (1,9 mm autour des aimants)
PLAQUE_EP = 1.4
PLAQUE_CHANF = 0.6
AIMANT_D, AIMANT_H = 6.0, 2.0      # aimants néodyme 6 x 2
AIMANT_JEU_D = 0.2                 # trou = 6,2 (serré en FDM, goutte de colle)
AIMANTS = [(23.5, 21.0), (-23.5, 21.0), (23.5, -15.0), (-23.5, -15.0)]   # asymétrique = détrompeur
AIMANT_ENFONCE_PLAQUE = 0.8        # profondeur de l'aimant dans la plaque (il dépasse de 1,2)
AIMANT_PEAU = 0.6                  # peau de plastique entre les aimants
VISAGE_ECHELLE = 1.1               # visage du visuel (cases de 2,2 au pas de 2,6) x échelle
VISAGE_RELIEF = 0.5

# =============================================================================
# COTES DÉDUITES (ne pas modifier : elles suivent les paramètres)
# =============================================================================
SQ = 2 - math.sqrt(2)              # un pan coupé raccourcit de d(2-√2) quand on décale de d
TAN = math.tan(math.radians(DEPOUILLE_DEG))
COS = math.cos(math.radians(DEPOUILLE_DEG))
E_PAROI = PAROI / COS              # retrait horizontal de la face intérieure des flancs

# Empilage en Z (de l'arrière vers l'avant)
Z_BATT_BAS = FOND + EP_MOUSSE_BATT
Z_BATT_HAUT = Z_BATT_BAS + BATT_EP
Z_PCB_AR = Z_BATT_HAUT + BATT_GONFLEMENT + H_COMP_SOUS_BATT
Z_PCB_AV = Z_PCB_AR + PCB_EP
Z_VERRE_AR = Z_PCB_AV + EP_DALLE
Z_VERRE_AV = Z_VERRE_AR + VERRE_EP
Z_LEVRE = Z_VERRE_AV + EP_ADHESIF_VERRE
Z2 = Z_LEVRE                       # section la plus large (fin du chanfrein avant)
H = Z_LEVRE + LEVRE                # face avant
Z3 = CHANF_AR                      # début du chanfrein arrière
T_DEP = (Z2 - Z3) * TAN            # retrait total dû à la dépouille
Z_J = Z_PCB_AR + 1.1               # plan de joint cadre / coque
Z_LANG_HAUT = Z_VERRE_AR - 0.3     # sommet des languettes (juste sous le verre)
Z_PERLE = Z_J + 0.9

# Demi-largeur / demi-longueur au plus large : paroi de PAROI au fond de la poche du verre
_d_poche = (Z2 - Z_VERRE_AR) * TAN
A = VERRE_W / 2 + JEU_VERRE + PAROI + _d_poche
B = VERRE_L / 2 + abs(VERRE_DECALAGE_Y) + JEU_VERRE + PAROI + _d_poche

# Zone active : centrée en X, son bord haut à AA_BORD_HAUT du bord haut du verre
AA_CY = VERRE_DECALAGE_Y + VERRE_L / 2 - AA_BORD_HAUT - AA_L / 2
# Dalle : bord côté nappe (haut) à DALLE_AA_BORD_NAPPE au-dessus de la zone active
DALLE_HAUT = AA_CY + AA_L / 2 + DALLE_AA_BORD_NAPPE
DALLE_CY = DALLE_HAUT - DALLE_L / 2


def decal(z):
    """Retrait horizontal d de la surface extérieure à la hauteur z, et pan coupé k."""
    if z >= Z2:
        d = z - Z2
        return d, K_COIN - d * SQ
    if z >= Z3:
        return (Z2 - z) * TAN, K_COIN
    return T_DEP + (Z3 - z), K_COIN - (Z3 - z) * SQ


# =============================================================================
# OUTILS DE GÉOMÉTRIE
# =============================================================================
def octo(a, b, k, z):
    pts = [(a, -(b - k)), (a, b - k), (a - k, b), (-(a - k), b),
           (-a, b - k), (-a, -(b - k)), (-(a - k), -b), (a - k, -b)]
    return cq.Wire.makePolygon([cq.Vector(x, y, z) for x, y in pts], close=True)


def corps(e=0.0, z0=0.0, z1=None):
    """Lingot extérieur entre z0 et z1, rentré de e (horizontalement) partout."""
    z1 = H if z1 is None else z1
    niveaux = sorted({z0, z1} | {z for z in (Z3, Z2) if z0 < z < z1})
    fils = []
    for z in niveaux:
        d, k = decal(z)
        fils.append(octo(A - d - e, B - d - e, k - e * SQ, z))
    return cq.Solid.makeLoft(fils, True)


def boite(x0, x1, y0, y1, z0, z1):
    return cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, cq.Vector(x0, y0, z0))


def cyl(x, y, z0, z1, d):
    return cq.Solid.makeCylinder(d / 2, z1 - z0, cq.Vector(x, y, z0))


def rect_arrondi(cx, cy, w, h, r, z0, z1):
    return (cq.Workplane("XY").workplane(offset=z0).center(cx, cy)
            .rect(w, h).extrude(z1 - z0).edges("|Z").fillet(r).val())


def barre_x(x0, x1, cy, cz, ly, lz, r):
    """Pavé arrondi d'axe X (capuchons, trous de bouton)."""
    s = boite(x0, x1, cy - ly / 2, cy + ly / 2, cz - lz / 2, cz + lz / 2)
    return cq.Workplane().add(s).edges("|X").fillet(min(r, lz / 2 - 0.01, ly / 2 - 0.01)).val()


def stade_y(cx, cz, lx, lz, y0, y1):
    """Fente oblongue d'axe Y (longue en X)."""
    r = lz / 2
    s = boite(cx - (lx / 2 - r), cx + (lx / 2 - r), y0, y1, cz - r, cz + r)
    for sx in (-1, 1):
        s = s.fuse(cq.Solid.makeCylinder(r, y1 - y0, cq.Vector(cx + sx * (lx / 2 - r), y0, cz),
                                         cq.Vector(0, 1, 0)))
    return s.clean()


def stade_x(cy, cz, ly, lz, x0, x1):
    """Fente oblongue d'axe X (longue en Y)."""
    r = lz / 2
    s = boite(x0, x1, cy - (ly / 2 - r), cy + (ly / 2 - r), cz - r, cz + r)
    for sy in (-1, 1):
        s = s.fuse(cq.Solid.makeCylinder(r, x1 - x0, cq.Vector(x0, cy + sy * (ly / 2 - r), cz),
                                         cq.Vector(1, 0, 0)))
    return s.clean()


def prisme_xz(pts, y0, y1):
    """Polygone dans le plan XZ (x, z) extrudé de y0 à y1."""
    s = cq.Workplane("XZ").polyline(pts).close().extrude((y1 - y0) / 2, both=True).val()
    return s.translate(cq.Vector(0, (y0 + y1) / 2, 0))


def fusion(formes):
    formes = [f for f in formes if f is not None]
    if not formes:
        return None
    r = formes[0]
    if len(formes) > 1:
        r = r.fuse(*formes[1:])
    return r.clean()


# Les détails des flancs et des bouts sont construits sur le flanc droit (+X, paroi au
# plus large en u = W), centrés en Y = 0, puis tournés et placés.
COTES = {"droite": (0, A), "gauche": (180, A), "haut": (90, B), "bas": (-90, B)}


def placer(forme, cote, pos):
    ang, _ = COTES[cote]
    f = forme.rotate(cq.Vector(0, 0, 0), cq.Vector(0, 0, 1), ang) if ang else forme
    if cote in ("droite", "gauche"):
        return f.translate(cq.Vector(0, pos, 0))
    return f.translate(cq.Vector(pos, 0, 0))


def u_ext(W, z):
    return W - decal(z)[0]


# =============================================================================
# PIÈCES
# =============================================================================
def languette(W, longueur):
    """Languette de la coque (avec son chanfrein à ~45° dessous) + bourrelet, sur le flanc +X."""
    zj, zh = Z_J, Z_LANG_HAUT
    pts = [(u_ext(W, zj - 1.0) - E_PAROI, zj - 1.0),
           (u_ext(W, zj) - T_JUPE - JEU_JUPE - T_LANG, zj),
           (u_ext(W, zh) - T_JUPE - JEU_JUPE - T_LANG, zh),
           (u_ext(W, zh) - T_JUPE - JEU_JUPE, zh),
           (u_ext(W, zj) - T_JUPE - JEU_JUPE, zj),
           (u_ext(W, zj) - 1.0, zj),
           (u_ext(W, zj - 1.0) - 1.0, zj - 1.0)]
    lang = prisme_xz(pts, -longueur / 2, longueur / 2)
    uf = u_ext(W, Z_PERLE) - T_JUPE - JEU_JUPE
    perle = prisme_xz([(uf - 0.2, Z_PERLE - PERLE - 0.2), (uf + PERLE, Z_PERLE),
                       (uf - 0.2, Z_PERLE + PERLE + 0.2)], -longueur / 2 + 1, longueur / 2 - 1)
    return lang.fuse(perle).clean()


def gorge_jupe(W, longueur):
    uf = u_ext(W, Z_PERLE) - T_JUPE
    return prisme_xz([(uf - 0.3, Z_PERLE - GORGE - 0.3), (uf + GORGE, Z_PERLE),
                      (uf - 0.3, Z_PERLE + GORGE + 0.3)], -longueur / 2 - 0.5, longueur / 2 + 0.5)


def bouton(W, cap):
    """Bouton sur le flanc +X, centré en Y=0 : bossage + berceau (à ajouter à la coque),
    trou (à retirer), capuchon et interrupteur (pièces)."""
    cap_l, cap_h = cap
    zb = Z_BOUTONS
    hb = cap_h / 2 + COL_DEBORD + BOSSAGE_MARGE
    lb = cap_l / 2 + COL_DEBORD + BOSSAGE_MARGE
    u_in_bas = u_ext(W, zb - hb) - E_PAROI
    u_siege = u_in_bas - SIEGE_MIN                       # siège plat vertical de la collerette
    u_bout = u_siege - COL_EP - JEU_BOUTON               # bout du poussoir
    u_base = u_bout - SW_H                               # base de l'interrupteur
    u_capot = u_bout - SW_ACT[2]                         # dessus du corps
    demi_logement = SW_L / 2 + JEU_SW
    demi_dos = demi_logement + NERV_EP
    z_col_bas = zb - cap_h / 2 - COL_DEBORD                                    # bas de la collerette
    z_appui = zb - SW_W / 2 - JEU_SW                                           # dessous de l'interrupteur
    ajout = [boite(u_siege, W + 1, -lb, lb, zb - hb, zb + hb)]                 # bossage
    ajout.append(boite(u_base - DOS_EP, u_base, -demi_dos, demi_dos,
                       FOND - 0.1, zb + SW_W / 2 + 0.6))                       # dos
    ajout.append(boite(u_base - DOS_EP, u_capot, -demi_dos, demi_dos,
                       FOND - 0.1, z_col_bas - JEU_PASSAGE))                   # socle, sous la collerette
    ajout.append(boite(u_base - 0.01, u_base + REBORD, -demi_dos, demi_dos,
                       FOND - 0.1, z_appui))                                   # rebord porteur sur le dos
    for s in (-1, 1):                                                          # butées latérales (sous les pattes)
        y0, y1 = sorted((s * demi_logement, s * demi_dos))
        ajout.append(boite(u_base - 0.01, u_base + REBORD, y0, y1, z_appui - 0.1, z_appui + 0.8))
    trou = barre_x(u_siege - 0.5, W + 2, 0, zb, cap_l + 2 * JEU_CAP, cap_h + 2 * JEU_CAP, CAP_R + JEU_CAP)
    # Capuchon : corps + collerette, face extérieure parallèle au flanc
    corps_cap = barre_x(u_siege - 0.01, W + 3, 0, zb, cap_l, cap_h, CAP_R)
    col = barre_x(u_siege - COL_EP, u_siege, 0, zb, cap_l + 2 * COL_DEBORD, cap_h + 2 * COL_DEBORD, CAP_R + 0.5)
    sail = CAP_SAILLIE / COS

    def u_face(z):          # plan du flanc (zone en dépouille) décalé de la saillie
        return W - (Z2 - z) * TAN + sail
    z_lo, z_hi = zb - hb - 2, zb + hb + 2
    garde = prisme_xz([(u_base - 5, z_lo), (u_face(z_lo), z_lo),
                       (u_face(z_hi), z_hi), (u_base - 5, z_hi)], -lb - 2, lb + 2)
    capuchon = col.fuse(corps_cap).clean().intersect(garde)
    # Interrupteur (pièce achetée, pour les contrôles)
    inter = fusion([
        boite(u_base, u_capot, -SW_L / 2, SW_L / 2, zb - SW_W / 2, zb + SW_W / 2),
        boite(u_capot, u_bout, -SW_ACT[0] / 2, SW_ACT[0] / 2, zb - SW_ACT[1] / 2, zb + SW_ACT[1] / 2),
        boite(u_base, u_base + 0.3, -SW_PATTES / 2, SW_PATTES / 2, zb - 0.5, zb + 0.5)])
    infos = {"u_siege": u_siege, "u_bout": u_bout, "u_base": u_base, "paroi_ext": u_ext(W, zb)}
    return ajout, trou, capuchon, inter, infos


def construire():
    """Construit toutes les pièces. Renvoie (pieces, reference, infos)."""
    infos = {}
    ext_coque = corps(0, 0, Z_J)
    cavite = corps(E_PAROI, FOND, Z_J + 1)
    coque = ext_coque.cut(cavite)

    ajouts, retraits = [], []
    # Languettes de clipsage
    for cote, pos, lg in LANGUETTES:
        ajouts.append(placer(languette(COTES[cote][1], lg), cote, pos))
    # Berceau de batterie
    bx, by = BATT_CENTRE
    ix, iy = BATT_W / 2 + BATT_JEU_XY, BATT_L / 2 + BATT_JEU_XY
    e, zh = BERCEAU_EP, Z_BATT_BAS + BERCEAU_H
    ajouts.append(boite(bx - ix - e, bx - ix, by - iy - e, by + iy + e, FOND - 0.1, zh))
    ajouts.append(boite(bx + ix, bx + ix + e, by - iy - e, by + iy + e, FOND - 0.1, zh))
    murs_y = [(by - iy - e, by - iy, BERCEAU_ENCOCHE_FILS), (by + iy, by + iy + e, BERCEAU_ENCOCHE_HAUT)]
    for y0, y1, (n0, n1) in murs_y:
        for x0, x1 in ((bx - ix - e, bx + n0), (bx + n1, bx + ix + e)):
            if x1 > x0:
                ajouts.append(boite(x0, x1, y0, y1, FOND - 0.1, zh))
    # Aimants : bossages, logements (ouverts vers l'intérieur) et creux au dos pour ceux de la plaque
    dp = AIMANT_D + AIMANT_JEU_D
    z_creux = AIMANT_H - AIMANT_ENFONCE_PLAQUE + 0.1
    z_aim0 = z_creux + AIMANT_PEAU
    z_aim1 = z_aim0 + AIMANT_H + 0.05
    for x, y in AIMANTS:
        ajouts.append(cyl(x, y, FOND - 0.1, z_aim1, dp + 2 * 1.2))
        retraits.append(cyl(x, y, z_aim0, z_aim1 + 1, dp))
        retraits.append(cyl(x, y, -1, z_creux, dp + 0.2))
    infos["z_aimant_coque"] = (z_aim0, z_aim1)
    # Plots d'appui de la carte (mousse dessus)
    z_plot = Z_PCB_AR - (EP_MOUSSE_PLOT - ECRASEMENT_MOUSSE)
    for x, y in TROUS_PCB:
        ajouts.append(cyl(x, y, FOND - 0.1, z_plot, PLOT_D))
    infos["z_plot"] = z_plot
    # Boutons : volume + à droite, volume - à gauche, marche en haut
    caps, inters = {}, {}
    for nom, cote, pos, cap in (("volume_plus", "droite", Y_VOLUME, CAP_VOL),
                                ("volume_moins", "gauche", Y_VOLUME, CAP_VOL),
                                ("marche", "haut", 0.0, CAP_MARCHE)):
        aj, tr, cp, it, inf = bouton(COTES[cote][1], cap)
        ajouts += [placer(a, cote, pos) for a in aj]
        retraits.append(placer(tr, cote, pos))
        caps[nom] = placer(cp, cote, pos)
        inters[nom] = placer(it, cote, pos)
        infos["bouton_" + nom] = inf

    # rogne ce qui dépasserait du lingot (les languettes montent au-dessus du joint, dans la jupe)
    coque = coque.fuse(*ajouts).clean().intersect(corps(0, 0, H))

    # USB-C : ouverture en U ouverte vers le plan de joint (pas de pont), complétée par une
    # entaille dans le bas du cadre, à la taille d'un embout de câble : l'embout vient au
    # contact de la prise et la fiche s'enfonce à fond.
    zu = Z_PCB_AR - USB_Z_SOUS_PCB
    z_bas = zu - USB_ENCOCHE_SOUS_AXE
    r_u = USB_ENCOCHE_R
    y_fin = -B + 5                     # le découpage s'arrête dans la cavité
    encoche = boite(USB_X - USB_ENCOCHE_L / 2, USB_X + USB_ENCOCHE_L / 2, -B - 2, y_fin, z_bas + r_u, Z_J + 1)
    encoche = encoche.fuse(boite(USB_X - USB_ENCOCHE_L / 2 + r_u, USB_X + USB_ENCOCHE_L / 2 - r_u,
                                 -B - 2, y_fin, z_bas, z_bas + r_u + 0.1))
    for sx in (-1, 1):
        encoche = encoche.fuse(cq.Solid.makeCylinder(r_u, y_fin + B + 2, cq.Vector(USB_X + sx * (USB_ENCOCHE_L / 2 - r_u),
                                                                                    -B - 2, z_bas + r_u), cq.Vector(0, 1, 0)))
    retraits.append(encoche.clean())
    entaille_usb = boite(USB_X - USB_ENCOCHE_L / 2, USB_X + USB_ENCOCHE_L / 2, -B - 2, y_fin,
                         Z_J - 1, zu + USB_ENCOCHE_SUR_AXE)
    infos["usb_bouche_retrait"] = u_ext(B, zu) - (-COMPOSANTS["usb_c"][2])
    # Dégagement des plis de nappe aux deux bouts (paroi 1,2 localement)
    relief = corps(E_PAROI - JEU_PLI_NAPPE, FOND, Z_J + 0.1).cut(cavite)
    retraits.append(relief.intersect(boite(-25, 30, B - 8, B + 1, Z_PCB_AR - 1.5, Z_J + 0.1)))
    retraits.append(relief.intersect(boite(-12, 2, -B - 1, -B + 8, Z_PCB_AR - 1.5, Z_J + 0.1)))
    # Lecteur TF : fente sur le flanc droit
    zt = Z_PCB_AR - TF_Z_SOUS_PCB
    retraits.append(stade_x(TF_Y, zt, TF_FENTE[0], TF_FENTE[1], A - 8, A + 2))
    # Trous d'épingle BOOT / RST
    zbc = Z_PCB_AR - BOUTONS_CARTE_Z_SOUS_PCB
    for nom in TROUS_EPINGLE:
        retraits.append(cq.Solid.makeCylinder(TROU_EPINGLE_D / 2, 10, cq.Vector(A - 6, BOUTONS_CARTE[nom], zbc),
                                              cq.Vector(1, 0, 0)))
    # Rainures façon Pamk (dos + chanfrein arrière + flancs jusqu'au joint)
    peau = ext_coque.cut(corps(RAINURE_P, RAINURE_P, Z_J + 1))
    for s in (-1, 1):
        retraits.append(peau.intersect(boite(-A - 1, A + 1, s * RAINURE_Y - RAINURE_L / 2,
                                             s * RAINURE_Y + RAINURE_L / 2, -1, Z_J - JOINT_V)))
    # V du joint
    v_int = cq.Solid.makeLoft([octo(A - decal(Z_J - JOINT_V)[0], B - decal(Z_J - JOINT_V)[0], decal(Z_J - JOINT_V)[1], Z_J - JOINT_V),
                               octo(A - decal(Z_J)[0] - JOINT_V, B - decal(Z_J)[0] - JOINT_V, decal(Z_J)[1] - JOINT_V * SQ, Z_J),
                               octo(A - decal(Z_J + JOINT_V)[0], B - decal(Z_J + JOINT_V)[0], decal(Z_J + JOINT_V)[1], Z_J + JOINT_V)], True)
    v_joint = corps(0, Z_J - JOINT_V, Z_J + JOINT_V).cut(v_int)
    retraits.append(v_joint)
    coque = coque.cut(fusion(retraits)).clean()

    # ------------------------------------------------------------- cadre avant
    cadre = corps(0, Z_J, H)
    rcad = [rect_arrondi(0, VERRE_DECALAGE_Y, VERRE_W + 2 * JEU_VERRE, VERRE_L + 2 * JEU_VERRE,
                         VERRE_R + JEU_VERRE, Z_VERRE_AR - 0.01, Z_LEVRE),
            corps(T_JUPE, Z_J - 1, Z_VERRE_AR)]
    fw, fl = AA_W + 2 * FENETRE_MARGE, AA_L + 2 * FENETRE_MARGE
    rcad.append(boite(-fw / 2, fw / 2, AA_CY - fl / 2, AA_CY + fl / 2, Z_LEVRE - 0.1, Z_LEVRE + FENETRE_DROIT))
    c = H - Z_LEVRE - FENETRE_DROIT

    def rect_fil(w, l, z):
        return cq.Wire.makePolygon([cq.Vector(x, AA_CY + y, z) for x, y in
                                    ((-w / 2, -l / 2), (w / 2, -l / 2), (w / 2, l / 2), (-w / 2, l / 2))], close=True)
    rcad.append(cq.Solid.makeLoft([rect_fil(fw, fl, Z_LEVRE + FENETRE_DROIT),
                                   rect_fil(fw + 2 * c + 0.02, fl + 2 * c + 0.02, H + 0.01)], True))
    for cote, pos, lg in LANGUETTES:
        rcad.append(placer(gorge_jupe(COTES[cote][1], lg), cote, pos))
    # Fentes écouteur (haut) / micro (bas) dans les chanfreins avant
    prof = 3.0 if FENTES_AUDIO_TRAVERSANTES else FENTES_AUDIO_PROF
    for s in (1, -1):
        f = (cq.Workplane("XY").slot2D(FENTES_AUDIO[0], FENTES_AUDIO[1]).extrude(prof + 0.6)
             .translate((0, 0, -prof)).val())
        f = f.rotate(cq.Vector(0, 0, 0), cq.Vector(1, 0, 0), -45 * s)
        rcad.append(f.translate(cq.Vector(0, s * (B - CHANF_AV / 2), Z2 + CHANF_AV / 2)))
    rcad.append(v_joint)
    rcad.append(entaille_usb)
    cadre = cadre.cut(fusion(rcad)).clean()

    # ------------------------------------------------------- plaque aimantée
    ap, bp, kp = PLAQUE
    zc = PLAQUE_EP - PLAQUE_CHANF
    plaque = cq.Solid.makeLoft([octo(ap, bp, kp, 0), octo(ap, bp, kp, -zc),
                                octo(ap - PLAQUE_CHANF, bp - PLAQUE_CHANF, kp - PLAQUE_CHANF * SQ, -PLAQUE_EP)], True)
    plaque = plaque.cut(fusion([cyl(x, y, -AIMANT_ENFONCE_PLAQUE, 0.1, dp) for x, y in AIMANTS])).clean()
    s = VISAGE_ECHELLE
    cases = []
    for cx in (-6.5, 6.5):
        for i in range(4):
            for j in range(4):
                if i in (0, 3) or j in (0, 3):
                    cases.append((cx + (i - 1.5) * 2.6, 7 + (j - 1.5) * 2.6))
    for i in range(9):
        cases.append(((i - 4) * 2.6, -7.6 if i % 2 else -5.0))
    c2 = 2.2 * s / 2
    visage = fusion([boite(x * s - c2, x * s + c2, y * s - c2, y * s + c2, -PLAQUE_EP - VISAGE_RELIEF, -PLAQUE_EP)
                     for x, y in cases])

    pieces = {"cadre_avant": cadre, "coque": coque, "plaque_aimantee": plaque, "plaque_visage": visage}
    for nom, cp in caps.items():
        pieces["capuchon_" + nom] = cp

    # ------------------------------------------- pièces achetées (référence)
    ref = {}
    ref["verre_tactile"] = rect_arrondi(0, VERRE_DECALAGE_Y, VERRE_W, VERRE_L, VERRE_R, Z_VERRE_AR, Z_VERRE_AV)
    ref["dalle_epaper"] = boite(-DALLE_W / 2, DALLE_W / 2, DALLE_CY - DALLE_L / 2, DALLE_CY + DALLE_L / 2,
                                Z_PCB_AV, Z_VERRE_AR)
    ex0, ex1, ey0, ey1 = ENCOCHE_ANTENNE
    ref["pcb"] = boite(-PCB_W / 2, PCB_W / 2, -PCB_L / 2, PCB_L / 2, Z_PCB_AR, Z_PCB_AV).cut(
        boite(ex0 - 1, ex1, ey0, ey1, Z_PCB_AR - 1, Z_PCB_AV + 1))
    for nom, (x0, x1, y0, y1, h) in COMPOSANTS.items():
        ref["comp_" + nom] = boite(x0, x1, y0, y1, Z_PCB_AR - h, Z_PCB_AR)
    m = MARGE_BORD_PCB
    nappe = boite(-PCB_W / 2 + m, PCB_W / 2 - m, -PCB_L / 2 + m, PCB_L / 2 - m, Z_PCB_AR - H_COMPOSANTS_GENERIQUE, Z_PCB_AR)
    nappe = nappe.cut(fusion([cyl(x, y, 0, H, PLOT_D + 1.5) for x, y in TROUS_PCB] +
                             [boite(ex0 - 1, ex1, ey0, ey1, 0, H)]))
    ref["comp_nappe_petits_composants"] = nappe
    ref["pli_nappe_dalle"] = boite(-12, 28, PCB_L / 2, PCB_L / 2 + 0.9, Z_PCB_AR - 0.5, Z_VERRE_AR)
    ref["batterie"] = rect_arrondi(bx, by, BATT_W, BATT_L, 1.0, Z_BATT_BAS, Z_BATT_HAUT)
    for nom, it in inters.items():
        ref["inter_" + nom] = it
    for i, (x, y) in enumerate(AIMANTS):
        ref[f"aimant_coque_{i + 1}"] = cyl(x, y, z_aim0, z_aim0 + AIMANT_H, AIMANT_D)
        ref[f"aimant_plaque_{i + 1}"] = cyl(x, y, -AIMANT_ENFONCE_PLAQUE, -AIMANT_ENFONCE_PLAQUE + AIMANT_H, AIMANT_D)

    infos.update({"A": A, "B": B, "H": H, "Z_J": Z_J, "Z_PCB_AR": Z_PCB_AR, "AA_CY": AA_CY})
    return pieces, ref, infos


# =============================================================================
# ORIENTATIONS D'IMPRESSION ET EXPORTS
# =============================================================================
ORIENTATION = {   # pièce -> description pour le README / la vérification
    "cadre_avant": "face avant sur le plateau",
    "coque": "dos sur le plateau",
    "plaque_aimantee": "face intérieure (logements d'aimants) sur le plateau, visage en haut",
    "plaque_visage": "avec la plaque (même objet, 2e filament)",
    "capuchon_volume_plus": "collerette sur le plateau",
    "capuchon_volume_moins": "collerette sur le plateau",
    "capuchon_marche": "collerette sur le plateau",
}


def vers_impression(nom, forme):
    """Tourne la pièce dans son sens d'impression, posée en z = 0."""
    o = cq.Vector(0, 0, 0)
    if nom in ("cadre_avant", "plaque_aimantee", "plaque_visage"):
        f = forme.rotate(o, cq.Vector(1, 0, 0), 180)
    elif nom == "capuchon_volume_plus":
        f = forme.rotate(o, cq.Vector(0, 1, 0), -90)      # +X -> +Z
    elif nom == "capuchon_volume_moins":
        f = forme.rotate(o, cq.Vector(0, 1, 0), 90)       # -X -> +Z
    elif nom == "capuchon_marche":
        f = forme.rotate(o, cq.Vector(1, 0, 0), 90)       # +Y -> +Z
    else:
        f = forme
    if nom == "plaque_visage":   # même transformation que la plaque pour rester alignés
        bb = vers_impression.bb_plaque
    else:
        bb = f.BoundingBox()
        if nom == "plaque_aimantee":
            vers_impression.bb_plaque = bb
    return f.translate(cq.Vector(-(bb.xmin + bb.xmax) / 2, -(bb.ymin + bb.ymax) / 2, -bb.zmin))


COULEURS = {"cadre_avant": (0.11, 0.11, 0.12), "coque": (0.37, 0.42, 0.27),
            "plaque_aimantee": (0.11, 0.11, 0.12), "plaque_visage": (0.91, 0.64, 0.23),
            "capuchon_volume_plus": (0.37, 0.42, 0.27), "capuchon_volume_moins": (0.37, 0.42, 0.27),
            "capuchon_marche": (0.37, 0.42, 0.27)}


def exporter(pieces, ref):
    os.makedirs(os.path.join(DOSSIER_EXPORT, "step"), exist_ok=True)
    os.makedirs(os.path.join(DOSSIER_EXPORT, "stl"), exist_ok=True)
    assy = cq.Assembly(name="pamk_phone_v0")
    for nom, f in pieces.items():
        assy.add(f, name=nom, color=cq.Color(*COULEURS.get(nom, (0.6, 0.6, 0.6))))
        cq.exporters.export(f, os.path.join(DOSSIER_EXPORT, "step", nom + ".step"))
        cq.exporters.export(vers_impression(nom, f), os.path.join(DOSSIER_EXPORT, "stl", nom + ".stl"),
                            tolerance=0.01, angularTolerance=0.1)
    achat = cq.Assembly(name="pieces_achetees")
    for nom, f in ref.items():
        if nom.startswith("comp_nappe"):
            continue
        col = (0.13, 0.32, 0.23) if nom.startswith(("pcb", "comp")) else (0.75, 0.76, 0.78)
        if nom.startswith(("verre", "dalle")):
            col = (0.85, 0.85, 0.82)
        achat.add(f, name=nom, color=cq.Color(*col))
    assy.add(achat)
    assy.save(os.path.join(DOSSIER_EXPORT, "pamk_phone_v0_assemblage.step"))


def resume(pieces, infos):
    lignes = ["Pamk Phone V0 - cotes générales",
              f"  Encombrement du corps : {2 * A:.1f} (larg.) x {2 * B:.1f} (long.) x {H:.2f} (ép.) mm",
              f"  Avec la plaque et le visage : épaisseur {H + PLAQUE_EP + VISAGE_RELIEF:.2f} mm",
              f"  Face avant : {2 * (A - CHANF_AV):.1f} x {2 * (B - CHANF_AV):.1f} ; dos : "
              f"{2 * (A - T_DEP - CHANF_AR):.1f} x {2 * (B - T_DEP - CHANF_AR):.1f}",
              f"  Retrait de dépouille : {T_DEP:.2f} mm par côté ({DEPOUILLE_DEG:.0f}°)",
              f"  Empilage Z : fond {FOND} | batterie {Z_BATT_BAS:.2f}-{Z_BATT_HAUT:.2f} | dos PCB {Z_PCB_AR:.2f}"
              f" | verre {Z_VERRE_AR:.2f}-{Z_VERRE_AV:.2f} | lèvre {Z_LEVRE:.2f} | avant {H:.2f} | joint {Z_J:.2f}",
              f"  Fenêtre écran : {AA_W + 2 * FENETRE_MARGE:.2f} x {AA_L + 2 * FENETRE_MARGE:.2f} au ras du verre, "
              f"centre Y = {AA_CY:.2f}"]
    for nom, f in pieces.items():
        bb = f.BoundingBox()
        lignes.append(f"  {nom:24s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:5.2f}   volume {f.Volume() / 1000:6.2f} cm³")
    return "\n".join(lignes)


def main():
    print("Construction des pièces...")
    pieces, ref, infos = construire()
    texte = resume(pieces, infos)
    print(texte)
    print("Export STEP / STL...")
    exporter(pieces, ref)
    os.makedirs(DOSSIER_EXPORT, exist_ok=True)
    with open(os.path.join(DOSSIER_EXPORT, "cotes.txt"), "w", encoding="utf-8") as f:
        f.write(texte + "\n")
    if "--sans-verif" not in sys.argv:
        import verification_v0
        verification_v0.verifier(pieces, ref, infos)
    if "--sans-apercus" not in sys.argv:
        import apercus_v0
        apercus_v0.rendre(pieces, ref, infos)
    print("Terminé.")


if __name__ == "__main__":
    sys.path.insert(0, ICI)
    sys.stdout.reconfigure(encoding="utf-8")
    main()
