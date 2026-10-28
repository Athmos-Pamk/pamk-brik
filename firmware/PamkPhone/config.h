// =============================================================================
//  Pamk Phone V0 — configuration : brochage, constantes, mise en page
//  Carte : LILYGO T5 4.7" E-paper V2.3 ESP32-S3 (H716), écran ED047TC1 960x540.
//  Tout le brochage est justifié dans docs/BROCHAGE.md (schémas LilyGO V2.3 / V2.4).
// =============================================================================
#pragma once
#include <Arduino.h>

#define PAMK_NOM      "Pamk Phone"
#define PAMK_VERSION  "0.2.0"

#ifndef BOARD_HAS_PSRAM
#error "Active la PSRAM : Outils > PSRAM > OPI PSRAM (voir firmware/README.md)"
#endif

// ---------------------------------------------------------------------------
//  Brochage
//  Attention : epd_driver.h (LilyGo-EPD47) inclut utilities.h qui définit déjà
//  BUTTON_1, BATT_PIN, BOARD_SDA, TOUCH_INT... : on garde des noms français.
// ---------------------------------------------------------------------------
// Bouton marche/veille : GPIO21 = bouton S4 de la carte (pull-up 100 k + 4,7 µF).
// Le bouton externe se câble EN PARALLÈLE de S4 (entre GPIO21 et GND).
// GPIO21 est une broche RTC : c'est elle qui réveille du sommeil profond (ext1).
constexpr int BROCHE_MARCHE      = 21;
// Volume + / volume − : connecteur P4 (JST PH 2.0, 4 broches) = 3V3, IO45, IO48, GND.
// Bouton entre la broche et GND, pull-up interne. Si + et − sont inversés
// (l'ordre IO45/IO48 sur P4 diffère entre V2.3 et V2.4), utilise
// Réglages > Boutons > Inverser volume, ou échange ces deux valeurs.
constexpr int BROCHE_VOL_PLUS    = 48;
constexpr int BROCHE_VOL_MOINS   = 45;   // broche de configuration (VDD_SPI) : un bouton
                                         // vers GND ne peut la tirer qu'à 0 = valeur sûre.
// Batterie : pont 100 k / 100 k (R29/R32) vers GPIO14 (ADC2, inutilisable si Wi-Fi actif).
// Le pont n'est alimenté que quand PWR_EN est actif (epd_poweron()).
constexpr int BROCHE_BATTERIE    = 14;
constexpr float BATTERIE_RAPPORT = 2.0f;     // pont diviseur
constexpr int   BATTERIE_CORRECTION_MV = 0;  // à ajuster après mesure au multimètre
// Bus I2C partagé : tactile GT911 + horloge RTC PCF8563.
constexpr int BROCHE_I2C_SDA     = 18;
constexpr int BROCHE_I2C_SCL     = 17;
constexpr uint32_t I2C_FREQUENCE = 400000;
constexpr int BROCHE_TACTILE_INT = 47;       // pas une broche RTC : pas de réveil tactile
constexpr int BROCHE_RTC_INT     = 9;        // INT du PCF8563 (non utilisé en V0)
constexpr uint8_t ADRESSE_RTC    = 0x51;

// ---------------------------------------------------------------------------
//  Tactile GT911 : passage coordonnées brutes -> coordonnées natives 960x540
//  (mêmes réglages que les exemples LilyGO : échange X/Y + miroir Y).
// ---------------------------------------------------------------------------
constexpr bool TACTILE_ECHANGER_XY = true;
constexpr bool TACTILE_MIROIR_X    = false;
constexpr bool TACTILE_MIROIR_Y    = true;
constexpr int  TACTILE_PERIODE_MS  = 20;     // lecture du tactile
constexpr int  TACTILE_APPUI_LONG_MS = 650;
constexpr int  TACTILE_GLISSER_PX  = 80;
constexpr int  TACTILE_TOLERANCE_PX = 36;    // déplacement max pour un « toucher »

// ---------------------------------------------------------------------------
//  Boutons
// ---------------------------------------------------------------------------
// false tant que les trois boutons (marche, volume + et −) ne sont pas montés : tout se fait
// au tactile, et la veille se réveille au toucher (veille légère, voir Systeme). Le bouton S4
// de la carte (GPIO21) réveille toujours aussi. Passer à true quand ils seront câblés.
constexpr bool BOUTONS_PRESENTS = false;
constexpr uint32_t VEILLE_TACTILE_SCRUTATION_MS = 250;   // sans bouton : lecture du tactile en veille
constexpr int BOUTON_ANTIREBOND_MS  = 30;
constexpr int BOUTON_LONG_MS        = 800;   // volume : appui long
constexpr int BOUTON_MARCHE_LONG_MS = 2000;  // marche : appui long = éteindre
constexpr int BOUTON_DUO_MS         = 250;   // vol+ et vol− ensemble (écart max)

