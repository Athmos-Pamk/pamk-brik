# -*- coding: utf-8 -*-
"""
Aperçus PNG du Pamk Phone V0 (rendu maison : projection orthographique + ombrage plat,
dessiné avec matplotlib, aucune carte graphique nécessaire).

Lancé automatiquement par telephone_v0.py. Seul :
    cd A:\\bernon\\Robotique\\Pamk_Phone\\cad ; .\\.venv\\Scripts\\python.exe apercus_v0.py
"""
import os
import sys

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import trimesh

ICI = os.path.dirname(os.path.abspath(__file__))
DOSSIER = os.path.join(ICI, "export", "apercus")

COUL = {
    "cadre_avant": "#1d1e20", "coque": "#5f6c45", "plaque_aimantee": "#1d1e20", "plaque_visage": "#e9a23b",
    "capuchon_volume_plus": "#5f6c45", "capuchon_volume_moins": "#5f6c45", "capuchon_marche": "#5f6c45",
    "verre_tactile": "#d4d3cb", "dalle_epaper": "#e6e4dc", "pcb": "#21523a", "batterie": "#a4a8ab",
    "inter": "#3a3a3a", "aimant": "#c3c6c9", "comp": "#2b2f33", "pli": "#c58b2a",
}
NOMS_FR = {
    "cadre_avant": "Cadre avant", "coque": "Coque", "plaque_aimantee": "Plaque aimantée",
    "plaque_visage": "Visage pixel", "verre_tactile": "Verre tactile", "dalle_epaper": "Dalle e-paper",
    "pcb": "Carte (PCB)", "batterie": "Batterie 103450", "capuchon_volume_plus": "Volume +",
    "capuchon_volume_moins": "Volume -", "capuchon_marche": "Marche",
}


def couleur(nom):
    for cle in ("inter", "aimant", "comp", "pli"):
        if nom.startswith(cle):
            return COUL[cle]
    return COUL.get(nom, "#888888")


def maillage(forme, tol=0.05, arete=None):
    v, t = forme.tessellate(tol, 0.2)
    V, T = np.array([(p.x, p.y, p.z) for p in v]), np.array(t, dtype=int)
    if arete:   # petits triangles : le tri par profondeur devient fiable
        V, T = trimesh.remesh.subdivide_to_size(V, T, max_edge=arete, max_iter=12)
    return V, T


def _base(cam, haut):
    c = np.asarray(cam, float)
    c /= np.linalg.norm(c)
    f = -c
    r = np.cross(f, haut)
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return c, r, u


def rendre_vue(objets, cam, haut, fichier, titre, taille=(9, 9), notes=None):
    """objets : liste de (V, T, couleur hex, décalage xyz)."""
    c, r, u = _base(cam, haut)
    lum1 = np.array([0.35, 0.6, 0.75]) @ np.eye(3)
    lum1 = lum1 / np.linalg.norm(lum1)
    lum2 = -0.5 * c + 0.4 * u
    lum2 /= np.linalg.norm(lum2)
    polys, cols, prof = [], [], []
    for V, T, hexa, dec in objets:
        P = V + np.asarray(dec)
        tri = P[T]
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        ln = np.linalg.norm(n, axis=1)
        ok = ln > 1e-12
        n[ok] /= ln[ok, None]
        vis = (n @ c) > 1e-6
        tri, n = tri[vis], n[vis]
        base = np.array(matplotlib.colors.to_rgb(hexa))
        l1 = np.clip(n @ lum1, 0, 1)
        l2 = np.clip(n @ lum2, 0, 1)
        k = 0.38 + 0.52 * l1 + 0.18 * l2
        col = np.clip(base[None, :] * k[:, None] + 0.04, 0, 1)
        xy = np.stack([tri @ r, tri @ u], axis=-1)
        polys.append(xy)
        cols.append(col)
        prof.append((tri @ c).mean(axis=1))
    xy = np.concatenate(polys)
    col = np.concatenate(cols)
    d = np.concatenate(prof)
    ordre = np.argsort(d)
    fig, ax = plt.subplots(figsize=taille, dpi=140)
    fig.patch.set_facecolor("#e5e8df")
    ax.set_facecolor("#e5e8df")
    pc = PolyCollection(xy[ordre], facecolors=col[ordre], edgecolors=col[ordre], linewidths=0.15,
                        antialiased=True)
    ax.add_collection(pc)
    pts = xy.reshape(-1, 2)
    m = 6
    ax.set_xlim(pts[:, 0].min() - m, pts[:, 0].max() + m)
    ax.set_ylim(pts[:, 1].min() - m, pts[:, 1].max() + m)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title(titre, fontsize=13, color="#1c2017", loc="left")
    if notes:
        ax.text(0.01, 0.01, notes, transform=ax.transAxes, fontsize=8.5, color="#3b4233", va="bottom")
    fig.tight_layout()
    fig.savefig(fichier, facecolor=fig.get_facecolor())
    plt.close(fig)
    print("  aperçu :", os.path.relpath(fichier, ICI))


