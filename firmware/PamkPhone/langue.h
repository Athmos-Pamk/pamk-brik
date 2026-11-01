// Langue de l'interface (01/11/2026) : français ou anglais.
// Le texte d'origine, dans le code, est en français. En anglais, chaque texte affiché par
// Dessin (texte, texteCentre, texteDroite, texteTronque, largeur) est cherché dans la table
// de langue.cpp ; les formats de snprintf passent par TR(). Un texte absent de la table reste
// en français : rien ne casse s'il en manque un.
#pragma once
#include "config.h"

enum : uint8_t { LANGUE_FR = 0, LANGUE_EN = 1 };

bool anglais();
const char* tr(const char* fr);   // le texte à afficher (le même pointeur en français ou s'il manque)
#define TR(s) tr(s)