// ---------------------------------------------------------------------------
//  Écran e-paper
// ---------------------------------------------------------------------------
constexpr int ECRAN_L = 540;                 // portrait
constexpr int ECRAN_H = 960;
constexpr int NATIF_L = 960;                 // sens natif de la dalle
constexpr int NATIF_H = 540;
// Orientation par défaut : 0 = USB-C en bas (vérifié sur la carte de Thomas le 27/10/2026 :
// la valeur 1 de la première version affichait l'image à l'envers).
constexpr uint8_t ORIENTATION_DEFAUT = 0;
// Mise à jour « pixel par pixel » (défaut) : seuls les pixels qui changent sont pilotés.
// Ceux qui n'étaient pas blancs sont d'abord nettoyés (poussés au noir puis au blanc,
// MAJ_POUSSEES trames de DUREE_CYCLE_US dans chaque sens = un cycle de epd_clear()),
// MAJ_CYCLES_PIXELS fois, puis tous reçoivent leur nouvelle valeur.
constexpr int MAJ_CYCLES_PIXELS = 1;         // 2 si des traces restent (plus lent)
constexpr int MAJ_POUSSEES = 4;
constexpr bool MAJ_NETTOYAGE_FORT = true;    // false = ancien nettoyage (epd_draw_image), en secours
constexpr int MAJ_GROUPES_MAX = 3;           // groupes de colonnes traités séparément
// Deux groupes de colonnes plus proches que ça sont traités ensemble : chaque passe coûte
// ~0,1 s fixe, et chaque colonne portrait (= ligne de la dalle) ~1,2 ms par passe.
constexpr int MAJ_ECART_FUSION_PX = 110;
constexpr int PART_INCONNU_COMPLET_PCT = 50; // écran inconnu à plus de 50 % -> image complète
constexpr int CYCLES_NETTOYAGE_COMPLET = 4;  // image complète : cycles de epd_clear() (bibliothèque)
constexpr int DUREE_CYCLE_US = 50;           // durée d'un cycle (valeur de la bibliothèque)
constexpr int NETTOYAGE_AUTO_TOUS = 50;      // option « toutes les 50 mises à jour »

// ---------------------------------------------------------------------------
//  Énergie
// ---------------------------------------------------------------------------
constexpr int BATTERIE_FAIBLE_MV   = 3450;   // avertissement
constexpr int BATTERIE_VIDE_MV     = 3350;   // écran « recharge-moi » et sommeil
constexpr int BATTERIE_PLEINE_MV   = 4180;
constexpr int BATTERIE_CHARGE_MV   = 4230;   // au-dessus : on suppose l'USB branché
constexpr uint32_t MESURE_BATTERIE_MS = 120000;  // mesure périodique hors rafraîchissement
constexpr uint32_t VEILLE_LEGERE_MAX_MS = 40;    // réveil périodique pour lire le tactile
constexpr uint32_t DELAI_AVANT_VEILLE_LEGERE_MS = 1500; // après la dernière action
constexpr int NUIT_DEBUT_H = 23;             // mode nuit (Param.ecoNuit) : de 23 h…
constexpr int NUIT_FIN_H = 7;                // …à 7 h

// ---------------------------------------------------------------------------
//  Données
// ---------------------------------------------------------------------------
constexpr int TACHES_MAX        = 300;
constexpr int TACHE_TITRE_MAX   = 120;   // octets UTF-8
constexpr int NOTES_MAX         = 100;
constexpr int NOTE_TEXTE_MAX    = 4000;  // octets UTF-8
constexpr int SUPPRIMES_MAX     = 100;   // traces de suppression gardées (future synchro)
constexpr int ID_LONGUEUR       = 16;    // identifiants hexadécimaux
constexpr int WIFI_RESEAUX_MAX  = 4;

#define FUSEAU_DEFAUT   "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Paris
#define NTP_DEFAUT      "pool.ntp.org"
#define NTP_SECOURS     "time.google.com"

// ---------------------------------------------------------------------------
//  Mise en page (pixels, portrait 540 x 960). Reprise par firmware/outils/apercus.py.
// ---------------------------------------------------------------------------
constexpr int MARGE          = 24;
constexpr int BARRE_ETAT_H   = 48;
constexpr int BARRE_TITRE_H  = 84;
constexpr int CONTENU_Y      = BARRE_ETAT_H + BARRE_TITRE_H;   // 132
constexpr int PIED_H         = 88;
constexpr int PIED_Y         = ECRAN_H - PIED_H;               // 872
constexpr int LIGNE_H        = 92;                             // ligne de liste
constexpr int LIGNES_PAR_PAGE = (PIED_Y - CONTENU_Y) / LIGNE_H; // 8
constexpr int CHANFREIN      = 10;
constexpr int TRAIT          = 3;
constexpr int CASE_TAILLE    = 40;
constexpr int RETOUR_L       = 96;                             // zone « retour » de la barre de titre
constexpr int CLAVIER_Y      = 596;                            // haut du clavier
constexpr int CLAVIER_TOUCHE_H = 82;
constexpr int CLAVIER_ECART  = 6;

// Niveaux de gris (4 bits : 0 = noir, 15 = blanc)
constexpr uint8_t NOIR       = 0;
constexpr uint8_t GRIS_FONCE = 5;
constexpr uint8_t GRIS       = 9;
constexpr uint8_t GRIS_CLAIR = 12;
constexpr uint8_t BLANC      = 15;

// ---------------------------------------------------------------------------
//  Petits outils communs
// ---------------------------------------------------------------------------
struct Boite {
  int x, y, l, h;
};
inline Boite boite(int x, int y, int l, int h) { Boite b; b.x = x; b.y = y; b.l = l; b.h = h; return b; }
inline bool dansBoite(const Boite& b, int px, int py) {
  return px >= b.x && px < b.x + b.l && py >= b.y && py < b.y + b.h;
}
inline int mini(int a, int b) { return a < b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline int borner(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

// Journal série (désactivable)
#define JOURNAL(...) do { Serial.printf(__VA_ARGS__); Serial.print("\r\n"); } while (0)
