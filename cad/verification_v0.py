# -*- coding: utf-8 -*-
"""
Vérifications chiffrées du Pamk Phone V0 (par le calcul, pas à l'œil) :
  1. chaque pièce : solide valide, fermé, d'un seul tenant ; STL étanche
  2. interférences : volume d'intersection entre toutes les pièces imprimées et achetées
  3. jeux : distances mini entre les couples importants (comparées aux jeux voulus)
  4. épaisseur de paroi sous la peau extérieure (lancer de rayons sur la coque et le cadre)
  5. impression sans support : porte-à-faux > 45° et ponts, dans le sens d'impression
  6. encombrements

Lancé par telephone_v0.py ; résultat dans export/verification.txt.
Seul : cd A:\\bernon\\Robotique\\Pamk_Phone\\cad ; .\\.venv\\Scripts\\python.exe verification_v0.py
"""
import itertools
import os
import sys

import cadquery as cq
import numpy as np
import trimesh
from OCP.BRep import BRep_Tool
from OCP.BRepExtrema import BRepExtrema_DistShapeShape

ICI = os.path.dirname(os.path.abspath(__file__))
EXPORT = os.path.join(ICI, "export")


class Rapport:
    def __init__(self):
        self.lignes = []
        self.alertes = []

    def __call__(self, txt=""):
        print(txt)
        self.lignes.append(txt)

    def alerte(self, txt):
        self.alertes.append(txt)
        self("  !! " + txt)


def distance(a, b):
    d = BRepExtrema_DistShapeShape(a.wrapped, b.wrapped)
    d.Perform()
    return d.Value() if d.IsDone() else float("nan")


def boites_proches(a, b, marge=0.5):
    p, q = a.BoundingBox(), b.BoundingBox()
    return not (p.xmax + marge < q.xmin or q.xmax + marge < p.xmin or p.ymax + marge < q.ymin or
                q.ymax + marge < p.ymin or p.zmax + marge < q.zmin or q.zmax + marge < p.zmin)


def volume_inter(a, b):
    if not boites_proches(a, b, 0.0):
        return 0.0
    try:
        return a.intersect(b).Volume()
    except Exception:
        return float("nan")


def maillage(forme, tol=0.02):
    v, t = forme.tessellate(tol, 0.1)
    return trimesh.Trimesh(np.array([(p.x, p.y, p.z) for p in v]), np.array(t), process=True)


def epaisseur_peau(forme, n=9000, graine=1, garde_arete=0.6, lateral=True):
    """Épaisseur de matière sous les points de la peau EXTÉRIEURE (rayon vers l'intérieur).
    On ne garde que les parois latérales (normale à moins de 45° de l'horizontale) : les
    faces du dessus/dessous (bord du joint, dessous de jupe) donneraient de fausses valeurs.
    Les points à moins de garde_arete d'une arête vive sont écartés (le rayon ressortirait
    par la face voisine)."""
    from scipy.spatial import cKDTree
    m = maillage(forme)
    pts, idx = trimesh.sample.sample_surface(m, n, seed=graine)
    nrm = m.face_normals[idx]
    if lateral:
        lat = np.abs(nrm[:, 2]) < 0.7
        pts, nrm = pts[lat], nrm[lat]
    vives = m.face_adjacency_edges[m.face_adjacency_angles > np.radians(20)]
    if len(vives):
        a, b = m.vertices[vives[:, 0]], m.vertices[vives[:, 1]]
        lg = np.linalg.norm(b - a, axis=1)
        nb = np.maximum(2, np.ceil(lg / 0.25).astype(int) + 1)      # un point tous les 0,25 mm
        rep_ = np.repeat(np.arange(len(a)), nb)
        t = np.concatenate([np.linspace(0, 1, k) for k in nb])[:, None]
        echant = a[rep_] * (1 - t) + b[rep_] * t
        dist, _ = cKDTree(echant).query(pts)
        loin = dist > garde_arete
        pts, nrm = pts[loin], nrm[loin]
    eps = 1e-3
    # Points extérieurs : un rayon vers l'extérieur ne retouche pas la pièce
    _, sortie, _ = m.ray.intersects_location(pts + nrm * eps, nrm, multiple_hits=False)
    exterieur = np.ones(len(pts), bool)
    exterieur[sortie] = False
    p, d = pts[exterieur], -nrm[exterieur]
    if len(p) == 0:
        return p, np.array([])
    loc, ray, _ = m.ray.intersects_location(p + d * eps, d, multiple_hits=False)
    ep = np.full(len(p), np.nan)
    ep[ray] = np.linalg.norm(loc - p[ray], axis=1)
    ok = ~np.isnan(ep)
    return p[ok], ep[ok]


