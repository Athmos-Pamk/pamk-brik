// Éléments d'interface « Pamk » : noir/blanc/gris, coins coupés, accents pixel.
// Les zones tactiles sont enregistrées pendant le dessin (interface « immédiate ») :
// un toucher est ensuite relié à la zone la plus proche.
#pragma once
#include "config.h"
#include "dessin.h"

// Codes de zone réservés (les applis utilisent des codes >= 0)
constexpr int CODE_RETOUR = -1;
constexpr int CODE_PAGE_PREC = -2;
constexpr int CODE_PAGE_SUIV = -3;

enum StyleBouton : uint8_t { STYLE_CONTOUR, STYLE_PLEIN, STYLE_DISCRET, STYLE_INACTIF };

// Pagination simple d'une liste
struct Pages {
  int page;
  int nb;
  void regler(int nbElements, int parPage) {
    nb = nbElements <= 0 ? 1 : (nbElements + parPage - 1) / parPage;
    if (page >= nb) page = nb - 1;
    if (page < 0) page = 0;
  }
  bool suivante() { if (page + 1 < nb) { page++; return true; } return false; }
  bool precedente() { if (page > 0) { page--; return true; } return false; }
};

namespace UI {

void viderZones();
void zone(const Boite& b, int code, int arg = 0);
bool chercher(int x, int y, int& code, int& arg);
bool chercherZone(int x, int y, Boite& b);   // la zone qui recevrait ce toucher (28/10/2026)
int nbZones();

void barreEtat(bool afficherHeure);
// Barre de titre. action : nullptr, "+" (bouton plus) ou un texte court (bouton plein).
void barreTitre(const char* titre, bool retour, const char* action = nullptr, int codeAction = 0,
                const GFXfont* police = nullptr);   // police : F_TITRE par défaut
void bouton(const Boite& b, const char* libelle, int code, int arg = 0,
            StyleBouton style = STYLE_CONTOUR, const GFXfont* police = nullptr);
void caseACocher(int x, int y, bool cochee);
void separateur(int y, int x0 = MARGE, int x1 = ECRAN_L - MARGE);
void etiquette(int x, int yBase, const char* texte);        // titre de section (pixel, gris)
void pagination(const Pages& p);
void interrupteur(int xDroite, int yCentre, bool actif);    // 100 x 48
void ligneReglage(int y, const char* libelle, const char* valeur, int code, int arg = 0);
void ligneInterrupteur(int y, const char* libelle, bool actif, int code);
void messageVide(int y, const char* ligne1, const char* ligne2);   // liste vide (avec Pamk)
void toast(const char* message);

}  // namespace UI
