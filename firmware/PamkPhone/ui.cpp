#include "ui.h"
#include "energie.h"
#include "reseau.h"
#include "horloge.h"
#include "minuteries.h"

namespace UI {

struct Zone {
  Boite b;
  int code;
  int arg;
};

static const int ZONES_MAX = 96;
static const int TOLERANCE = 16;   // un toucher juste à côté d'un bouton compte quand même
static Zone s_zones[ZONES_MAX];
static int s_nbZones = 0;

void viderZones() { s_nbZones = 0; }
int nbZones() { return s_nbZones; }

void zone(const Boite& b, int code, int arg) {
  if (s_nbZones >= ZONES_MAX) return;
  s_zones[s_nbZones].b = b;
  s_zones[s_nbZones].code = code;
  s_zones[s_nbZones].arg = arg;
  s_nbZones++;
}

bool chercher(int x, int y, int& code, int& arg) {
  // Zone qui contient le point : la dernière dessinée gagne (elle est au-dessus).
  for (int i = s_nbZones - 1; i >= 0; i--) {
    if (dansBoite(s_zones[i].b, x, y)) {
      code = s_zones[i].code;
      arg = s_zones[i].arg;
      return true;
    }
  }
  // Sinon la plus proche à moins de TOLERANCE pixels.
  int meilleure = -1, dmin = TOLERANCE + 1;
  for (int i = 0; i < s_nbZones; i++) {
    const Boite& b = s_zones[i].b;
    const int dx = x < b.x ? b.x - x : (x >= b.x + b.l ? x - (b.x + b.l - 1) : 0);
    const int dy = y < b.y ? b.y - y : (y >= b.y + b.h ? y - (b.y + b.h - 1) : 0);
    const int d = maxi(dx, dy);
    if (d < dmin) { dmin = d; meilleure = i; }
  }
  if (meilleure < 0) return false;
  code = s_zones[meilleure].code;
  arg = s_zones[meilleure].arg;
  return true;
}

void barreEtat(bool afficherHeure) {
  const int base = 36;
  if (afficherHeure && Horloge::valide()) {
    struct tm t;
    Horloge::locale(t);
    char h[8];
    Horloge::texteHeure(h, sizeof(h), t);
    Dessin::texte(F_PIXEL, MARGE, base, h, NOIR);
  } else if (afficherHeure) {
    Dessin::texte(F_PIXEL, MARGE, base, "--:--", GRIS);
  }
  // Pomodoro / minuteur en cours
  char info[24] = "";
  if (Minuteries::pomoPhase() != POMO_ARRET) {
    Minuteries::texteRestant(info, sizeof(info), Minuteries::pomoRestantS());
  } else if (Minuteries::minActif()) {
    Minuteries::texteRestant(info, sizeof(info), Minuteries::minRestantS());
  }
  if (info[0]) {
    const int l = Dessin::largeur(F_PETITE, info) + 22;
    const int x = ECRAN_L / 2 - l / 2;
    Dessin::rectPlein(x, 16, 12, 12, (Minuteries::pomoSuspendu() || Minuteries::minSuspendu()) ? GRIS : NOIR);
    Dessin::texte(F_PETITE, x + 22, 31, info, NOIR);
  }
  // Batterie
  int x = ECRAN_L - MARGE;
  char pct[8];
  if (Energie::mesureValide()) snprintf(pct, sizeof(pct), "%d%%", Energie::pourcentage());
  else snprintf(pct, sizeof(pct), "--%%");
  const int lp = Dessin::largeur(F_PIXEL, pct);
  Dessin::texte(F_PIXEL, x - lp, base, pct, Energie::faible() ? NOIR : GRIS_FONCE);
  x -= lp + 8 + 38;
  Dessin::pictoBatterie(x, 14, Energie::pourcentage(), Energie::enCharge(), NOIR);
  // Wi-Fi
  if (Reseau::etat() == RESEAU_CONNECTE) {
    x -= 30 + 14;
    Dessin::pictoWifi(x, 12, Reseau::barres(), NOIR);
  } else if (Reseau::occupe()) {
    x -= 30 + 14;
    Dessin::pictoWifi(x, 12, 0, GRIS);
  }
  Dessin::rectPlein(0, BARRE_ETAT_H - 2, ECRAN_L, 2, GRIS_CLAIR);
}

void barreTitre(const char* titre, bool retour, const char* action, int codeAction, const GFXfont* police) {
  if (!police) police = F_TITRE;
  const int y = BARRE_ETAT_H;
  int x = MARGE;
  if (retour) {
    Dessin::icone(ICONE_CHEVRON_G, MARGE + 2, y + (BARRE_TITRE_H - 35) / 2, 5, NOIR);
    zone(boite(0, y, RETOUR_L, BARRE_TITRE_H), CODE_RETOUR);
    x = 72;
  }
  int droite = ECRAN_L - MARGE;
  if (action) {
    const bool plus = strcmp(action, "+") == 0;
    const int l = plus ? 68 : maxi(84, Dessin::largeur(F_GRAS, action) + 36);
    const Boite b = boite(ECRAN_L - MARGE - l, y + 10, l, 64);
    if (plus) {
      Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, TRAIT, NOIR);
      Dessin::pictoPlus(b.x + b.l / 2, b.y + b.h / 2, 30, 6, NOIR);
      zone(boite(b.x - 12, y, b.l + 12 + MARGE, BARRE_TITRE_H), codeAction);
    } else {
      bouton(b, action, codeAction, 0, STYLE_PLEIN);
    }
    droite = b.x - 12;
  }
  Dessin::texteTronque(police, x, Dessin::baseCentree(police, y, BARRE_TITRE_H), droite - x, titre, NOIR);
  Dessin::rectPlein(0, CONTENU_Y - 2, ECRAN_L, 2, GRIS_CLAIR);
}