def porte_a_faux(m, angle_max=45.5):
    """Faces orientées vers le bas au-delà de angle_max (hors plateau) et ponts horizontaux."""
    nz = m.face_normals[:, 2]
    cz = m.triangles_center[:, 2]
    hors_plateau = cz > 0.05
    lim = -np.sin(np.radians(angle_max))
    raide = hors_plateau & (nz < lim) & (nz > -0.985)
    plat = hors_plateau & (nz <= -0.985)
    zones = []
    if plat.any():
        sel = np.where(plat)[0]
        adj = m.face_adjacency
        garde = np.isin(adj[:, 0], sel) & np.isin(adj[:, 1], sel)
        groupes = trimesh.graph.connected_components(adj[garde], nodes=sel)
        for g in groupes:
            tri = m.triangles[g].reshape(-1, 3)
            dx, dy = np.ptp(tri[:, 0]), np.ptp(tri[:, 1])
            zones.append((max(dx, dy), min(dx, dy), m.area_faces[g].sum(), tri[:, 2].mean(),
                          tri[:, 0].mean(), tri[:, 1].mean()))
    zones.sort(reverse=True)
    raides = []
    if raide.any():
        sel = np.where(raide)[0]
        adj = m.face_adjacency
        garde = np.isin(adj[:, 0], sel) & np.isin(adj[:, 1], sel)
        for g in trimesh.graph.connected_components(adj[garde], nodes=sel):
            c = m.triangles_center[g].mean(axis=0)
            raides.append((m.area_faces[g].sum(), c[0], c[1], c[2]))
        raides.sort(reverse=True)
    return m.area_faces[raide].sum(), zones, raides


