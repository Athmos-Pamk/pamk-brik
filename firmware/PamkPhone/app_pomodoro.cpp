// Pomodoro (travail / pause / grande pause) et minuteur.
// Affichage à la minute près : l'e-paper n'est rafraîchi que quand le nombre change.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "parametres.h"
#include "minuteries.h"

namespace {

enum Code {
  C_ONGLET, C_DEMARRER, C_PAUSE, C_PASSER, C_ARRETER, C_REGLAGES,
  C_MIN_AJOUT, C_MIN_PRESET, C_MIN_DEMARRER, C_MIN_PAUSE, C_MIN_ARRETER,
};

const int ONGLETS_Y = CONTENU_Y + 10;
const int ONGLETS_H = 56;
const int PRESETS[9] = {1, 3, 5, 10, 15, 20, 30, 45, 60};
const int AJOUTS[4] = {-5, -1, 1, 5};

// Grand nombre de minutes : « 24 min », « <1 min »
void grandesMinutes(int secondes, int yBase) {
  char num[12];
  if (secondes > 0 && secondes < 60) snprintf(num, sizeof(num), "<1");
  else snprintf(num, sizeof(num), "%d", (secondes + 59) / 60);
  const char* unite = " min";
  const int l1 = Dessin::largeur(F_GRANDE, num), l2 = Dessin::largeur(F_TITRE, unite);
  const int x = (ECRAN_L - l1 - l2) / 2;
  Dessin::texte(F_GRANDE, x, yBase, num, NOIR);
  Dessin::texte(F_TITRE, x + l1, yBase, unite, GRIS_FONCE);
}

void heureFin(int dansSecondes, char* b, size_t n) {
  struct tm t;
  Horloge::localeDe(Horloge::maintenant() + (uint32_t)maxi(0, dansSecondes), t);
  char h[8];
  Horloge::texteHeure(h, sizeof(h), t);
  snprintf(b, n, "Fin à %s", h);
}

class AppPomodoro : public App {
 public:
  IdApp id() const override { return APP_POMODORO; }

  void entrer() override {
    // Si seul le minuteur tourne, on ouvre directement son onglet.
    if (Minuteries::minActif() && Minuteries::pomoPhase() == POMO_ARRET) m_onglet = 1;
  }

  void dessiner() override {
    UI::barreTitre(m_onglet ? "Minuteur" : "Pomodoro", true);
    const int lo = (ECRAN_L - 2 * MARGE - 12) / 2;
    UI::bouton(boite(MARGE, ONGLETS_Y, lo, ONGLETS_H), "Pomodoro", C_ONGLET, 0, m_onglet == 0 ? STYLE_PLEIN : STYLE_DISCRET);
    UI::bouton(boite(MARGE + lo + 12, ONGLETS_Y, lo, ONGLETS_H), "Minuteur", C_ONGLET, 1, m_onglet == 1 ? STYLE_PLEIN : STYLE_DISCRET);
    if (m_onglet == 0) dessinerPomodoro();
    else dessinerMinuteur();
    m_signature = signature();
  }

  void action(int code, int arg) override {
    switch (code) {
      case C_ONGLET: m_onglet = arg; break;
      case C_DEMARRER: Minuteries::pomoDemarrer(); break;
      case C_PAUSE: Minuteries::pomoPauseReprise(); break;
      case C_PASSER: Minuteries::pomoPasser(); break;
      case C_ARRETER: Minuteries::pomoArreter(); break;
      case C_REGLAGES: Systeme::ouvrir(appReglagesPage(PAGE_POMODORO)); return;
      case C_MIN_AJOUT: Param.minuteurMin = (uint8_t)borner(Param.minuteurMin + AJOUTS[arg], 1, 240); break;
      case C_MIN_PRESET: Param.minuteurMin = (uint8_t)PRESETS[arg]; break;
      case C_MIN_DEMARRER:
        sauverParametres();   // garde la dernière durée
        Minuteries::minDemarrer(Param.minuteurMin * 60);
        break;
      case C_MIN_PAUSE: Minuteries::minPauseReprise(); break;
      case C_MIN_ARRETER: Minuteries::minArreter(); break;
      default: break;
    }
    Systeme::redessiner();
  }

