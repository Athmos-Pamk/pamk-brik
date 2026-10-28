// Écrans plein écran du système (dessinés sur la toile, sans barre d'état).
// L'écran de veille reste affiché sans consommer : c'est le propre de l'e-paper.
// Le dernier écran affiché avant le sommeil est mémorisé (mémoire RTC) : au réveil,
// redessinerMemorise() refait exactement la même image, ce qui permet de ne changer
// ensuite que les pixels qui diffèrent (pas de rafraîchissement complet au réveil).
#pragma once
#include "config.h"

namespace Ecrans {

void demarrage(const char* message);
void veille(bool profonde = false);   // données actuelles : mémorise puis dessine ;
                                      // profonde : réveil par le bouton de la carte (sans bouton)
bool veilleProfonde();                // l'écran mémorisé est une veille profonde
void eteint();
void batterieVide();
bool redessinerMemorise();     // false si rien n'est mémorisé (démarrage à froid, reset)

}  // namespace Ecrans