def coupe(axe, valeur, objets, ax, titre):
    """Coupe plane : axe 'y' (plan Y = valeur, on voit X/Z) ou 'x' (plan X = valeur, on voit Y/Z)."""
    normale = [0, 1, 0] if axe == "y" else [1, 0, 0]
    origine = [0, valeur, 0] if axe == "y" else [valeur, 0, 0]
    cols2d = [0, 2] if axe == "y" else [1, 2]
    legendes = {}
    for nom, (V, T) in objets.items():
        m = trimesh.Trimesh(V, T, process=False)
        try:
            s = m.section(plane_origin=origine, plane_normal=normale)
        except Exception:
            s = None
        if s is None:
            continue
        p2 = trimesh.path.Path2D(entities=s.entities, vertices=s.vertices[:, cols2d])
        try:
            polys = p2.polygons_full
        except Exception:
            polys = []
        hexa = couleur(nom)
        for pg in polys:
            x, y = pg.exterior.xy
            ax.fill(x, y, color=hexa, alpha=0.95, lw=0)
            for trou in pg.interiors:
                xi, yi = trou.xy
                ax.fill(xi, yi, color="white", lw=0)
            ax.plot(x, y, color="#111111", lw=0.35)
        if polys:
            legendes[NOMS_FR.get(nom, None) or nom] = hexa
    ax.set_aspect("equal")
    ax.grid(True, lw=0.25, color="#bbbbbb")
    ax.set_title(titre, fontsize=10, loc="left")
    ax.tick_params(labelsize=7)
    return legendes


