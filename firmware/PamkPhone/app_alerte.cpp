// Fin de cycle Pomodoro / minuteur, rappel d'une tâche (28/10/2026) : écran inversé (noir)
// bien visible, sans son en V0.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "parametres.h"
#include "stockage.h"

namespace {

enum Code { C_OK, C_DEMARRER, C_OUVRIR };

void boutonInverse(const Boite& b, const char* texte, int code, bool plein) {
  if (plein) {
    Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, BLANC);
    Dessin::texteCentre(F_GRAS, b.x + b.l / 2, Dessin::baseCentree(F_GRAS, b.y, b.h), texte, NOIR);
  } else {
    Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, TRAIT, BLANC);
    Dessin::texteCentre(F_GRAS, b.x + b.l / 2, Dessin::baseCentree(F_GRAS, b.y, b.h), texte, BLANC);
  }
  UI::zone(b, code);
}

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
};

AppAlerte g_alerte;

// ---------------------------------------------------------------------------
//  Rappel d'une tâche : titre, heure prévue, « Fait », « Dans 10 min », « Fermer ».
//  Un toucher à côté des boutons ne ferme pas : un rappel fermé ne revient pas.
// ---------------------------------------------------------------------------
enum CodeRappel { R_FAIT, R_REPORTER, R_FERMER };
const int REPORT_MIN = 10;

class AppRappel : public App {
 public:
  IdApp id() const override { return APP_ALERTE; }
  bool barreEtat() const override { return false; }
  bool reprendreApresVeille() const override { return false; }

  void configurer(const char* id) {
    strncpy(m_id, id ? id : "", sizeof(m_id) - 1);
    m_id[sizeof(m_id) - 1] = 0;
  }

  void dessiner() override {
    Dessin::rectPlein(0, 0, ECRAN_L, ECRAN_H, NOIR);
    Dessin::visagePamk(ECRAN_L / 2, 100, 18, VISAGE_NORMAL, BLANC);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 320, "Rappel", BLANC);
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (!t) {
      Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 400, "Cette tâche n'existe plus.", BLANC);
      boutonInverse(boite(MARGE, 800, ECRAN_L - 2 * MARGE, 84), "Fermer", R_FERMER, true);
      return;
    }
    // Titre de la tâche, centré, 3 lignes au plus
    const GFXfont* p = policeGras();
    const int il = Dessin::hauteurLigne(p) + 6;
    LigneTexte l[3];
    const int n = Dessin::couperLignes(p, t->titre, ECRAN_L - 2 * MARGE - 40, l, 3);
    int y = 390;
    for (int i = 0; i < n; i++) {
      const int lg = Dessin::largeurN(p, t->titre + l[i].debut, l[i].fin - l[i].debut);
      Dessin::texteN(p, (ECRAN_L - lg) / 2, y + i * il, t->titre + l[i].debut, l[i].fin - l[i].debut, BLANC);
    }
    y += maxi(0, n - 1) * il + 50;
    // Heure prévue (« Prévu à 14:30 », « Prévu hier à 23:00 »)
    char detail[64];
    if (t->heure >= 0) {
      char quand[24] = "";
      const int auj = Horloge::aujourdhui();
      if (t->echeance && t->echeance != auj) {
        Horloge::texteDateRelative(quand, sizeof(quand), t->echeance);
        snprintf(detail, sizeof(detail), "Prévu %s à %d:%02d", quand, t->heure / 60, t->heure % 60);
      } else {
        snprintf(detail, sizeof(detail), "Prévu à %d:%02d", t->heure / 60, t->heure % 60);
      }
    } else {
      snprintf(detail, sizeof(detail), "C'est l'heure !");
    }
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, y, detail, GRIS_CLAIR);
    boutonInverse(boite(MARGE, 600, ECRAN_L - 2 * MARGE, 84), "Fait", R_FAIT, true);
    if (!t->faite) {
      char r[24];
      snprintf(r, sizeof(r), "Dans %d min", REPORT_MIN);
      boutonInverse(boite(MARGE, 700, ECRAN_L - 2 * MARGE, 84), r, R_REPORTER, false);
    }
    boutonInverse(boite(MARGE, 800, ECRAN_L - 2 * MARGE, 84), "Fermer", R_FERMER, false);
  }

  void action(int code, int arg) override {
    (void)arg;
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    char message[48] = "";
    if (t && code == R_FAIT && !t->faite) {
      const bool repetee = t->repetition != REP_AUCUNE;
      Stockage::cocherTache(m_id, true);
      if (repetee) {   // elle reste à faire : son échéance a avancé
        char d[24];
        Horloge::texteDateRelative(d, sizeof(d), t->echeance);
        snprintf(message, sizeof(message), "Revient %s", d);
      }
    } else if (t && code == R_REPORTER) {
      if (Stockage::reporterRappel(m_id, REPORT_MIN)) snprintf(message, sizeof(message), "Rappel dans %d min", REPORT_MIN);
      else snprintf(message, sizeof(message), "Rappel impossible à reporter");
    }
    Systeme::retour();   // ferme le rappel dans tous les cas
    if (message[0]) Systeme::notifier(message);
  }

  bool bouton(Bouton b) override {
    (void)b;
    Systeme::retour();
    return true;
  }

 private:
  char m_id[ID_LONGUEUR + 1] = "";
};

AppRappel g_rappel;

}  // namespace

App* appAlerte(Alerte a) {
  g_alerte.configurer(a);
  return &g_alerte;
}

App* appAlerteRappel(const char* idTache) {
  g_rappel.configurer(idTache);
  return &g_rappel;
}