void bouton(const Boite& b, const char* libelle, int code, int arg, StyleBouton style, const GFXfont* police) {
  if (!police) police = F_GRAS;
  uint8_t texte = NOIR;
  switch (style) {
    case STYLE_PLEIN:
      Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, NOIR);
      texte = BLANC;
      break;
    case STYLE_DISCRET:
      Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, 2, GRIS);
      break;
    case STYLE_INACTIF:
      Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, 2, GRIS_CLAIR);
      texte = GRIS;
      break;
    default:
      Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, TRAIT, NOIR);
      break;
  }
  if (libelle && *libelle) {
    const int l = mini(Dessin::largeur(police, libelle), b.l - 16);
    Dessin::texteTronque(police, b.x + (b.l - l) / 2, Dessin::baseCentree(police, b.y, b.h), b.l - 16, libelle, texte);
  }
  if (style != STYLE_INACTIF) zone(b, code, arg);
}

void caseACocher(int x, int y, bool cochee) {
  if (cochee) {
    Dessin::chanfreinPlein(x, y, CASE_TAILLE, CASE_TAILLE, 7, NOIR);
    Dessin::icone(ICONE_COCHE, x + (CASE_TAILLE - 28) / 2, y + (CASE_TAILLE - 24) / 2, 4, BLANC);
  } else {
    Dessin::chanfrein(x, y, CASE_TAILLE, CASE_TAILLE, 7, TRAIT, NOIR);
  }
}

void separateur(int y, int x0, int x1) { Dessin::rectPlein(x0, y, x1 - x0, 2, GRIS_CLAIR); }

void etiquette(int x, int yBase, const char* texte) { Dessin::texte(F_PIXEL, x, yBase, texte, GRIS_FONCE); }

