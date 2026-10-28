// Fin de cycle Pomodoro / minuteur : écran inversé (noir) bien visible, sans son en V0.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "parametres.h"

namespace {

enum Code { C_OK, C_DEMARRER, C_OUVRIR };

class AppAlerte : public App {
 public:
  IdApp id() const override { return APP_ALERTE; }
  bool barreEtat() const override { return false; }
  bool reprendreApresVeille() const override { return false; }

  void configurer(Alerte a) { m_alerte = a; }

  void dessiner() override {
    Dessin::rectPlein(0, 0, ECRAN_L, ECRAN_H, NOIR);
    Dessin::visagePamk(ECRAN_L / 2, 150, 22, VISAGE_CONTENT, BLANC);
    const char* titre = "C'est l'heure !";
    char detail[96] = "";
    switch (m_alerte) {
      case ALERTE_FIN_TRAVAIL: {
        titre = "Pause !";
        char fin[8];
        struct tm t;
        Horloge::localeDe(Horloge::maintenant() + (uint32_t)Minuteries::pomoRestantS(), t);
        Horloge::texteHeure(fin, sizeof(fin), t);
        snprintf(detail, sizeof(detail), "Pomodoro %d terminé. %s jusqu'à %s.", Minuteries::pomoCyclesFaits(),
                 Minuteries::nomPhase(Minuteries::pomoPhase()), fin);
        break;
      }
      case ALERTE_FIN_PAUSE:
        titre = "Au travail ?";
        snprintf(detail, sizeof(detail), "La pause est finie. Prêt pour %u min ?", (unsigned)Param.pomoTravail);
        break;
      case ALERTE_FIN_MINUTEUR: {
        titre = "Minuteur fini";
        const int m = Minuteries::minDureeS() / 60, s = Minuteries::minDureeS() % 60;
        if (s) snprintf(detail, sizeof(detail), "%d min %02d s écoulées.", m, s);
        else snprintf(detail, sizeof(detail), "%d min écoulée%s.", m, m > 1 ? "s" : "");
        break;
      }
      default:
        break;
    }
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 470, titre, BLANC);
    LigneTexte l[3];
    const int n = Dessin::couperLignes(F_TEXTE, detail, ECRAN_L - 2 * MARGE - 40, l, 3);
    for (int i = 0; i < n; i++) {
      const int largeur = Dessin::largeurN(F_TEXTE, detail + l[i].debut, l[i].fin - l[i].debut);
      Dessin::texteN(F_TEXTE, (ECRAN_L - largeur) / 2, 540 + i * 40, detail + l[i].debut, l[i].fin - l[i].debut, BLANC);
    }
    if (m_alerte == ALERTE_FIN_PAUSE) {
      boutonInverse(boite(MARGE, 700, ECRAN_L - 2 * MARGE, 84), "Démarrer un pomodoro", C_DEMARRER, true);
      boutonInverse(boite(MARGE, 808, ECRAN_L - 2 * MARGE, 84), "Plus tard", C_OK, false);
    } else {
      boutonInverse(boite(MARGE, 700, ECRAN_L - 2 * MARGE, 84), "OK", C_OK, true);
      boutonInverse(boite(MARGE, 808, ECRAN_L - 2 * MARGE, 84), "Voir le Pomodoro", C_OUVRIR, false);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code == C_DEMARRER) {
      Minuteries::pomoDemarrer();
      Systeme::remplacer(appPomodoro());
    } else if (code == C_OUVRIR) {
      Systeme::remplacer(appPomodoro());
    } else {
      Systeme::retour();
    }
  }

  void toucherLibre(int x, int y) override {
    (void)x; (void)y;
    Systeme::retour();   // toucher n'importe où ferme l'alerte
  }

  bool bouton(Bouton b) override {
    (void)b;
    Systeme::retour();
    return true;
  }

 private:
  Alerte m_alerte = ALERTE_AUCUNE;

  static void boutonInverse(const Boite& b, const char* texte, int code, bool plein) {
    if (plein) {
      Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, BLANC);
      Dessin::texteCentre(F_GRAS, b.x + b.l / 2, Dessin::baseCentree(F_GRAS, b.y, b.h), texte, NOIR);
    } else {
      Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, TRAIT, BLANC);
      Dessin::texteCentre(F_GRAS, b.x + b.l / 2, Dessin::baseCentree(F_GRAS, b.y, b.h), texte, BLANC);
    }
    UI::zone(b, code);
  }
};

AppAlerte g_alerte;

}  // namespace

App* appAlerte(Alerte a) {
  g_alerte.configurer(a);
  return &g_alerte;
}
