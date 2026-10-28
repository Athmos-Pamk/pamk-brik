# Pamk Phone V0 : données mécaniques des pièces achetées

Fiche de référence pour la 3D (`cad/telephone_v0.py`). Pour chaque cote : d'où elle vient, et
si elle est **vérifiée** (lue dans un document officiel ou un fichier CAO du fabricant) ou
**estimée** (à mesurer à la réception, voir la liste dans `cad/README.md`).
Les fichiers téléchargés sont dans `cad/ref/`.

## Repère utilisé partout

- X = largeur (vers la droite quand on regarde l'écran), Y = longueur (haut = +Y), Z = épaisseur
  (écran vers +Z). Origine X/Y au centre du PCB.
- Le bout **bas** (Y < 0) porte l'USB-C. Le bout **haut** porte la nappe de la dalle et le bouton
  marche.
- Passage depuis le DXF LilyGO (vu côté écran, Y du DXF de 0 côté USB à −118,07) :
  **X = 31,535 − x_dxf ; Y = −y_dxf − 59,035**.

## Carte LilyGO T5 4.7" ESP32-S3 (H716 : tactile, connecteur non soudé)

| Donnée | Valeur | Statut | Source |
|---|---|---|---|
| Contour du PCB | 63,07 × 118,07 mm | vérifié | DXF LilyGO `shell/EPD47-S3.dxf`, calque KeepOut |
| Encoche du PCB sous l'antenne | X −31,5 → −24,3 ; Y −50,7 → −30,7 (7,25 × 20 mm) | vérifié | même DXF |
| Épaisseur du PCB | 1,2 mm | estimé | STEP LilyGO `shell/style2/full.stp` (carte de la génération précédente : 1,20) |
| Version | annonce « V2.3 » ; photos de production : sérigraphie « Screen-4.7-S3 V2.4 2024-01-15 » | à voir | page produit + wiki (V2.4 = version courante) |
| Module ESP32-S3-WROOM-1-N16R8 | 18,0 × 25,5 × 3,1 mm, X −29,9 → −4,1, Y −50,7 → −31,0, antenne au-dessus de l'encoche (flanc gauche) | vérifié | datasheet Espressif v1.8 + DXF |
| USB-C | centre X = +7,80, bouche à Y = −60,09 (dépasse de 1,05 du PCB), prise posée sur le dos du PCB ; ouverture du boîtier 12,6 × 6,8 pour l'embout du câble (12,35 × 6,5 au plus) | vérifié (X, Y) / estimé (axe à 1,6 sous le PCB) | DXF, empreinte `TYPE_C3_0` à pattes traversantes |
| Prise batterie | X −0,4 → 8,8 ; Y −28,5 → −20,3, entrée côté +X, pas de 2,0 mm | vérifié (position) / probable (pas) | DXF : empreinte `CONN-SMD_2P-H-P2.00` |
| Lecteur TF | X 15,8 → 31,0 ; Y −5,5 → 9,4 (carte au centre Y ≈ 1,9), entrée par le flanc droit | vérifié (empreinte) / estimé (hauteur 1,85) | DXF `TF_CKT01_009D` |
| Poussoirs latéraux sur le bord droit | RST Y −28,2 ; BOOT (IO0) Y −18,9 ; IO21 Y −10,5 | vérifié | DXF (`TS_019B` S5/S6/S4) + schéma |
| Connecteur nappe tactile 6 broches (I2C, GT911) | X 21,0 → 28,7 ; Y −42,8 → −36,6 | vérifié | DXF |
| Connecteur nappe de la dalle (P1, Panasonic AXE544127) | X 12,8 → 23,6 ; Y 39,4 → 51,9 | vérifié | DXF |
| Connecteurs 4 broches pas de 2,0 (P4, P5) | X 25,3 → 30,6 ; Y 11,7 → 20,7 et 22,5 → 31,5 | vérifié (V2.3) | DXF ; sur V2.4 : un seul HY2.0 côté P5, P4 non peuplé (photos LilyGO) |
| SOIC-16 (74HCT4094) | X −18,5 → −8,3 ; Y 37,5 → 41,4, 1,75 mm | vérifié (position) | DXF |
| Pile RTC MS412FE | X 11,1 → 17,1 ; Y −51,3 → −46,0 | vérifié | DXF |
| Connecteur 40 broches | pastilles X −30,1 → −25,8 ; Y −17,0 → 33,0, rien ne dépasse (option non soudée) | vérifié | DXF + option H716 |
| Trous de fixation | (27,05 ; −54,06), (19,05 ; −54,06), (−27,05 ; −54,06), (27,05 ; 47,04), (27,05 ; 54,04), (−16,55 ; 47,04), (−16,55 ; 54,04) | vérifié ± 0,4 | les 7 trous de la plaque du boîtier LilyGO « style3 » (`shell/style3/a.stl`, coupe à y = −2,5 ; X = z_stl, Y = −x_stl), recoupés sur la photo du dos V2.4 ; absents du DXF. (Avant le 27/10 : 46 au lieu de 47,04 pour deux d'entre eux.) |
| Vis LilyGO | M2 × 4 pour le PCB, M1,5 × 5 pour leurs boîtiers | vérifié | `shell/README.MD` |
| Boutons de la carte | BOOT = IO0, RST = EN, bouton utilisateur = IO21 | vérifié | schémas V2.3 et V2.4, image pinmap LilyGO |

Les trous de fixation sont recouverts par la dalle collée côté écran : on ne peut pas y visser.
Le boîtier s'appuie seulement autour d'eux, côté composants.

## Écran e-paper ED047TC1 (datasheet E Ink P-511-710 V1)

| Donnée | Valeur | Statut |
|---|---|---|
| Contour (dalle + film de protection) | 62,5 × 113,9 × 0,872 mm | vérifié |
| Verre TFT | 60,1 × 112,7 mm, 0,5 mm | vérifié |
| Zone active | **58,32 × 103,68 mm** (540 × 960 px au pas de 0,108) | vérifié |
| Zone active dans la dalle | centrée en largeur (0,89 de chaque côté du verre TFT) ; en longueur : 8,12 mm du bord côté nappe, 2,1 mm de l'autre | vérifié (lu sur le plan) |
| Nappe | sort d'un petit côté, se replie (zone pliable 3 mm), queue de 30 mm vers le connecteur Panasonic | vérifié |
| Épaisseur dalle + adhésifs sur la carte | 1,98 mm | estimé (STEP LilyGO `full.stp`, pièce « PM2 ») |

## Verre tactile (option « With Touch Glass »)

| Donnée | Valeur retenue | Statut | Sources |
|---|---|---|---|
| Contour | 121 × 67 mm | estimé, conservateur | wiki LilyGO « Dimensions 121x67x12mm » ; boîtier LilyGO style3 : 120,2 × 66,5 ; rapport largeur/longueur de la photo produit : 1,81 (121 × 67 → 1,806) ; l'annonce « 119 × 64 » ne colle pas avec ces trois sources |
| Rayon des coins | 3,0 mm | estimé | contour du boîtier LilyGO style3 (3,0 mesuré dans le STL) |
| Épaisseur | 1,1 mm | estimé | valeur courante d'un verre 2.5D, rien de publié |
| Fenêtre imprimée | = zone active : bordure ≈ 12,8 mm en haut, ≈ 4,5 en bas, ≈ 4,3 sur les côtés | estimé | mesuré sur la photo produit (fenêtre 104 × 58,3 mm à l'échelle, cohérent avec la zone active) |
| Position sur le PCB | centré | estimé | trous de la carte à 5 mm de chaque bout, symétriques dans le boîtier LilyGO dont le contour = le verre |
| Tactile | GT911, 2 points, I2C SDA IO18 / SCL IO17, INT IO47 | vérifié | wiki + pinmap LilyGO |

La grande bordure est du côté de la nappe de la dalle (bout opposé à l'USB-C), car c'est là que
la dalle a sa grande marge (8,12 mm). Conséquence pour le boîtier : la zone active est
décalée de 4,14 mm vers le bas par rapport au centre du verre.

## Batterie LiPo 103450

| Donnée | Valeur | Statut | Source |
|---|---|---|---|
| Dimensions | 50 × 34 × 10 mm | annonce vendeur, à mesurer | commande du 08/10 (`docs/SPEC_V0.md`) |
| Prise | JST PH 2,0 mm, 2 broches | annonce vendeur | idem |
| Place dans le boîtier | centrée en X, Y −8,5 → 41,5, sur 0,5 mm de mousse, berceau 35 × 51 (0,5 de jeu par côté), 0,8 de marge de gonflement au-dessus | choix | sous les composants les plus bas de la carte (SOIC 1,75, nappe, lecteur TF 1,85) |

**Prise de la carte : à contrôler.** L'empreinte du DXF (`P2.00`), l'image pinmap LilyGO
(« Li-Po Battery JST-PH 2.0mm ») et le câble fourni (« JST 2Pin 2.0mm ») disent pas de 2,0, donc
la batterie se branche directement. Mais le symbole du schéma s'appelle « 2P-1.27MM » et le
wiki écrit « JST-GH 2.0 mm » (le GH est au pas de 1,25). Vérifier que la fiche de la batterie
entre avant de tout fermer.

## Boutons CMS 3 × 6 × 4,3 (appui sur le dessus)

Plan de référence : HYP (Hon Yuan) 1TS002B, la famille « 3X6 » vendue sous ce nom.

| Donnée | Valeur | Statut |
|---|---|---|
| Corps | 6,0 ± 0,2 × 3,5 ± 0,2 mm, 3,5 de haut | vérifié (plan HYP) |
| Hauteur totale avec poussoir | 4,3 ± 0,2 mm | vérifié |
| Poussoir | 3,0 × 1,5 mm, dépasse de 0,8 | vérifié |
| Pattes | 8,0 ± 0,2 hors tout, 1,0 de large | vérifié |
| Course | 0,25 ± 0,1 mm | vérifié |
| Force | 180 ± 30 gf (variante 1800) | vérifié |

Les boutons achetés sont un lot générique : le plan HYP est la référence la plus proche,
à confirmer en mesurant un bouton.

## Autres choix liés à l'électronique

- Antenne de l'ESP32 : flanc gauche, partie basse (Y −50,7 → −30,7). Rien de métallique à
  moins de ≈ 12 mm : pas de vis, pas d'insert ; l'aimant le plus proche est à (−23,5 ; −15).
- Aimants de la plaque : 4 × Ø 6 × 2, placés en trapèze (détrompeur : la plaque ne se pose
  que visage à l'endroit).
- GPIO libres d'après le wiki LilyGO : IO45, IO10, IO48, IO39 (le choix pour les 3 boutons
  revient au firmware ; IO45 est une broche de démarrage).

## Sources

- Dépôt LilyGO, branche esp32s3 : https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3
  - DXF de la carte : `shell/EPD47-S3.dxf`
  - Boîtiers LilyGO : `shell/README.MD`, `shell/style2/full.stp`, `shell/style2/Shell_STP.stp`,
    `shell/style3/a.stl`, `b.stl`, `c.stl` (style3 = version tactile). Attention : style2 est
    fait pour l'ancienne carte non tactile (PCB 63,1 × 114,1, 5 boutons) : il ne sert pas de
    preuve pour cette carte, sauf l'ordre de grandeur des épaisseurs. Seul style3 colle (trous,
    boutons, USB-C).
  - Schémas : `schematic/T5-ePaper-S3-V2.3.pdf`, `schematic/Screen-4.7-S3-V2.4 24-12-03.pdf`
  - Datasheet dalle : `datasheet/ED047TC1.pdf`
  - Photo dos V2.3 avec BOOT/RST : `images/product.jpg`
- Page produit : https://lilygo.cc/products/t5-4-7-inch-e-paper-v2-3 (variantes : Touch,
  Non-Soldered Female pin [H716]) et ses images :
  - https://cdn.shopify.com/s/files/1/0617/7190/7253/files/LILYGO-T5-E-PAPER_1.jpg (verre + dos V2.4)
  - https://cdn.shopify.com/s/files/1/0617/7190/7253/files/T5-S3V4.7_1.jpg (pinmap V2.4)
  - https://cdn.shopify.com/s/files/1/0617/7190/7253/files/T5-S3V4_3.jpg (121 × 67 × 12)
  - https://cdn.shopify.com/s/files/1/0617/7190/7253/files/LILYGO-T5-E-PAPER_2.jpg (contenu du colis)
- Wiki LilyGO : https://wiki.lilygo.cc/products/t5-series/t5-e-paper/
- Espressif, ESP32-S3-WROOM-1 datasheet v1.8 :
  https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- Bouton HYP 1TS002B (LCSC C255787) : https://www.lcsc.com/product-detail/C255787.html et le plan
  https://storage.googleapis.com/graviton-electric-symbols/document_assets/lcsc/2408011734_HYP--Hongyuan-Precision-1TS002B-1800-4300-CT_C255787.pdf

Copies locales : `cad/ref/lilygo_github/`, `cad/ref/lilygo_site/`, `cad/ref/espressif/`,
`cad/ref/bouton_3x6x4.3/`.
