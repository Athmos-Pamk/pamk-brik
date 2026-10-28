// Primitives de dessin sur la toile portrait (coordonnées 0..539 x 0..959).
// Texte UTF-8 avec anticrénelage (glyphes 4 bits des polices générées).
#pragma once
#include "config.h"
#include "polices.h"

// Une ligne de texte découpée : octets [debut, fin) de la chaîne d'origine.
struct LigneTexte {
  int debut;
  int fin;
};

enum Expression : uint8_t {
  VISAGE_NORMAL,
  VISAGE_DORT,
  VISAGE_ETEINT,
  VISAGE_CONTENT,
  VISAGE_FATIGUE,
};

enum Icone : uint8_t {
  ICONE_TACHES,
  ICONE_NOTES,
  ICONE_POMODORO,
  ICONE_CALENDRIER,
  ICONE_CALCULATRICE,
  ICONE_REGLAGES,
  ICONE_ACCUEIL,
  ICONE_COCHE,          // 7 x 6
  ICONE_CHEVRON_G,      // 5 x 7
  ICONE_CHEVRON_D,      // 5 x 7
  ICONE_MAJ,            // 9 x 8 (flèche creuse)
  ICONE_MAJ_PLEIN,      // 9 x 8 (verrouillage majuscules)
  ICONE_EFFACER,        // 10 x 5
  ICONE_ENTREE,         // 10 x 7
  ICONE_RECHERCHE,      // 12 x 12 (loupe)
  ICONE_BILAN,          // 12 x 12 (barres)
  ICONE_NB
};

namespace Dessin {

// Découpe (clip) : rien n'est dessiné hors de cette boîte.
void decoupe(const Boite& b);
void sansDecoupe();

void pixel(int x, int y, uint8_t c);
void rectPlein(int x, int y, int l, int h, uint8_t c);
void rect(int x, int y, int l, int h, int ep, uint8_t c);
void ligneH(int x, int y, int l, uint8_t c, int ep = 1);
void ligneV(int x, int y, int h, uint8_t c, int ep = 1);
void ligne(int x0, int y0, int x1, int y1, uint8_t c, int ep = 1);
void pointille(int x, int y, int l, uint8_t c, int pas = 6);
void chanfreinPlein(int x, int y, int l, int h, int k, uint8_t c);
void chanfrein(int x, int y, int l, int h, int k, int ep, uint8_t c);
void cerclePlein(int cx, int cy, int r, uint8_t c);
void cercle(int cx, int cy, int r, int ep, uint8_t c);
void barreProgression(const Boite& b, int pourMille, uint8_t fond, uint8_t plein);

// --- Texte (x = gauche, y = ligne de base) ---
uint32_t utf8Lire(const char*& s);                 // lit un point de code et avance
int utf8Precedent(const char* s, int pos);         // début du caractère avant pos
int utf8Encoder(uint32_t cp, char* sortie);        // retourne le nombre d'octets (1..4)
int largeur(const GFXfont* p, const char* s);
int largeurN(const GFXfont* p, const char* s, int nbOctets);
int hautCapitale(const GFXfont* p);                // hauteur des majuscules / chiffres
int hauteurLigne(const GFXfont* p);
int texte(const GFXfont* p, int x, int y, const char* s, uint8_t c);
int texteN(const GFXfont* p, int x, int y, const char* s, int nbOctets, uint8_t c);
void texteCentre(const GFXfont* p, int cx, int y, const char* s, uint8_t c);
void texteDroite(const GFXfont* p, int xDroite, int y, const char* s, uint8_t c);
// Texte coupé avec « … » s'il dépasse largeurMax. Retourne la largeur dessinée.
int texteTronque(const GFXfont* p, int x, int y, int largeurMax, const char* s, uint8_t c);
// Ligne de base pour centrer verticalement une police dans une boîte.
int baseCentree(const GFXfont* p, int y, int h);
// Coupe en lignes (aux espaces, ou au milieu des mots trop longs, et aux '\n').
int couperLignes(const GFXfont* p, const char* s, int largeurMax, LigneTexte* lignes, int maxLignes);

// --- Pixel art ---
// motif : lignes de caractères, '#' = couleur c, '+' = gris, autre = vide.
void motif(const char* const* lignes, int nbLignes, int x, int y, int taille, uint8_t c, int ecart = 0);
void icone(Icone i, int x, int y, int taille, uint8_t c);
int iconeLargeur(Icone i);
int iconeHauteur(Icone i);
// Visage de Pamk : deux yeux carrés creux 4x4 + bouche ondulée de 9 pixels (grille 11 x 7).
void visagePamk(int cx, int y, int p, Expression e, uint8_t c);

// --- Pictogrammes dessinés ---
void pictoBatterie(int x, int y, int pourcentage, bool charge, uint8_t c);   // 40 x 20
void pictoWifi(int x, int y, int barres, uint8_t c);                          // 30 x 22
void pictoPlus(int cx, int cy, int t, int ep, uint8_t c);
void pictoCroix(int cx, int cy, int t, int ep, uint8_t c);

}  // namespace Dessin
