// Lanceur : 6 grandes tuiles (2 x 3) avec icônes pixel.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"
#include "minuteries.h"

namespace {

const int TUILE_L = (ECRAN_L - 3 * MARGE) / 2;   // 234
const int TUILE_H = 220;
const int TUILE_Y0 = CONTENU_Y + 24;
const int TUILE_ECART = 20;

struct DefTuile {
  const char* nom;
  Icone icone;
  IdApp app;
};
const DefTuile TUILES[6] = {
  {"Tâches", ICONE_TACHES, APP_TACHES},
  {"Notes", ICONE_NOTES, APP_NOTES},
  {"Pomodoro", ICONE_POMODORO, APP_POMODORO},
  {"Calendrier", ICONE_CALENDRIER, APP_CALENDRIER},
  {"Calculatrice", ICONE_CALCULATRICE, APP_CALCULATRICE},
  {"Réglages", ICONE_REGLAGES, APP_REGLAGES},
};

class AppMenu : public App {
 public:
  IdApp id() const override { return APP_MENU; }

  void dessiner() override {
    UI::barreTitre("Applications", true);
    for (int i = 0; i < 6; i++) {
      const int x = MARGE + (i % 2) * (TUILE_L + MARGE);
      const int y = TUILE_Y0 + (i / 2) * (TUILE_H + TUILE_ECART);
      Dessin::chanfrein(x, y, TUILE_L, TUILE_H, 16, TRAIT, NOIR);
      Dessin::icone(TUILES[i].icone, x + (TUILE_L - 72) / 2, y + 28, 6, NOIR);
      Dessin::texteCentre(F_GRAS, x + TUILE_L / 2, y + 150, TUILES[i].nom, NOIR);
      char info[40];
      infoTuile(TUILES[i].app, info, sizeof(info));
      Dessin::texteCentre(F_PETITE, x + TUILE_L / 2, y + 188, info, GRIS_FONCE);
      UI::zone(boite(x, y, TUILE_L, TUILE_H), (int)TUILES[i].app);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    App* a = appParId((uint8_t)code);
    if (a) Systeme::ouvrir(a);
  }

 private:
  static void infoTuile(IdApp app, char* b, size_t n) {
    b[0] = 0;
    switch (app) {
      case APP_TACHES: {
        const int k = Stockage::nbTachesAFaire();
        if (k == 0) snprintf(b, n, "tout est fait");
        else snprintf(b, n, "%d à faire", k);
        break;
      }
      case APP_NOTES: {
        const int k = Stockage::nbNotes();
        snprintf(b, n, "%d note%s", k, k > 1 ? "s" : "");
        break;
      }
      case APP_POMODORO:
        if (Minuteries::pomoPhase() != POMO_ARRET) {
          char r[16];
          Minuteries::texteRestant(r, sizeof(r), Minuteries::pomoRestantS());
          snprintf(b, n, "en cours · %s", r);
        } else if (Minuteries::minActif()) {
          snprintf(b, n, "minuteur en cours");
        } else {
          snprintf(b, n, "et minuteur");
        }
        break;
      case APP_CALENDRIER:
        if (Horloge::valide()) Horloge::texteDateCourte(b, n, Horloge::aujourdhui());
        break;
      case APP_REGLAGES:
        snprintf(b, n, "Wi-Fi, heure, écran");
        break;
      default:
        break;
    }
  }
};

AppMenu g_menu;

}  // namespace

App* appMenu() { return &g_menu; }