  bool bouton(Bouton b) override {
    if (m_onglet == 0) {
      if (b == BOUTON_PLUS) {
        if (Minuteries::pomoPhase() == POMO_ARRET) Minuteries::pomoDemarrer();
        else Minuteries::pomoPauseReprise();
      }
    } else if (Minuteries::minActif()) {
      if (b == BOUTON_PLUS) Minuteries::minPauseReprise();
    } else {
      Param.minuteurMin = (uint8_t)borner(Param.minuteurMin + (b == BOUTON_PLUS ? 1 : -1), 1, 240);
    }
    Systeme::redessiner();
    return true;
  }

  void glisser(int dx, int dy) override {
    if (abs(dx) > abs(dy)) {   // glisser à gauche / à droite : change d'onglet
      m_onglet = dx < 0 ? 1 : 0;
      Systeme::redessiner();
    }
  }

  void seconde() override {
    if (signature() != m_signature) Systeme::redessiner();
  }

 private:
  int m_onglet = 0;
  long m_signature = -1;

  // Ce qui est affiché ; un changement demande un rafraîchissement.
  long signature() {
    long s = m_onglet;
    s = s * 8 + Minuteries::pomoPhase();
    s = s * 2 + (Minuteries::pomoSuspendu() ? 1 : 0);
    s = s * 2 + (Minuteries::minActif() ? 1 : 0);
    s = s * 2 + (Minuteries::minSuspendu() ? 1 : 0);
    const int r = m_onglet == 0 ? Minuteries::pomoRestantS() : Minuteries::minRestantS();
    s = s * 1000 + (r > 0 && r < 60 ? 999 : (r + 59) / 60);
    return s;
  }

