// Énergie : mesure de la batterie (GPIO14, pont /2 alimenté par PWR_EN), pourcentage
// selon la courbe d'une LiPo, veille légère entre deux actions et sommeil profond.
#pragma once
#include "config.h"
#include <esp_sleep.h>

namespace Energie {

void demarrer();
void mesurer();                     // à appeler alimentation de la dalle active (rappel d'Ecran)
void mesurerMaintenant();           // allume PWR_EN le temps de la mesure (si Wi-Fi éteint)
void boucle();                      // mesure périodique
int millivolts();                   // 0 = pas encore mesuré
int pourcentage();
bool mesureValide();
bool enCharge();                    // supposition : tension de charge ou ordinateur branché
bool usbOrdinateur();               // hôte USB détecté (trames SOF)
bool faible();
bool vide();

void activite();                    // remet à zéro le compteur d'inactivité
uint32_t inactifDepuisMs();

void veilleLegere(uint32_t dureeMaxMs);
// Sommeil profond : réveil par le bouton marche (GPIO21, ext1) et, si dansSecondes > 0,
// par minuterie. Ne revient pas.
void sommeilProfond(uint32_t dansSecondes);

esp_sleep_wakeup_cause_t causeReveil();
const char* texteCauseReveil();

}  // namespace Energie
