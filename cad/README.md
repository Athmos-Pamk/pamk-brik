# Pamk Brik V0 : boîtier imprimable

Boîtier style « Lingot » recalculé autour des pièces commandées : LilyGO T5 4.7" S3 avec
verre tactile (H716), batterie LiPo 103450 de 10 mm, 3 boutons CMS 3 × 6 × 4,3.
Tout est paramétrique dans `telephone_v0.py` (cotes en tête du fichier, commentées).

**Encombrement : 71,1 × 125,1 × 21,3 mm**, et 23,2 mm d'épaisseur avec la plaque aimantée et
le visage. Face avant : 67,1 × 121,1 mm. Dos : 60,6 × 114,6 mm (12° de dépouille).

Pourquoi 21 mm et pas les 13 mm du visuel : la batterie de 10 mm est derrière la carte.
Empilage de l'arrière vers l'avant :

| Couche | mm | Cumul (Z) |
|---|---|---|
| Fond de coque | 1,6 | 1,6 |
| Mousse double face sous la batterie | 0,5 | 2,1 |
| Batterie 103450 | 10,0 | 12,1 |
| Marge de gonflement | 0,8 | 12,9 |
| Composants au dos de la carte au-dessus de la batterie (SOIC 1,75) | 2,0 | 14,9 |
| PCB | 1,2 | 16,1 |
| Dalle e-paper + adhésifs | 1,98 | 18,08 |
| Verre tactile | 1,1 | 19,18 |
| Adhésif sous la lèvre | 0,15 | 19,33 |
| Lèvre du cadre (= chanfrein avant 2 mm à 45°) | 2,0 | **21,33** |

Une batterie plus fine fait gagner directement son épaisseur (paramètre `BATT_EP`).