def verifier(pieces, ref, infos):
    import telephone_v0 as T
    r = Rapport()
    r("=" * 78)
    r("VÉRIFICATION PAMK PHONE V0")
    r("=" * 78)

    # ------------------------------------------------------------------ 1
    r("\n1. Solides (valide / fermé / nombre de solides / volume)")
    for nom, f in pieces.items():
        coques = f.Shells()
        fermees = all(BRep_Tool.IsClosed_s(s.wrapped) for s in coques)
        nb = len(f.Solids())
        attendu = nb == 1 or nom == "plaque_visage"
        r(f"  {nom:24s} valide={f.isValid()}  fermé={fermees}  solides={nb:2d}  volume={f.Volume():9.1f} mm³")
        if not (f.isValid() and fermees and attendu):
            r.alerte(f"{nom} : solide douteux")
    r("  STL exportés (sens d'impression) :")
    for nom in pieces:
        chemin = os.path.join(EXPORT, "stl", nom + ".stl")
        if os.path.exists(chemin):
            m = trimesh.load(chemin)
            r(f"    {nom + '.stl':30s} étanche={m.is_watertight}  triangles={len(m.faces):6d}  "
              f"z min={m.bounds[0][2]:.3f}  hauteur={m.extents[2]:.2f}")
            if not m.is_watertight:
                r.alerte(f"{nom}.stl non étanche")

    # ------------------------------------------------------------------ 2
    r("\n2. Interférences (volume d'intersection, doit être ~0)")
    imprimees = list(pieces.items())
    achetees = [(n, f) for n, f in ref.items()]
    couples = list(itertools.combinations(imprimees, 2)) + \
        [(p, a) for p in imprimees for a in achetees]
    # achat contre achat : batterie, interrupteurs, aimants contre la carte et ses composants
    mobiles = [x for x in achetees if x[0].startswith(("batterie", "inter_", "aimant"))]
    fixes = [x for x in achetees if x[0].startswith(("comp_", "pcb", "dalle", "verre", "pli"))]
    couples += [(m, f) for m in mobiles for f in fixes]
    couples += list(itertools.combinations(mobiles, 2))
    pire = 0.0
    nb = 0
    for (na, a), (nb_, b) in couples:
        v = volume_inter(a, b)
        nb += 1
        if not (v < 1e-3):
            r.alerte(f"interférence {na} / {nb_} : {v:.4f} mm³")
        pire = max(pire, v if v == v else 0)
    # batterie gonflée de BATT_GONFLEMENT contre la carte et ses composants
    bx, by = T.BATT_CENTRE
    gonflee = T.rect_arrondi(bx, by, T.BATT_W, T.BATT_L, 1.0, T.Z_BATT_BAS, T.Z_BATT_HAUT + T.BATT_GONFLEMENT - 1e-3)
    for n, f in fixes:
        v = volume_inter(gonflee, f)
        if v > 1e-3:
            r.alerte(f"batterie gonflée de {T.BATT_GONFLEMENT} mm touche {n} : {v:.3f} mm³")
        nb += 1
    r(f"  {nb} couples testés, plus grand volume commun : {pire:.5f} mm³")

    # ------------------------------------------------------------------ 3
    r("\n3. Jeux mesurés (distance mini entre surfaces) / jeu voulu")
    P, R = pieces, ref
    controles = [
        ("verre_tactile", "cadre_avant", T.EP_ADHESIF_VERRE, "adhésif sous la lèvre (0,15) ; 0,2 sur les côtés"),
        ("pcb", "coque", 0.2, "coins du PCB contre les pans coupés (les plots ont 0,7 de mousse)"),
        ("pcb", "cadre_avant", None, ""),
        ("dalle_epaper", "coque", None, ""),
        ("batterie", "coque", T.EP_MOUSSE_BATT, "mousse sous la batterie / 0,5 dans le berceau"),
        ("batterie", "comp_nappe_petits_composants", T.BATT_GONFLEMENT, "marge de gonflement"),
        ("comp_usb_c", "coque", 0.2, "prise USB-C / bords de l'encoche"),
        ("comp_bouton_rst", "coque", None, "poussoir RST / flanc"),
        ("comp_bouton_boot", "coque", None, ""),
        ("comp_lecteur_tf", "coque", None, ""),
        ("comp_connecteur_hy20", "coque", None, ""),
        ("comp_module_esp32s3", "coque", None, "antenne : rien que du plastique autour"),
        ("pli_nappe_dalle", "coque", None, "pli de la nappe de la dalle (haut)"),
        ("inter_volume_plus", "capuchon_volume_plus", T.JEU_BOUTON, "poussoir / capuchon au repos"),
        ("inter_volume_moins", "capuchon_volume_moins", T.JEU_BOUTON, ""),
        ("inter_marche", "capuchon_marche", T.JEU_BOUTON, ""),
        ("inter_volume_plus", "coque", 0.0, "posé dans son berceau"),
        ("capuchon_volume_plus", "coque", 0.0, "collerette en appui sur le siège"),
        ("cadre_avant", "coque", 0.0, "appui au plan de joint"),
        ("aimant_plaque_1", "coque", 0.1, "aimant de plaque / fond de son creux"),
        ("aimant_coque_1", "aimant_plaque_1", None, "écart entre aimants"),
    ]
    tous = {**P, **R}
    for a, b, voulu, note in controles:
        d = distance(tous[a], tous[b])
        txt = f"  {a:22s} <-> {b:30s} {d:6.3f} mm"
        if voulu is not None:
            txt += f"  (voulu {voulu:.2f})"
        r(txt + ("  " + note if note else ""))
        if d < -1e-6 or (voulu is not None and d + 1e-3 < voulu):
            r.alerte(f"jeu {a}/{b} = {d:.3f} < {voulu}")
    r(f"  plots d'appui : sommet z={infos['z_plot']:.2f}, dos du PCB z={T.Z_PCB_AR:.2f} -> "
      f"{T.Z_PCB_AR - infos['z_plot']:.2f} mm pour une mousse de {T.EP_MOUSSE_PLOT} mm")
    # USB-C : où bute l'embout surmoulé du câble (rayons tirés vers la prise sur toute sa surface)
    face_prise = T.COMPOSANTS["usb_c"][2]
    zu = T.Z_PCB_AR - T.USB_Z_SOUS_PCB
    m_ext = trimesh.util.concatenate([maillage(P["coque"]), maillage(P["cadre_avant"])])
    for (w, h) in ((12.35, 6.5), (11.0, 5.5)):
        X, Z = np.meshgrid(np.linspace(T.USB_X - w / 2 + 0.05, T.USB_X + w / 2 - 0.05, 50),
                           np.linspace(zu - h / 2 + 0.05, zu + h / 2 - 0.05, 30))
        rc = 1.0                                     # coins de l'embout arrondis à 1 mm (prudent)
        dx = np.maximum(np.abs(X - T.USB_X) - (w / 2 - rc), 0)
        dz = np.maximum(np.abs(Z - zu) - (h / 2 - rc), 0)
        dans = (dx ** 2 + dz ** 2) <= rc ** 2
        o = np.stack([X[dans], np.full(dans.sum(), face_prise - 20), Z[dans]], 1)
        loc, ri, _ = m_ext.ray.intersects_location(o, np.tile([0, 1.0, 0], (len(o), 1)), multiple_hits=False)
        y_hit = np.full(len(o), np.inf)
        y_hit[ri] = loc[:, 1]
        manque = max(0.0, face_prise - y_hit.min())
        r(f"  embout USB-C {w:.2f} x {h:.1f} (coins r 1) : bute à {manque:.2f} mm devant la prise -> la fiche entre de "
          f"{6.65 - manque:.2f} mm sur 6,65")
        if manque > 0.3:
            r.alerte(f"embout USB-C {w} x {h} : la fiche n'entre que de {6.65 - manque:.2f} mm")
    # Montage des capuchons par l'intérieur : ils doivent pouvoir reculer de toute la longueur
    # de leur corps sans toucher la coque (sinon on ne peut pas les mettre en place)
    sens = {"volume_plus": (-1, 0, 0), "volume_moins": (1, 0, 0), "marche": (0, -1, 0)}
    for nom, v in sens.items():
        cap = P["capuchon_" + nom]
        bb = cap.BoundingBox()
        bout = {"volume_plus": bb.xmax, "volume_moins": -bb.xmin, "marche": bb.ymax}[nom]
        course = bout - infos["bouton_" + nom]["u_siege"] + 0.1
        pire, ou = 0.0, None
        for d in np.linspace(0.25, course, int(course / 0.25) + 1):
            vi = volume_inter(cap.translate(cq.Vector(v[0] * d, v[1] * d, v[2] * d)), P["coque"])
            if vi > pire:
                pire, ou = vi, d
        r(f"  montage capuchon {nom:13s}: recul de {course:.2f} mm par l'intérieur -> "
          + ("passe sans toucher" if pire < 1e-3 else f"BLOQUÉ à {ou:.2f} mm"))
        if pire >= 1e-3:
            r.alerte(f"capuchon {nom} impossible à monter (touche la coque après {ou:.2f} mm de recul)")
    # Course utile des boutons
    for nom in ("volume_plus", "volume_moins", "marche"):
        i = infos["bouton_" + nom]
        saillie_repos = T.CAP_SAILLIE
        r(f"  bouton {nom:13s}: jeu {T.JEU_BOUTON:.2f} + course {T.SW_COURSE:.2f} -> le capuchon dépasse encore "
          f"de {saillie_repos - T.JEU_BOUTON - T.SW_COURSE:.2f} mm enfoncé à fond")

    # ------------------------------------------------------------------ 4
    r("\n4. Épaisseur sous la peau extérieure (rayons ; cible 1,6, mini 1,2)")
    for nom, lat in (("coque", True), ("cadre_avant", True), ("plaque_aimantee", False)):
        p, e = epaisseur_peau(P[nom], lateral=lat)
        if len(e) == 0:
            r(f"  {nom:16s} aucun point mesurable")
            continue
        if not lat:
            r(f"  (plaque : mesure à travers les faces ; 0,6 = peau voulue au-dessus des aimants)")
        q = np.percentile(e, [0.5, 5, 50])
        r(f"  {nom:16s} {len(e)} points  mini={e.min():.2f}  0,5 %={q[0]:.2f}  5 %={q[1]:.2f}  médiane={q[2]:.2f}")
        minces = p[e < 1.15]
        if len(minces):
            # regroupe les points minces par zone (grille de 6 mm)
            cles = {}
            for pt, ep in zip(minces, e[e < 1.15]):
                k = tuple(np.round(pt / 6).astype(int))
                cles.setdefault(k, []).append((ep, pt))
            r(f"    zones sous 1,15 mm : {len(cles)}")
            for k, lst in sorted(cles.items(), key=lambda kv: min(x[0] for x in kv[1]))[:12]:
                ep, pt = min(lst, key=lambda x: x[0])
                r(f"      {ep:.2f} mm vers X={pt[0]:6.1f} Y={pt[1]:6.1f} Z={pt[2]:5.1f} ({len(lst)} pts)")

    # ------------------------------------------------------------------ 5
    r("\n5. Impression sans support (STL dans le sens d'impression)")
    for nom in pieces:
        chemin = os.path.join(EXPORT, "stl", nom + ".stl")
        m = trimesh.load(chemin)
        aire_raide, zones, raides = porte_a_faux(m)
        r(f"  {nom:24s} ({T.ORIENTATION.get(nom, '')})")
        r(f"    porte-à-faux > 45° : {aire_raide:.2f} mm² au total, plus grande zone "
          f"{raides[0][0] if raides else 0:.2f} mm²")
        for a_, x, y, z in raides[:6]:
            r(f"      {a_:5.2f} mm² vers ({x:6.1f}, {y:6.1f}, z={z:5.2f})")
        if raides and raides[0][0] > 2.0:
            r.alerte(f"{nom} : zone en porte-à-faux > 45° de {raides[0][0]:.1f} mm²")
        grands = [z for z in zones if z[0] > 2.0]
        for L, l, aire, z, x, y in grands[:8]:
            r(f"    pont/plafond {L:5.1f} x {l:4.1f} mm (aire {aire:6.1f}) à z={z:5.2f}  (x={x:6.1f}, y={y:6.1f})")
        if len(grands) > 8:
            r(f"    ... {len(grands) - 8} autres plus petits")

    # ------------------------------------------------------------------ 6
    r("\n6. Encombrements (repère assemblage, d'après le maillage fin)")
    mins, maxs = [], []
    for nom, f in pieces.items():
        b = maillage(f, 0.01).bounds
        mins.append(b[0])
        maxs.append(b[1])
        r(f"  {nom:24s} X[{b[0][0]:7.2f},{b[1][0]:7.2f}] Y[{b[0][1]:7.2f},{b[1][1]:7.2f}] Z[{b[0][2]:6.2f},{b[1][2]:6.2f}]")
    lo, hi = np.min(mins, axis=0), np.max(maxs, axis=0)
    r(f"  TÉLÉPHONE COMPLET : {hi[0] - lo[0]:.2f} x {hi[1] - lo[1]:.2f} x {hi[2] - lo[2]:.2f} mm "
      f"(largeur x longueur x épaisseur, plaque et visage compris)")

    r("\nBILAN : " + ("aucune alerte" if not r.alertes else f"{len(r.alertes)} alerte(s) :"))
    for a in r.alertes:
        r("  - " + a)
    os.makedirs(EXPORT, exist_ok=True)
    with open(os.path.join(EXPORT, "verification.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(r.lignes) + "\n")
    return r.alertes


if __name__ == "__main__":
    sys.path.insert(0, ICI)
    sys.stdout.reconfigure(encoding="utf-8")
    import telephone_v0 as t
    p, rf, i = t.construire()
    verifier(p, rf, i)
