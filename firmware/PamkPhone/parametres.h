// Réglages utilisateur, gardés en flash (NVS, espace « pamk ») via Preferences.
#pragma once
#include "config.h"

struct Parametres {
  uint16_t veilleS;          // mise en veille après inactivité (0 = jamais)
  uint8_t horlogeVeilleMin;  // rafraîchir l'heure de l'écran de veille (0 = non)
  uint8_t orientation;       // 0 / 1 (180°) ; 0 = USB-C en bas
  uint8_t modeMaj;           // MAJ_PIXELS (0, défaut) ou MAJ_DOUX (1) : voir ecran.h
  uint8_t nettoyageAuto;     // image complète automatique : 0 jamais, 1 à chaque mise en
                             // veille, 2 toutes les 50 mises à jour, 3 auto selon l'usure (défaut)
  bool inverserVolume;
  bool veilleLegere;         // veille légère entre deux actions
  bool veilleSurUSB;         // veille automatique même branché à un ordinateur
  uint8_t wifiMode;          // 0 éteint, 1 auto (synchro de l'heure), 2 toujours
  uint8_t pomoTravail;       // minutes
  uint8_t pomoPause;
  uint8_t pomoLongue;
  uint8_t pomoCycles;        // pomodoros avant la grande pause
  uint8_t minuteurMin;       // dernière durée du minuteur
  char fuseau[64];           // règle POSIX
  char ntp[64];
  char prenom[32];
  // Ajouts du 28/10/2026 (version 3 des réglages)
  bool retourToucher;        // le bouton touché s'inverse tout de suite
  bool clavierRapide;        // clavier : lettres en noir et blanc pur, gris corrigés après
  uint8_t tailleTexte;       // 0 normale, 1 grande (listes, notes)
  bool ecoNuit;              // de 23 h à 7 h : heure de la veille figée, pas de Wi-Fi auto
  uint8_t sommeilApresH;     // veille sans toucher pendant N heures -> sommeil profond (0 = jamais)
  bool assistantFait;        // premier démarrage guidé terminé
  char icalUrl[256];         // adresse iCal secrète de l'agenda (vide = pas d'agenda). Secret :
                             // jamais dans le journal série.
};

extern Parametres Param;

void parametresParDefaut();
void chargerParametres();
void sauverParametres();
void effacerParametres();    // réglages + réseaux Wi-Fi mémorisés
