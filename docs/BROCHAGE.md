# Pamk Brik V0 — brochage et câblage

Carte : **LILYGO T5 4.7" E-paper V2.3 ESP32-S3** (module ESP32-S3-WROOM-1-N16R8 : 16 Mo de flash,
8 Mo de PSRAM octale), version « With Touch Glass », « No Solder Pin » (code LilyGO H716).

Tout ce qui suit vient des sources de LilyGO, relues le 13/10/2026 dans le dépôt
`Xinyuan-LilyGO/LilyGo-EPD47`, branche `esp32s3`, version 1.1.0 (copie de lecture dans
`firmware/_ref/`) :

- schéma **T5-ePaper-S3-V2.3.pdf** et schéma suivant **Screen-4.7-S3-V2.4 24-12-03.pdf** (dossier `schematic/`) ;
- tableau « GPIO List » du `README.MD` ;
- `src/utilities.h` et `src/ed047tc1.h` (broches utilisées par le pilote d'écran) ;
- exemples `demo`, `touch`, `sleep` (formule de la batterie, adresses du tactile, réveil) ;
- fiche de la dalle `datasheet/ED047TC1.pdf` (sens de balayage, page 8).

Les cotes de la carte, la position des connecteurs et le modèle exact reçu (V2.3 ou V2.4)
sont **à vérifier sur la carte** à réception : la sérigraphie indique « Screen-4.7-S3 V2.x ».

## Les 3 boutons du téléphone

| Bouton | GPIO | Où le câbler | Pourquoi ce choix |
|---|---|---|---|
| **Marche / veille** (dessus) | **21** | en **parallèle du bouton S4** de la carte (sérigraphié `SENSOR_VN`, à côté de RST et BOOT) : un fil sur la patte côté GPIO21, un fil sur la patte côté GND | GPIO21 est une broche RTC (0 à 21) : elle peut réveiller l'ESP32-S3 du sommeil profond (réveil « ext1 », déjà utilisé par les exemples LilyGO). Elle a déjà sa résistance de rappel 100 kΩ (R8) vers VDD3V3, toujours alimentée, et un condensateur 4,7 µF (C13) anti-rebond. Aucune broche ne se perd. |
| **Volume +** (flanc droit) | **48** | connecteur **P4** (JST PH 2.0, 4 broches) | broche libre, pas de rôle au démarrage, accessible sans souder sur la carte |
| **Volume −** (flanc gauche) | **45** | connecteur **P4** | broche libre ; c'est une broche de configuration (tension VDD_SPI) mais un bouton vers la masse ne peut que la mettre à 0, qui est justement la bonne valeur (3,3 V) |

Chaque bouton CMS 3 × 6 mm se branche **entre sa broche et la masse (GND)**, rien d'autre :
le firmware active les résistances de rappel internes.

### Connecteur P4 (JST PH 2.0, 4 broches)

| Broche P4 | Carte V2.3 (schéma) | Carte V2.4 (schéma) | Câblage |
|---|---|---|---|
| 1 | 3V3 (coupé en veille) | 3V3 | **ne rien brancher** |
| 2 | IO45 (« MSIO ») | IO48 (via R23) | un bouton vers la broche 4 |
| 3 | IO48 (« SCLK ») | IO45 (via R26) | l'autre bouton vers la broche 4 |
| 4 | GND | GND | masse commune des deux boutons |

L'ordre IO45/IO48 est **inversé entre les schémas V2.3 et V2.4**. Si volume + et volume −
font l'inverse de ce qu'ils devraient : **Réglages > Boutons > Inverser volume + et −**
(ou échanger `BROCHE_VOL_PLUS` / `BROCHE_VOL_MOINS` dans `config.h`). Repère la broche 1
avec la sérigraphie ou au multimètre (la broche 4 sonne avec `BAT-`).

Câblage conseillé : un câble JST PH 2.0 4 broches déjà serti, on coupe le fil de la broche 1.

**Si P4 n'est pas soudé sur ta carte** (d'après les photos LilyGO relevées pour la mécanique,
`docs/MECANIQUE.md`, la V2.4 n'a qu'un connecteur 4 broches, côté P5) : souder les fils sur
les pastilles de P4, ou sur le connecteur 2 × 20 : **broche 21 = même signal que P4 broche 2**
(« MSIO »), **broche 23 = même signal que P4 broche 3** (« SCLK »), plus une broche GND de ce
connecteur (repérée au multimètre avec `BAT-`). La même inversion V2.3 / V2.4 s'applique.

### Bouton marche en parallèle de S4