def rendre(pieces, ref, infos):
    os.makedirs(DOSSIER, exist_ok=True)
    print("Aperçus...")
    mesh_p = {n: maillage(f) for n, f in pieces.items()}
    mesh_r = {n: maillage(f) for n, f in ref.items() if not n.startswith(("comp_nappe", "pli"))}
    fin_p = {n: maillage(f, arete=1.6) for n, f in pieces.items()}
    fin_r = {n: maillage(f, arete=1.6) for n, f in ref.items() if not n.startswith(("comp_nappe", "pli"))}
    ecran = {"verre_tactile": fin_r["verre_tactile"]}
    H = infos["H"]
    centre = np.array([0, 0, -H / 2])

    def objs(dico, decal=None):
        out = []
        for n, (V, T) in dico.items():
            d = (decal or {}).get(n, (0, 0, 0))
            out.append((V + centre, T, couleur(n), d))
        return out

    dims = (f"{2 * infos['A']:.1f} x {2 * infos['B']:.1f} x {H:.1f} mm "
            f"({H + 1.9:.1f} avec la plaque)")
    rendre_vue(objs(fin_p) + objs(ecran), (1.0, 0.39, 0.95), (0, 1, 0), os.path.join(DOSSIER, "1_trois_quarts_avant.png"),
               "Pamk Phone V0 - 3/4 avant", notes=dims)
    rendre_vue(objs(fin_p), (-1.0, 0.39, -0.95), (0, 1, 0), os.path.join(DOSSIER, "2_trois_quarts_dos.png"),
               "Pamk Phone V0 - 3/4 dos (plaque aimantée, visage pixel)", notes=dims)
    rendre_vue(objs(fin_p) + objs(ecran), (0.28, -1.0, 0.22), (0, 0, 1), os.path.join(DOSSIER, "3_dessous_usb.png"),
               "Dessous : ouverture USB-C à la taille de l'embout (coque + bas du cadre), fente micro borgne", taille=(9, 6))
    rendre_vue(objs(fin_p) + objs(ecran), (1.0, -0.12, 0.18), (0, 0, 1), os.path.join(DOSSIER, "3b_flanc_droit.png"),
               "Flanc droit : volume +, fente TF, trous d'épingle BOOT et RST", taille=(11, 5))

    # Vue éclatée : chaque couche décalée en Z, capuchons écartés vers l'extérieur
    dz = {"cadre_avant": 70, "verre_tactile": 52, "dalle_epaper": 44, "pcb": 34, "batterie": 14,
          "plaque_aimantee": -30, "plaque_visage": -30}
    ecl = {}
    for n in list(mesh_p) + list(mesh_r):
        z = dz.get(n, 0)
        if n.startswith("comp_"):
            z = dz["pcb"]
        if n.startswith("aimant_plaque"):
            z = -30
        if n.startswith("aimant_coque"):
            z = 0
        x = y = 0
        if n.endswith("volume_plus"):
            x = 14
        if n.endswith("volume_moins"):
            x = -14
        if n.endswith("marche"):
            y = 14
        ecl[n] = (x, y, z)
    tout = objs(fin_p, ecl) + objs(fin_r, ecl)
    rendre_vue(tout, (1.0, 0.55, 0.8), (0, 1, 0), os.path.join(DOSSIER, "4_eclate.png"),
               "Vue éclatée : cadre, verre + dalle + carte, batterie, coque, plaque", taille=(10, 11),
               notes="pièces achetées en gris/vert (référence pour les contrôles), pièces imprimées en couleur")

    # Coupes
    tous = {**mesh_p, **mesh_r}
    fig, axs = plt.subplots(2, 1, figsize=(12, 10), dpi=140, gridspec_kw={"height_ratios": [1, 1.6]})
    yv = infos.get("Y_VOLUME", 10.0)
    leg = coupe("y", yv, tous, axs[0], f"Coupe transversale Y = {yv:g} (boutons de volume)")
    axs[0].set_xlim(-40, 40)
    leg.update(coupe("x", 0.0, tous, axs[1], "Coupe longitudinale X = 0 (bouton marche en haut, à droite)"))
    axs[1].set_xlim(-66, 66)
    poignees = [plt.Rectangle((0, 0), 1, 1, color=c) for c in leg.values()]
    fig.legend(poignees, list(leg.keys()), loc="lower center", ncol=6, fontsize=8, frameon=False)
    fig.suptitle(f"Pamk Phone V0 - coupes (épaisseur {H:.2f} mm sans plaque)", fontsize=12, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0.05, 1, 0.97))
    f = os.path.join(DOSSIER, "5_coupes.png")
    fig.savefig(f)
    plt.close(fig)
    print("  aperçu :", os.path.relpath(f, ICI))

    # Zoom de coupe sur un bouton (course, collerette, jeu)
    fig, ax = plt.subplots(figsize=(8, 6), dpi=160)
    coupe("y", yv, tous, ax, f"Détail bouton volume + (coupe Y = {yv:g})")
    ax.set_xlim(20, 36)
    ax.set_ylim(0, 14)
    fig.tight_layout()
    f = os.path.join(DOSSIER, "6_detail_bouton.png")
    fig.savefig(f)
    plt.close(fig)
    print("  aperçu :", os.path.relpath(f, ICI))


if __name__ == "__main__":
    sys.path.insert(0, ICI)
    sys.stdout.reconfigure(encoding="utf-8")
    import telephone_v0 as t
    p, r, i = t.construire()
    i["Y_VOLUME"] = t.Y_VOLUME
    rendre(p, r, i)
