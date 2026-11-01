// =============================================================================
//  Pamk Brik V0 — téléphone minimaliste e-paper (sans carte SIM ni son)
//  Carte : LILYGO T5 4.7" E-paper V2.3 ESP32-S3 avec écran tactile (H716)
//
//  Réglages de l'IDE et bibliothèques : voir firmware/README.md
//    Carte « ESP32S3 Dev Module », cœur esp32 2.0.17, Flash 16 Mo, PSRAM « OPI PSRAM »,
//    schéma « 16M Flash (3MB APP/9.9MB FATFS) », USB CDC On Boot « Enabled ».
//  Brochage et câblage des boutons : voir docs/BROCHAGE.md
//
//  Organisation :
//    config.h          brochage, constantes, mise en page
//    systeme.*         démarrage, pile d'applis, événements, rendu, veille
//    ecran.*           toile portrait + rafraîchissements de l'e-paper (LilyGo-EPD47)
//    dessin.*, ui.*    primitives, texte UTF-8, visage de Pamk, éléments d'interface
//    tactile.*         GT911 et gestes ; boutons.* : 3 boutons physiques
//    energie.*         batterie, veille légère, sommeil profond
//    horloge.*         RTC PCF8563, NTP, fuseau, dates en français
//    reseau.*          Wi-Fi à la demande ; stockage.* : JSON sur FFat ; parametres.* : NVS
//    minuteries.*      Pomodoro et minuteur (continuent pendant la veille)
//    stats.*           bilan de la semaine ; agenda.* : agenda iCal en lecture seule
//    acces.*           page web et mise à jour sans fil (écran « Accès depuis l'ordinateur »)
//    app_*.cpp         une application par fichier ; console.* : console série
// =============================================================================
#include "config.h"
#include "systeme.h"

// La connexion HTTPS de l'agenda (agenda.cpp) se fait dans loop() : la poignée de main TLS
// demande plus que les 8 Kio de pile par défaut de la tâche de loop().
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  Systeme::demarrer();
}

void loop() {
  Systeme::boucle();
}