  void dessinerPomodoro() {
    const PhasePomo ph = Minuteries::pomoPhase();
    const bool enCours = ph != POMO_ARRET;
    const char* etiquette = "PRÊT";
    if (ph == POMO_TRAVAIL) etiquette = "TRAVAIL";
    else if (ph == POMO_PAUSE) etiquette = "PAUSE";
    else if (ph == POMO_LONGUE) etiquette = "GRANDE PAUSE";
    Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 270, etiquette, GRIS_FONCE);
    const int restant = enCours ? Minuteries::pomoRestantS() : Param.pomoTravail * 60;
    grandesMinutes(restant, 400);
    if (enCours) {
      const int duree = maxi(1, Minuteries::pomoDureeS());
      Dessin::barreProgression(boite(MARGE, 430, ECRAN_L - 2 * MARGE, 16),
                               (int)((long)(duree - restant) * 1000 / duree), GRIS_CLAIR, NOIR);
    }
    // Cycles de la série
    const int nbCycles = mini(Param.pomoCycles, 8);
    int faits = Minuteries::pomoCyclesFaits() % maxi(1, Param.pomoCycles);
    if (Minuteries::pomoCyclesFaits() > 0 && faits == 0 && ph == POMO_LONGUE) faits = Param.pomoCycles;
    const int c = 28, e = 14;
    const int x0 = (ECRAN_L - (nbCycles * c + (nbCycles - 1) * e)) / 2;
    for (int i = 0; i < nbCycles; i++) {
      if (i < faits) Dessin::rectPlein(x0 + i * (c + e), 476, c, c, NOIR);
      else Dessin::rect(x0 + i * (c + e), 476, c, c, 3, NOIR);
    }
    char t[64];
    snprintf(t, sizeof(t), "%d pomodoro%s terminé%s", Minuteries::pomoCyclesFaits(),
             Minuteries::pomoCyclesFaits() > 1 ? "s" : "", Minuteries::pomoCyclesFaits() > 1 ? "s" : "");
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 540, t, GRIS_FONCE);
    if (enCours) {
      if (Minuteries::pomoSuspendu()) strcpy(t, "En pause");
      else heureFin(restant, t, sizeof(t));
      Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 592, t, NOIR);
    }
    // Boutons
    const int lb = (ECRAN_L - 2 * MARGE - 16) / 2;
    if (!enCours) {
      UI::bouton(boite(MARGE, 630, ECRAN_L - 2 * MARGE, 84), "Démarrer", C_DEMARRER, 0, STYLE_PLEIN);
    } else {
      UI::bouton(boite(MARGE, 630, lb, 84), Minuteries::pomoSuspendu() ? "Reprendre" : "Pause", C_PAUSE, 0, STYLE_PLEIN);
      UI::bouton(boite(MARGE + lb + 16, 630, lb, 84), "Passer", C_PASSER);
      UI::bouton(boite(MARGE, 730, ECRAN_L - 2 * MARGE, 76), "Arrêter la série", C_ARRETER, 0, STYLE_DISCRET);
    }
    snprintf(t, sizeof(t), "%u min · pause %u · grande pause %u · %u cycles",
             (unsigned)Param.pomoTravail, (unsigned)Param.pomoPause, (unsigned)Param.pomoLongue, (unsigned)Param.pomoCycles);
    UI::separateur(842);
    Dessin::texteTronque(F_PETITE, MARGE, 890, ECRAN_L - 2 * MARGE - 30, t, GRIS_FONCE);
    Dessin::icone(ICONE_CHEVRON_D, ECRAN_L - MARGE - 15, 868, 3, GRIS);
    UI::zone(boite(0, 846, ECRAN_L, 80), C_REGLAGES);
  }

  void dessinerMinuteur() {
    char t[48];
    if (Minuteries::minActif()) {
      Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 270, Minuteries::minSuspendu() ? "EN PAUSE" : "EN COURS", GRIS_FONCE);
      const int restant = Minuteries::minRestantS();
      grandesMinutes(restant, 400);
      const int duree = maxi(1, Minuteries::minDureeS());
      Dessin::barreProgression(boite(MARGE, 430, ECRAN_L - 2 * MARGE, 16),
                               (int)((long)(duree - restant) * 1000 / duree), GRIS_CLAIR, NOIR);
      if (!Minuteries::minSuspendu()) {
        heureFin(restant, t, sizeof(t));
        Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 500, t, NOIR);
      }
      const int lb = (ECRAN_L - 2 * MARGE - 16) / 2;
      UI::bouton(boite(MARGE, 630, lb, 84), Minuteries::minSuspendu() ? "Reprendre" : "Pause", C_MIN_PAUSE, 0, STYLE_PLEIN);
      UI::bouton(boite(MARGE + lb + 16, 630, lb, 84), "Arrêter", C_MIN_ARRETER);
      return;
    }
    Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 250, "DURÉE", GRIS_FONCE);
    grandesMinutes(Param.minuteurMin * 60, 360);
    static const char* const LIB_AJOUTS[4] = {"- 5", "- 1", "+ 1", "+ 5"};
    const int la = (ECRAN_L - 2 * MARGE - 3 * 12) / 4;
    for (int i = 0; i < 4; i++) {
      UI::bouton(boite(MARGE + i * (la + 12), 392, la, 64), LIB_AJOUTS[i], C_MIN_AJOUT, i, STYLE_DISCRET);
    }
    const int lp = (ECRAN_L - 2 * MARGE - 2 * 16) / 3;
    for (int i = 0; i < 9; i++) {
      snprintf(t, sizeof(t), "%d min", PRESETS[i]);
      UI::bouton(boite(MARGE + (i % 3) * (lp + 16), 480 + (i / 3) * 80, lp, 64), t, C_MIN_PRESET, i,
                 Param.minuteurMin == PRESETS[i] ? STYLE_PLEIN : STYLE_CONTOUR);
    }
    UI::bouton(boite(MARGE, 740, ECRAN_L - 2 * MARGE, 84), "Démarrer le minuteur", C_MIN_DEMARRER, 0, STYLE_PLEIN);
  }
};

AppPomodoro g_pomodoro;

}  // namespace

App* appPomodoro() { return &g_pomodoro; }
