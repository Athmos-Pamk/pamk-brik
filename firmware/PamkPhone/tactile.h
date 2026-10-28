// Tactile capacitif GT911 (I2C 0x5D ou 0x14, INT sur GPIO47) et gestes.
// Pilote minimal écrit ici (pas de SensorLib) : lecture du registre d'état 0x814E,
// du premier point en 0x814F, puis remise à zéro de l'état.
#pragma once
#include "config.h"

enum TypeGeste : uint8_t {
  GESTE_AUCUN,
  GESTE_POSE,        // le doigt vient de se poser (x, y) : sert au retour immédiat (28/10/2026)
  GESTE_TOUCHER,     // appui court
  GESTE_APPUI_LONG,  // doigt immobile >= TACTILE_APPUI_LONG_MS
  GESTE_GLISSER,     // déplacement >= TACTILE_GLISSER_PX (dx, dy)
};

struct Geste {
  TypeGeste type;
  int x, y;     // point de départ (portrait)
  int dx, dy;   // déplacement pour un glissé
};

namespace Tactile {

bool demarrer(bool apresSommeil);  // réveille le GT911 et cherche son adresse
bool present();
uint8_t adresse();
Geste suivre();                     // à appeler à chaque tour de boucle
bool doigtPose();
void purger();                      // après un rafraîchissement : ignore le contact en cours
void dormir();                      // mode veille du GT911 (avant le sommeil profond)
bool reveilParContact();            // veille sans bouton : contact depuis le dernier purger() ?
bool intActifHaut();                // niveau de INT signalant une donnée (pour la veille légère)
void definirDebug(bool actif);
bool debug();

}  // namespace Tactile
