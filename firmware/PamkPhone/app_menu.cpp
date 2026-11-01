// =============================================================================
//  Pamk Phone — Lanceur d'applications paginé (8 tuiles par page, 2 x 4)
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"
#include "minuteries.h"
#include "meteo.h"
#include "reseau.h"

namespace {

const int TUILE_L = (ECRAN_L - 3 * MARGE) / 2;   // 234
const int TUILE_H = 176;
const int TUILE_Y0 = CONTENU_Y + 12;
const int TUILE_ECART = 14;
const int TUILES_PAR_PAGE = 8;

struct DefTuile {
  const char* nom;
  Icone icone;
  IdApp app;
};

// Liste ordonnée de toutes les applications disponibles
const DefTuile TOUTES_TUILES[] = {
  // Page 1 : Les Essentiels (8 tuiles)
  {"Tâches", ICONE_TACHES, APP_TACHES},
  {"Notes", ICONE_NOTES, APP_NOTES},
  {"Minuteur", ICONE_POMODORO, APP_POMODORO},
  {"Calendrier", ICONE_CALENDRIER, APP_CALENDRIER},
  {"Calculatrice", ICONE_CALCULATRICE, APP_CALCULATRICE},
  {"Météo", ICONE_METEO, APP_METEO},
  {"Bilan", ICONE_BILAN, APP_BILAN},
  {"Réglages", ICONE_REGLAGES, APP_REGLAGES},

  // Page 2 : Outils & Bien-être
  {"Habitudes", ICONE_HABITUDES, APP_HABITUDES},
  {"Zen", ICONE_ZEN, APP_ZEN},
  {"Convertisseur", ICONE_CONVERTISSEUR, APP_CONVERTISSEUR},
  {"Recherche", ICONE_RECHERCHE, APP_RECHERCHE},
  {"Accès PC", ICONE_ACCES, APP_ACCES},
};

const int NB_TOTAL_TUILES = sizeof(TOUTES_TUILES) / sizeof(TOUTES_TUILES[0]);
const int NB_PAGES = (NB_TOTAL_TUILES + TUILES_PAR_PAGE - 1) / TUILES_PAR_PAGE;

class AppMenu : public App {
 public:
  IdApp id() const override { return APP_MENU; }

  void dessiner() override {
    UI::barreTitre("Applications", true);

    const int iDebut = m_page * TUILES_PAR_PAGE;
    const int iFin = mini(iDebut + TUILES_PAR_PAGE, NB_TOTAL_TUILES);

    for (int idx = iDebut; idx < iFin; idx++) {
      const int pos = idx - iDebut;
      const int col = pos % 2;
      const int ligne = pos / 2;
      const int x = MARGE + col * (TUILE_L + MARGE);
      const int y = TUILE_Y0 + ligne * (TUILE_H + TUILE_ECART);

      Dessin::chanfrein(x, y, TUILE_L, TUILE_H, 16, TRAIT, NOIR);
      Dessin::icone(TOUTES_TUILES[idx].icone, x + (TUILE_L - 60) / 2, y + 18, 5, NOIR);
      Dessin::texteCentre(F_GRAS, x + TUILE_L / 2, y + 116, TOUTES_TUILES[idx].nom, NOIR);

      char info[40];
      infoTuile(TOUTES_TUILES[idx].app, info, sizeof(info));
      Dessin::texteCentre(F_PETITE, x + TUILE_L / 2, y + 150, info, GRIS_FONCE);

      UI::zone(boite(x, y, TUILE_L, TUILE_H), (int)TOUTES_TUILES[idx].app);
    }

    // Puces de pagination en bas
    if (NB_PAGES > 1) {
      const int yPuces = ECRAN_H - 24;
      const int r = 6;
      for (int p = 0; p < NB_PAGES; p++) {
        const int cx = ECRAN_L / 2 + (p - (NB_PAGES - 1) / 2.0f) * 28;
        if (p == m_page) {
          Dessin::cerclePlein(cx, yPuces, r, NOIR);
        } else {
          Dessin::cercle(cx, yPuces, r, 2, GRIS);
        }
      }
      // Zones tactiles en bas gauche/droite pour changer de page sans glisser
      UI::zone(boite(0, ECRAN_H - 50, ECRAN_L / 2, 50), CODE_PAGE_PREC);
      UI::zone(boite(ECRAN_L / 2, ECRAN_H - 50, ECRAN_L / 2, 50), CODE_PAGE_SUIV);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code == CODE_PAGE_PREC) {
      if (m_page > 0) {
        m_page--;
        Systeme::redessiner();
      }
      return;
    }
    if (code == CODE_PAGE_SUIV) {
      if (m_page + 1 < NB_PAGES) {
        m_page++;
        Systeme::redessiner();
      }
      return;
    }

    App* a = appParId((uint8_t)code);
    if (a) Systeme::ouvrir(a);
  }

  void glisser(int dx, int dy) override {
    // Balayage horizontal : page précédente / suivante
    if (abs(dx) > abs(dy)) {
      if (dx < -35 && m_page + 1 < NB_PAGES) {
        m_page++;
        Systeme::redessiner();
      } else if (dx > 35 && m_page > 0) {
        m_page--;
        Systeme::redessiner();
      }
    }
  }

 private:
  int m_page = 0;

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
          snprintf(b, n, "et Pomodoro");
        }
        break;
      case APP_CALENDRIER:
        if (Horloge::valide()) Horloge::texteDateCourte(b, n, Horloge::aujourdhui());
        break;
      case APP_METEO:
        if (Meteo::disponible()) {
          snprintf(b, n, "%d°C · %s", Meteo::temperature(), Meteo::texteCondition(Meteo::codeWmo()));
        } else {
          snprintf(b, n, "locale (Wi-Fi)");
        }
        break;
      case APP_REGLAGES:
        snprintf(b, n, "Wi-Fi, heure, écran");
        break;
      case APP_BILAN:
        snprintf(b, n, "ta semaine");
        break;
      case APP_RECHERCHE:
        snprintf(b, n, "tâches et notes");
        break;
      case APP_ACCES:
        snprintf(b, n, "page web & OTA");
        break;
      case APP_HABITUDES:
        snprintf(b, n, "rituels du jour");
        break;
      case APP_ZEN:
        snprintf(b, n, "cohérence cardiaque");
        break;
      case APP_CONVERTISSEUR:
        snprintf(b, n, "unités & mesures");
        break;
      default:
        break;
    }
  }
};

AppMenu g_menu;

}  // namespace

App* appMenu() { return &g_menu; }