void pagination(const Pages& p) {
  if (p.nb <= 1) return;
  const int y = PIED_Y + 12, h = 64, l = 120;
  const bool avant = p.page > 0, apres = p.page + 1 < p.nb;
  const Boite g = boite(MARGE, y, l, h);
  const Boite d = boite(ECRAN_L - MARGE - l, y, l, h);
  Dessin::chanfrein(g.x, g.y, g.l, g.h, CHANFREIN, avant ? TRAIT : 2, avant ? NOIR : GRIS_CLAIR);
  Dessin::icone(ICONE_CHEVRON_G, g.x + (l - 20) / 2, y + (h - 28) / 2, 4, avant ? NOIR : GRIS_CLAIR);
  Dessin::chanfrein(d.x, d.y, d.l, d.h, CHANFREIN, apres ? TRAIT : 2, apres ? NOIR : GRIS_CLAIR);
  Dessin::icone(ICONE_CHEVRON_D, d.x + (l - 20) / 2, y + (h - 28) / 2, 4, apres ? NOIR : GRIS_CLAIR);
  if (avant) zone(g, CODE_PAGE_PREC);
  if (apres) zone(d, CODE_PAGE_SUIV);
  char t[16];
  snprintf(t, sizeof(t), "%d / %d", p.page + 1, p.nb);
  Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, Dessin::baseCentree(F_PIXEL, y, h), t, GRIS_FONCE);
}

void interrupteur(int xDroite, int yCentre, bool actif) {
  const int l = 100, h = 48;
  const int x = xDroite - l, y = yCentre - h / 2;
  if (actif) {
    Dessin::chanfreinPlein(x, y, l, h, 8, NOIR);
    Dessin::texteCentre(F_GRAS, x + l / 2, Dessin::baseCentree(F_GRAS, y, h), "Oui", BLANC);
  } else {
    Dessin::chanfrein(x, y, l, h, 8, TRAIT, NOIR);
    Dessin::texteCentre(F_GRAS, x + l / 2, Dessin::baseCentree(F_GRAS, y, h), "Non", NOIR);
  }
}

void ligneReglage(int y, const char* libelle, const char* valeur, int code, int arg) {
  const int h = 84;
  const int base = Dessin::baseCentree(F_TEXTE, y, h);
  const int xDroite = ECRAN_L - MARGE - 24;
  int lv = 0;
  if (valeur && *valeur) lv = mini(Dessin::largeur(F_GRAS, valeur), 230);
  Dessin::texteTronque(F_TEXTE, MARGE, base, xDroite - lv - 16 - MARGE, libelle, NOIR);
  if (lv) Dessin::texteTronque(F_GRAS, xDroite - lv, base, 230, valeur, GRIS_FONCE);
  Dessin::icone(ICONE_CHEVRON_D, ECRAN_L - MARGE - 15, y + (h - 21) / 2, 3, GRIS);
  separateur(y + h - 2);
  zone(boite(0, y, ECRAN_L, h), code, arg);
}

void ligneInterrupteur(int y, const char* libelle, bool actif, int code) {
  const int h = 84;
  Dessin::texteTronque(F_TEXTE, MARGE, Dessin::baseCentree(F_TEXTE, y, h), ECRAN_L - 2 * MARGE - 116, libelle, NOIR);
  interrupteur(ECRAN_L - MARGE, y + h / 2, actif);
  separateur(y + h - 2);
  zone(boite(0, y, ECRAN_L, h), code);
}

void messageVide(int y, const char* ligne1, const char* ligne2) {
  Dessin::visagePamk(ECRAN_L / 2, y, 14, VISAGE_NORMAL, GRIS_FONCE);
  Dessin::texteCentre(F_GRAS, ECRAN_L / 2, y + 150, ligne1, NOIR);
  if (ligne2) Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, y + 192, ligne2, GRIS_FONCE);
}

void toast(const char* message) {
  const Boite b = boite(MARGE, 776, ECRAN_L - 2 * MARGE, 76);
  Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, NOIR);
  const int l = mini(Dessin::largeur(F_GRAS, message), b.l - 32);
  Dessin::texteTronque(F_GRAS, b.x + (b.l - l) / 2, Dessin::baseCentree(F_GRAS, b.y, b.h), b.l - 32, message, BLANC);
}

}  // namespace UI
