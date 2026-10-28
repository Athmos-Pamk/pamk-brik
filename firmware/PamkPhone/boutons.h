// Trois boutons physiques : marche/veille (GPIO21), volume + (GPIO48), volume − (GPIO45).
// Anti-rebond, appui court / long, et « duo » (volume + et − ensemble).
#pragma once
#include "config.h"

enum Bouton : uint8_t { BOUTON_MARCHE, BOUTON_PLUS, BOUTON_MOINS };
enum TypeAppui : uint8_t { APPUI_COURT, APPUI_LONG, APPUI_DUO };

struct EvenementBouton {
  Bouton bouton;
  TypeAppui type;
};

namespace Boutons {

void demarrer();                        // un bouton déjà enfoncé (réveil) est ignoré jusqu'au relâchement
void scruter();                         // à appeler souvent
bool lire(EvenementBouton& e);          // prochain événement
bool unAppuye();
bool marcheAppuye();                    // lecture directe de la broche
void inverserVolume(bool inverser);
void definirDebug(bool actif);
const char* nom(Bouton b);

}  // namespace Boutons