Aperçus : `export/apercus/` (3/4 avant, 3/4 dos, dessous, flanc droit, éclaté, coupes,
détail d'un bouton).

## Les pièces

| Pièce | Fichier STL | Sens d'impression | Couleur (visuel) |
|---|---|---|---|
| Cadre avant | `cadre_avant.stl` | face avant sur le plateau | noir |
| Coque | `coque.stl` | dos sur le plateau | olive |
| Plaque aimantée | `plaque_aimantee.stl` | face intérieure sur le plateau | noir |
| Visage pixel | `plaque_visage.stl` | avec la plaque, 2e filament | ambre |
| Capuchon volume + | `capuchon_volume_plus.stl` | collerette sur le plateau | olive |
| Capuchon volume − | `capuchon_volume_moins.stl` | collerette sur le plateau | olive |
| Capuchon marche | `capuchon_marche.stl` | collerette sur le plateau | olive |

Les STL sont déjà tournés dans leur sens d'impression et posés à z = 0. Les STEP
(`export/step/`, un par pièce) sont dans le repère du téléphone : ils s'assemblent tout seuls
dans Inventor. `export/pamk_phone_v0_assemblage.step` contient tout, plus les pièces achetées
(verre, dalle, carte et ses composants, batterie, interrupteurs, aimants) pour vérifier.

Plaque + visage à l'AMS : importer les deux STL ensemble dans Bambu Studio et répondre
**oui** à « charger comme un seul objet à plusieurs pièces », puis mettre l'ambre sur
`plaque_visage`. Les cases du visage n'ont que les 0,5 dernier mm : le changement de filament
ne se fait qu'en haut.

Les berceaux des 3 boutons et le berceau de batterie font partie de la coque (imprimés avec
elle). Avec une course de bouton de 0,25 mm, la chaîne capuchon → interrupteur doit être
précise au dixième : un berceau séparé, posé dans un logement, ajouterait son jeu.
Le berceau de bouton se réduit à un **dos** (la référence précise) avec un rebord de 0,4 mm :
le capuchon se monte par l'intérieur et doit pouvoir reculer de 3,9 mm derrière son trou,
donc rien ne dépasse dans son passage. (Relecture du 27/10 : la première version avait des
nervures et un socle qui bloquaient le capuchon après 1 mm de recul : impossible à monter.
La vérification teste maintenant ce montage.)

## Réglages d'impression (Bambu Studio)

- PLA, buse 0,4, couches de 0,2 mm.
- Parois : 4 boucles (1,6 mm). Dessus / dessous : 5 couches. Remplissage 15 % gyroïde.
- **Aucun support.** Pas de bordure nécessaire.
- Garder la compensation de patte d'éléphant (0,15 mm, valeur des profils Bambu) : les
  logements d'aimants de la plaque et les creux d'aimants au dos de la coque touchent le
  plateau, sans elle ils rétrécissent et l'aimant n'entre plus.
- Coque et cadre : couture « alignée », à placer sur l'arrière ou dans un pan coupé.
- Cadre : plateau PEI texturé conseillé, la face avant prend le grain du plateau.
- Capuchons : petites pièces, en imprimer 2 ou 3 jeux d'un coup (on choisit le meilleur).
  Couches de 0,12 si tu veux des bords plus nets.

Ce que la vérification a mesuré dans les STL (détail dans `export/verification.txt`) :
- porte-à-faux au-delà de 45° : seulement les petits arrondis (rayon 0,95) en haut des trous
  de bouton, 1,8 mm² chacun ;
- ponts : 10,0 mm au plafond de la fente TF, 8,3 mm au-dessus des capuchons, 6,2 mm sur les
  logements d'aimants, le reste en dessous ; l'ouverture USB-C est ouverte vers le joint
  (coque) et vers le haut de l'impression (cadre), sans pont.

## À acheter en plus

| Quoi | Combien | Pour |
|---|---|---|
| Aimants néodyme disque 6 × 2 mm (N35 ou plus) | 8 | 4 dans la coque, 4 dans la plaque |
| Adhésif double face mousse 0,5 mm | ≈ 34 × 50 mm | coller la batterie au fond |
| Mousse EVA adhésive 1 mm | 7 pastilles Ø 5 mm | sur les plots qui appuient sur la carte |
| Adhésif double face fin 0,15 mm (type 3M 9448A, en bande de 2–3 mm, vendu pour les écrans de téléphone) | ≈ 40 cm | optionnel : coller le verre sous la lèvre du cadre |
| Fil fin silicone 30 AWG | 6 brins de 8 cm | relier les 3 boutons à la carte |
| Colle cyanoacrylate + un peu de colle chaude | | aimants, blocage des boutons |

Aucune vis, aucun insert.

## Comment ça tient (et pourquoi pas de vis)

Le verre tactile (≈ 121 × 67 mm) occupe tout l'intérieur du cadre, et la dalle est collée sur
les trous de fixation de la carte. Une colonne de vis n'a donc pas de place, sauf en
élargissant ou rallongeant le téléphone de 4 à 9 mm. On fait donc comme une liseuse
(Kindle, Kobo) :

- **Cadre clipsé** : 8 languettes de 16 mm sur les flancs de la coque (4 par côté) montent
  dans la jupe du cadre ; un bourrelet de 0,35 mm entre dans une gorge de 0,45 mm.
  Aux deux bouts, le verre ne dépasse la carte que de 1,47 mm : pas de languette.
- **Le verre** est dans une poche du cadre (0,2 mm de jeu par côté) sous une lèvre de 2 mm.
  La fenêtre fait la zone active + 0,8 mm par côté : le cadre n'appuie que sur la bordure
  imprimée du verre, jamais sur la zone tactile.
- **La carte** est poussée vers l'avant par 7 plots (Ø 4,5) avec une pastille de mousse,
  placés sur les trous de fixation de la carte, là où il n'y a pas de composant.
- **Ouvrir** : un médiator dans le petit V du joint, sur un flanc, et faire levier.

## Assemblage

1. **Aimants.** Coller les aimants deux par deux pour trouver le sens qui attire. Mettre
   4 aimants dans les logements de la coque (depuis l'intérieur) avec une goutte de cyano,
   tous dans le même sens. Mettre les 4 autres dans la plaque, face qui dépasse vers la
   coque (ils dépassent de 1,2 mm et rentrent dans les creux du dos : c'est ce qui centre
   la plaque). Les aimants ne sont pas symétriques : la plaque ne se pose que visage à
   l'endroit.
2. **Boutons.** Souder 2 fils sur chaque interrupteur (fils vers le côté ou vers le haut,
   rien derrière le corps). Poser chaque capuchon dans son berceau vide, collerette côté
   intérieur, et le pousser dans son trou (la collerette reste dedans). Poser l'interrupteur
   dos contre la paroi du berceau, sur le petit rebord, poussoir au milieu du capuchon, et le
   fixer par une goutte de colle (colle chaude basse température ou cyano gel) sur ses côtés
   et son dessus, **jamais entre son dos et la paroi** (ça mangerait le jeu de 0,1).
   Essayer chaque bouton avant de fermer : si le clic ne vient pas avant que le capuchon soit
   au ras du flanc, glisser un morceau d'adhésif (≈ 0,1 mm) derrière l'interrupteur ; s'il
   clique tout seul, poncer un peu l'arrière du capuchon.
3. **Batterie.** Mousse double face au fond du berceau, batterie dedans, fils côté USB-C
   (encoche dans la paroi du berceau).
4. **Pastilles de mousse** sur les 7 plots.
5. **Carte.** Souder les fils des boutons sur les pastilles du connecteur 40 broches
   (GPIO à choisir avec le firmware), brancher la batterie sur la carte.
6. **Cadre.** Optionnel : bandes d'adhésif fin sous la lèvre. Poser le cadre face avant sur
   la table, y déposer la carte verre vers le bas. La grande bordure du verre va en haut
   (côté opposé à l'USB-C).
7. **Fermer.** Descendre la coque sur le tout, l'encoche USB-C en face de la prise, et serrer
   le long des flancs jusqu'au clic des 8 languettes.
8. **Plaque** : elle se pose sur le dos et les aimants la tiennent.

Sur la coque : volume + à droite et volume − à gauche quand on regarde l'écran, marche au
milieu du dessus. Sur le flanc droit : fente de la carte TF (on la pousse avec un ongle ou un
cure-dent) et deux trous d'épingle pour BOOT et RST. L'USB-C est décalé de 7,8 mm à droite du
centre : c'est sa place sur la carte. Fentes écouteur (haut) et micro (bas) dans les
chanfreins avant : borgnes en V0 (`FENTES_AUDIO_TRAVERSANTES = True` pour la V1).

## Régénérer

```powershell
cd A:\bernon\Robotique\Pamk_Phone\cad ; .\.venv\Scripts\python.exe telephone_v0.py
```

Ça reconstruit les pièces, réécrit `export/` (STEP, STL, `cotes.txt`) puis lance la
vérification (`export/verification.txt`) et les aperçus. Environ 1 minute. Options :
`--sans-verif`, `--sans-apercus`.

Si le dossier `.venv` disparaît :

```powershell
cd A:\bernon\Robotique\Pamk_Phone\cad ; python -m venv .venv ; .\.venv\Scripts\python.exe -m pip install cadquery ezdxf matplotlib trimesh rtree scipy shapely networkx pymupdf
```

(Versions testées : CadQuery 2.8.0, trimesh 5.1.1, matplotlib 3.11.2, ezdxf 1.4.4, Python 3.11.)

## À mesurer au pied à coulisse à la réception

Chaque ligne correspond à un paramètre en tête de `telephone_v0.py`. On corrige la valeur,
on relance la commande, et la vérification dit tout de suite si quelque chose touche.

| # | Quoi mesurer | Paramètre | Valeur actuelle |
|---|---|---|---|
| 1 | Verre : longueur, largeur, rayon des coins | `VERRE_L`, `VERRE_W`, `VERRE_R` | 121,0 × 67,0, r 3,0 |
| 2 | Verre : épaisseur | `VERRE_EP` | 1,1 |
| 3 | Pile complète : face avant du verre → dos du PCB | `VERRE_EP + EP_DALLE + PCB_EP` | 4,28 |
| 4 | Épaisseur du PCB seul (sur un bord) | `PCB_EP` | 1,2 |
| 5 | Débord du verre sur le PCB, aux 4 côtés (centrage) | `VERRE_DECALAGE_Y` | 0 (centré) |
| 6 | Bord haut du verre → première ligne de pixels (grande bordure) | `AA_BORD_HAUT` | 12,8 |
| 7 | Hauteur de l'axe de l'USB-C sous le dos du PCB | `USB_Z_SOUS_PCB` | 1,6 |
| 8 | Dépassement de la prise USB-C au-delà du bord du PCB | `COMPOSANTS["usb_c"]` | 1,05 |
| 9 | Plus haut composant dans la zone de la batterie (sous le logo LILYGO, vers le bout opposé à l'USB) | `H_COMP_SOUS_BATT` | 2,0 |
| 10 | Hauteur de la prise batterie, de la fiche branchée et de ses fils | `COMPOSANTS["prise_batterie"]`, `["fiche_batterie"]` | 4,9 |
| 11 | Hauteur du connecteur blanc 4 broches (HY2.0) | `COMPOSANTS["connecteur_hy20"]` | 5,0 |
| 12 | Lecteur TF : position du centre de la carte, hauteur sous le PCB | `TF_Y`, `TF_Z_SOUS_PCB` | 1,93 / 0,95 |
| 13 | Boutons RST et BOOT : position, hauteur du poussoir, dépassement du bord | `BOUTONS_CARTE`, `BOUTONS_CARTE_Z_SOUS_PCB` | DXF / 0,95 |
| 14 | Trous de fixation de la carte (les plots doivent tomber dessus, pas sur un composant) | `TROUS_PCB` | trous du boîtier LilyGO style3 + photo, ± 0,4 |
| 15 | Pli de la nappe de la dalle (bout opposé à l'USB) et de la nappe du tactile : dépassement au-delà du PCB | `JEU_PLI_NAPPE` | dégagement 0,4 + 0,9 |
| 16 | Batterie : longueur avec le circuit de protection, largeur, épaisseur, côté de sortie des fils | `BATT_L`, `BATT_W`, `BATT_EP` | 50 × 34 × 10 |
| 17 | Prise batterie de la carte : pas de 2,0 mm (JST PH, comme la batterie) ou 1,25 mm ? | — | 2,0 probable |
| 18 | Interrupteurs : corps, hauteur totale, poussoir | `SW_L`, `SW_W`, `SW_H`, `SW_ACT` | 6,0 × 3,5 × 4,3 |
| 19 | Aimants reçus : diamètre et épaisseur | `AIMANT_D`, `AIMANT_H` | 6 × 2 |

Avant d'imprimer la coque entière : imprimer d'abord le cadre seul (≈ 1 h) et y poser la
carte. Ça valide d'un coup les points 1, 2, 5 et 6. Puis, dans Bambu Studio, couper la coque
à z = 12 (outil Couper, garder le haut) et imprimer ce haut : on essaie les 8 clips avec le
cadre, l'ouverture USB-C avec un vrai câble et la fente TF, sans attendre la coque entière.

## Point à surveiller : les clips du cadre

Les languettes (1,2 mm d'épaisseur, 1,8 mm de haut, bourrelet à 0,9 mm du pied) sont trop
courtes pour plier elles-mêmes : les 0,25 mm de serrage viennent de la flexion des flancs de
la coque et des grands côtés du cadre. Au milieu des flancs (Y = ±16) ça plie ; près des
coins (Y = ±48) c'est raide : montage dur, et risque de fendre la jupe du cadre au fond de
la gorge (1,05 mm, tirée entre les couches). Si l'essai ci-dessus est trop dur : `PERLE` à
0,25 (serrage 0,15), ou languettes à ±40 au lieu de ±48 (`LANGUETTES`).

## Choix à revoir si tu veux

- **Coins coupés de 3 mm** au lieu de 8 dans le visuel : les coins du verre (rayon ≈ 3)
  arrivent dans les pans coupés. Des coins de 8 demanderaient un téléphone plus grand
  de ≈ 5 mm (`K_COIN`).
- **Dépouille de 12°** comme demandé : sur 18 mm de flanc ça fait 3,8 mm par côté, d'où un
  dos bien plus étroit que l'avant. `DEPOUILLE_DEG` à 7° donnerait les 2 mm par côté du
  visuel.
- **USB-C** : ouverture de 12,6 × 6,8 mm (encoche dans la coque + entaille de 0,7 mm dans
  le bas du cadre) pour que l'embout surmoulé du câble vienne contre la prise. Avec
  l'ancienne encoche de 10,8 × 5, un embout courant (11–12,35 × 5,5–6,5) butait à 1,7–1,9 mm
  de la prise (flanc en dépouille + bas du cadre) : la fiche n'entrait que de 4,8–5,0 mm sur
  6,65, sans verrouillage. On voit un peu plus le bord de la carte par l'ouverture.
- **Boutons** : capuchons de 9,5 × 3,4 mm (14 × 3,4 dans le visuel) pour garder des ponts
  de moins de 10 mm à l'impression. Ils dépassent de 0,6 mm, et encore de 0,25 mm enfoncés
  à fond.
