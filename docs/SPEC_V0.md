# Pamk Phone — V0 : ce qui est décidé

Projet perso de Thomas, téléphone minimaliste de l'écosystème Pamk. Cette fiche est la base
commune de la 3D (`cad/`) et du code (`firmware/`).

## Pièces commandées (le 08/10/2026, livraison attendue mi-octobre)

| Pièce | Référence | Ce qu'on sait (annonce du vendeur, à mesurer à réception) |
|---|---|---|
| Carte écran | LILYGO T5 4.7" E-paper **V2.3 ESP32-S3**, option **« With Touch Glass »**, **« No Solder Pin »** (code LilyGO **H716**) | 119 × 64 mm, e-paper 4,7" 960 × 540, tactile, ESP32-S3, recharge batterie par USB-C, lecteur TF |
| Batterie | LiPo **103450**, 3,7 V, 2000 mAh, prise **JST PH 2.0** 2 broches | 50 × 34 × **10** mm |
| Boutons | Bouton tactile CMS **3 × 6 × 4,3 mm** (appui sur le dessus), lot de 100 | on en utilise 3 : marche/veille, volume +, volume − |

## Ce que fait la V0

- **Pas de carte SIM, pas d'appels, pas de son** (micro + ampli plus compacts en V1).
- Thomas veut **un maximum de fonctions** dans la V0.
- **Pour l'instant (28/10), pas de boutons montés** : tout se fait au tactile, la veille se
  réveille au toucher (`BOUTONS_PRESENTS = false` dans `firmware/PamkPhone/config.h`). Il les
  ajoutera plus tard : la coque garde leurs emplacements.
- **Pas de liaison avec Pamk dans la V0** : c'est un palier réservé de la campagne Pamk.
  Les données (tâches, notes) gardent un identifiant et une date de modification pour
  pouvoir synchroniser plus tard.

## Style (voir `docs/visuel_concept.html`, qu'il adore)

Style « Lingot » : chanfreins à 45° (2 mm devant, 1,5 mm derrière), plan octogonal (angles
coupés à 8 mm), flancs en dépouille (plus large côté écran, ≈ 12°), deux rainures de 0,6 mm
façon Pamk, plaque aimantée au dos entre les rainures avec le visage pixel de Pamk en relief,
face avant = uniquement l'écran (fentes écouteur/micro dans les chanfreins haut et bas).
Bouton marche au centre du dessus, volume + à droite et volume − à gauche, à la même hauteur.
Les cotes du visuel (72 × 121 × 13) étaient pour un autre écran : tout se recale sur la LilyGO
et la batterie de 10 mm.

## Règles

- **Thomas compile et flashe lui-même.** Ne jamais lancer de compilation ni de flash, ne rien
  installer ni mettre à jour dans son Arduino (il y a le cœur esp32 2.0.17 pour Pamk).
- Commandes données à Thomas : **PowerShell**, enchaînées avec `;` (pas `&&`).
- Pas de commit git : c'est lui qui décide quand.
- Impression FDM d'abord (PLA, buse 0,4, Bambu Lab avec AMS), résine plus tard.