S4 est un petit bouton CMS à 4 pattes. Au multimètre (mode continuité) : les pattes qui
sonnent avec `BAT-` sont côté masse, les autres côté GPIO21. Souder le bouton externe entre
une patte de chaque côté. Les deux boutons (S4 et l'externe) font alors la même chose.

À savoir : à cause du condensateur de 4,7 µF, la broche remonte lentement au relâchement
(environ 0,2 s avec le rappel interne activé par le firmware) ; c'est pour ça que l'appui
long « éteindre » est fixé à 2 s.

### Autres broches utilisables plus tard

- **GPIO10** (« MOSI », broche 19 du connecteur 2 × 20), **GPIO39** (« CS », broche 24),
  **GPIO43 / GPIO44** (TX0 / RX0, broches 10 et 8 : libres puisque la console passe par l'USB).
  LilyGO note GPIO10 « entrée analogique seulement » mais ses propres explications (ticket #93)
  s'en servent comme entrée numérique : à tester avant de compter dessus.
- **GPIO16, 15, 11, 42** = lecteur de carte TF : **réservées à l'audio de la V1** (micro + ampli
  en I2S : horloge, mot, données micro, données ampli). Aucun bouton dessus.

## Batterie

| | |
|---|---|
| Connecteur | JST PH 2.0, 2 broches, sérigraphié `BAT-` / `BAT+` |
| Batterie | LiPo 103450, 3,7 V, 2000 mAh |
| Charge | par l'USB-C, circuit HX6610S de la carte (LED de charge) |
| Mesure | pont 100 kΩ / 100 kΩ 1 % (R29 / R32) vers **GPIO14** (ADC2, canal 3), 10 nF de filtrage |

**Avant de brancher la batterie : vérifier la polarité.** Le sens des fils des câbles JST PH
n'est pas normalisé : le fil rouge doit arriver sur `BAT+`. Une inversion peut détruire la
carte ou la batterie. Si le sens est faux, retourner les deux broches serties dans le boîtier
(on soulève les languettes avec une aiguille).

Mesure de tension (exemple LilyGO `demo`) : le pont n'est alimenté que lorsque la sortie
« PWR_EN » est active, c'est-à-dire après `epd_poweron()`. Le firmware mesure donc pendant
chaque rafraîchissement de l'écran (et toutes les 2 minutes sinon) :
tension = `analogReadMilliVolts(14)` × 2. L'ADC2 ne répond pas quand le Wi-Fi tourne : la
mesure est alors sautée. À comparer au multimètre (correction : `BATTERIE_CORRECTION_MV`
dans `config.h`).

## Tout le brochage de la carte

| GPIO | Rôle sur la carte | Utilisé par Pamk Brik |
|---|---|---|
| 0 | STR du registre 74HCT4094 (commande de l'écran) + bouton BOOT (S6) ; broche de démarrage | pilote d'écran |
| 1, 2, 3, 4, 5, 6, 7, 8 | données de l'écran D1, D2, D3, D4, D5, D6, D7, D0 | pilote d'écran |
| 9 | INT de l'horloge PCF8563 (rappel 10 kΩ R39) | non (réservé) |
| 10 | « MOSI », connecteur 2 × 20 broche 19 | libre |
| 11 | carte TF (horloge SPI) | réservé audio V1 |
| 12 | CLK du 74HCT4094 | pilote d'écran |
| 13 | DATA du 74HCT4094 | pilote d'écran |
| 14 | mesure batterie (ADC2_CH3) | batterie |
| 15, 16 | carte TF (MOSI, MISO), aussi sur le connecteur P5 | réservé audio V1 |
| 17 | I2C SCL (tactile GT911 + horloge PCF8563) | oui |
| 18 | I2C SDA | oui |
| 19, 20 | USB D− / D+ | USB (programmation, console) |
| **21** | bouton S4 « SENSOR_VN » (100 kΩ + 4,7 µF) | **bouton marche** |
| 26 à 37 | flash et PSRAM du module (non sorties) | — |
| 38 | CKV de l'écran | pilote d'écran |
| 39 | « CS », connecteur 2 × 20 broche 24 | libre |
| 40, 41 | STH, CKH de l'écran | pilote d'écran |
| 42 | carte TF (CS) | réservé audio V1 |
| 43, 44 | TX0 / RX0, connecteur 2 × 20 | libres |
| **45** | « MSIO », connecteur P4 (et 2 × 20 broche 21) ; broche de configuration VDD_SPI | **volume −** |
| 46 | reliée au même fil que GPIO0 (STR_IO0) sur le schéma | ne pas utiliser |
| 47 | INT du tactile (connecteur P2) | tactile (pas de réveil possible : pas une broche RTC) |
| **48** | « SCLK », connecteur P4 (et 2 × 20 broche 23) | **volume +** |

## Bus I2C (SDA 18, SCL 17)

| Adresse | Composant | Remarque |
|---|---|---|
| 0x5D ou 0x14 | tactile **GT911** | l'adresse dépend de l'état de INT au démarrage ; le firmware essaie les deux (comme l'exemple `touch`) |
| 0x51 | horloge **PCF8563** (+ pile rechargeable MS412FE sur le schéma) | garde l'heure quand la batterie est débranchée ; présence à vérifier (commande `etat`) |

## Écran

ED047TC1, 960 × 540, 16 niveaux de gris, commandé en parallèle (8 bits) par le pilote
LilyGo-EPD47. La dalle est en portrait de nature (540 × 960) avec sa nappe sur un petit
côté ; le firmware dessine en portrait et tourne l'image. D'après la position du connecteur
de nappe sur la photo de la carte, la première version avait choisi « orientation 1 » :
l'image était à l'envers sur la carte de Thomas (27/10/2026). **« Orientation 0 » met
l'USB-C en bas** (`ORIENTATION_DEFAUT = 0` dans `config.h`) ; le tactile suit la même
orientation. Réglages > Écran > Retourner l'écran inverse les deux.
