// Accès depuis l'ordinateur (28/10/2026) : pendant que l'écran « Accès depuis l'ordinateur »
// est ouvert, le téléphone se connecte au Wi-Fi connu et sert une petite page web
// (tâches, notes, sauvegarde, adresse de l'agenda, envoi d'un nouveau firmware .bin) et
// accepte la mise à jour sans fil depuis l'Arduino IDE (ArduinoOTA).
// Tout est protégé par un code à 4 chiffres tiré au hasard et affiché sur l'écran.
// Rien ne tourne quand l'écran est fermé (batterie, sécurité).
#pragma once
#include "config.h"

namespace Acces {

bool demarrer();          // lance la connexion Wi-Fi si besoin ; le serveur démarre une fois connecté
void arreter();           // coupe serveur, OTA et Wi-Fi (selon Param.wifiMode)
void boucle();            // à appeler à chaque tour de boucle tant que actif()
bool actif();
bool pret();              // connecté et serveur en route
const char* adresse();    // « http://192.168.1.23 », ou "" tant que pas connecté
const char* code();       // code à 4 chiffres (page web et mot de passe OTA)
const char* etat();       // texte court pour l'écran (« connexion… », « prêt », « mise à jour 42 % »…)

// Ajouts (28/10/2026)
// Pendant une mise à jour du firmware, la boucle principale est bloquée (réception dans
// boucle()) : ce rappel laisse l'écran se dessiner lui-même au début, à chaque palier de
// 10 % et à la fin.
void definirRappelAffichage(void (*rappel)());
int progression();        // 0..100 pendant une mise à jour, -1 sinon

}  // namespace Acces
