// Polices du Pamk Phone (générées par firmware/outils/generer_polices.py).
// Barlow (texte) et VT323 (accents pixel), licence SIL OFL 1.1 : src/polices/OFL-*.txt
#pragma once
#include "epd_driver.h"   // structures GFXfont / GFXglyph / UnicodeInterval

extern const GFXfont* const F_PETITE;   // Barlow Medium 22 px : barre d'état, détails
extern const GFXfont* const F_TEXTE;    // Barlow Regular 28 px : texte courant
extern const GFXfont* const F_GRAS;     // Barlow SemiBold 28 px : boutons, intitulés
extern const GFXfont* const F_PIXEL;    // VT323 50 px : étiquettes pixel, heure de la barre
extern const GFXfont* const F_TITRE;    // VT323 75 px : titres d'écran
extern const GFXfont* const F_GRANDE;   // VT323 125 px : grands chiffres (ASCII + × ÷ −)
extern const GFXfont* const F_HORLOGE;  // VT323 200 px : horloge (chiffres, « : », « - »)
