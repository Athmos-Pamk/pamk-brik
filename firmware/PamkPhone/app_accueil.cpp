// Accueil : grande horloge, date, Pamk, prochaines tâches, Pomodoro, accès aux applis.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"
#include "parametres.h"
#include "minuteries.h"

namespace {

enum Code { C_HORLOGE, C_COCHER, C_TACHE, C_TOUT_VOIR, C_POMODORO, C_POMO_DEMARRER, C_APPLIS };

const int Y_HORLOGE = 72;         // haut de la zone horloge
const int Y_TACHES = 432;         // titre de section « À faire »
const int H_TACHE = 72;
const int NB_TACHES = 3;
const int Y_POMO = 740;           // ligne de base de l'étiquette « Pomodoro »

class AppAccueil : public App {
 public:
  IdApp id() const override { return APP_ACCUEIL; }
  bool afficherHeure() const override { return false; }   // l'heure est en grand

  void entrer() override {}

  void dessiner() override {
    struct tm t;
    Horloge::locale(t);
    // Horloge
    char h[8];
    if (Horloge::valide()) Horloge::texteHeure(h, sizeof(h), t);
    else strcpy(h, "--:--");
    Dessin::texteCentre(F_HORLOGE, ECRAN_L / 2, Y_HORLOGE + 150, h, NOIR);
    char date[48];
    if (Horloge::valide()) {
      Horloge::texteDateLongue(date, sizeof(date), Horloge::dateDe(t), false);
      date[0] = (char)toupper((unsigned char)date[0]);
    } else {
      // Jamais synchronisée (premier démarrage, RTC d'usine) : on le dit au lieu d'afficher
      // une heure fausse. Toucher ici ouvre Réglages > Wi-Fi.
      strcpy(date, "Heure à régler : connecte le Wi-Fi");
    }
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, Y_HORLOGE + 206, date, NOIR);
    UI::zone(boite(0, Y_HORLOGE, ECRAN_L, 230), C_HORLOGE);

    // Pamk et salutation
    const int y = Y_HORLOGE + 248;
    Dessin::visagePamk(MARGE + 48, y, 8, VISAGE_NORMAL, NOIR);
    char salut[64];
    const char* bonjour = "Bonjour";
    if (t.tm_hour >= 18 || t.tm_hour < 5) bonjour = "Bonsoir";
    else if (t.tm_hour >= 12) bonjour = "Bon après-midi";
    if (Param.prenom[0]) snprintf(salut, sizeof(salut), "%s %s", bonjour, Param.prenom);
    else snprintf(salut, sizeof(salut), "%s !", bonjour);
    Dessin::texteTronque(F_GRAS, MARGE + 116, y + 26, ECRAN_L - MARGE * 2 - 116, salut, NOIR);
    char resume[64];
    const int aFaire = Stockage::nbTachesAFaire();
    const int retard = Stockage::nbTachesEnRetard();
    if (aFaire == 0) snprintf(resume, sizeof(resume), "Rien à faire pour l'instant");
    else if (retard > 0) snprintf(resume, sizeof(resume), "%d à faire, dont %d en retard", aFaire, retard);
    else snprintf(resume, sizeof(resume), "%d tâche%s à faire", aFaire, aFaire > 1 ? "s" : "");
    Dessin::texte(F_PETITE, MARGE + 116, y + 60, resume, GRIS_FONCE);
    UI::separateur(Y_TACHES - 32);

    // Prochaines tâches
    UI::etiquette(MARGE, Y_TACHES + 8, "À FAIRE");
    Dessin::texteDroite(F_GRAS, ECRAN_L - MARGE, Y_TACHES + 4, "Tout voir", NOIR);
    UI::zone(boite(ECRAN_L - 200, Y_TACHES - 30, 200, 52), C_TOUT_VOIR);
    int idx[NB_TACHES];
    const int n = Stockage::listeTaches(TACHES_A_FAIRE, idx, NB_TACHES);
    int yy = Y_TACHES + 24;
    if (n == 0) {
      Dessin::texte(F_TEXTE, MARGE, yy + 46, "Rien à faire. Profite !", GRIS_FONCE);
    }
    for (int i = 0; i < n; i++) {
      const Tache* ta = Stockage::tache(idx[i]);
      UI::caseACocher(MARGE, yy + (H_TACHE - CASE_TAILLE) / 2, false);
      UI::zone(boite(0, yy, 84, H_TACHE), C_COCHER, idx[i]);
      int largeurDate = 0;
      if (ta->echeance) {
        char e[24];
        Horloge::texteDateRelative(e, sizeof(e), ta->echeance);
        const bool retardee = ta->echeance < Horloge::aujourdhui();
        largeurDate = Dessin::largeur(F_PETITE, e) + 12;
        Dessin::texteDroite(F_PETITE, ECRAN_L - MARGE, yy + 46, e, retardee ? NOIR : GRIS_FONCE);
      }
      Dessin::texteTronque(F_TEXTE, MARGE + 60, yy + 46, ECRAN_L - 2 * MARGE - 60 - largeurDate, ta->titre, NOIR);
      UI::zone(boite(84, yy, ECRAN_L - 84, H_TACHE), C_TACHE, idx[i]);
      yy += H_TACHE;
    }
    UI::separateur(Y_POMO - 52);

    // Pomodoro / minuteur
    UI::etiquette(MARGE, Y_POMO, "POMODORO");
    const PhasePomo ph = Minuteries::pomoPhase();
    char ligne[64];
    if (ph != POMO_ARRET) {
      char r[16];
      Minuteries::texteRestant(r, sizeof(r), Minuteries::pomoRestantS());
      snprintf(ligne, sizeof(ligne), "%s · %s%s", Minuteries::nomPhase(ph), r,
               Minuteries::pomoSuspendu() ? " (en pause)" : "");
      Dessin::texte(F_GRAS, MARGE, Y_POMO + 50, ligne, NOIR);
      const int duree = maxi(1, Minuteries::pomoDureeS());
      const int fait = duree - Minuteries::pomoRestantS();
      Dessin::barreProgression(boite(MARGE, Y_POMO + 70, ECRAN_L - 2 * MARGE, 14),
                               (int)((long)fait * 1000 / duree), GRIS_CLAIR, NOIR);
    } else if (Minuteries::minActif()) {
      char r[16];
      Minuteries::texteRestant(r, sizeof(r), Minuteries::minRestantS());
      snprintf(ligne, sizeof(ligne), "Minuteur · %s", r);
      Dessin::texte(F_GRAS, MARGE, Y_POMO + 50, ligne, NOIR);
    } else {
      snprintf(ligne, sizeof(ligne), "Prêt : %u min de concentration", (unsigned)Param.pomoTravail);
      Dessin::texte(F_TEXTE, MARGE, Y_POMO + 50, ligne, GRIS_FONCE);
      UI::bouton(boite(ECRAN_L - MARGE - 150, Y_POMO - 36, 150, 56), "Démarrer", C_POMO_DEMARRER);
    }
    UI::zone(boite(0, Y_POMO - 40, ECRAN_L - MARGE - 160, 132), C_POMODORO);

    // Applications
    UI::bouton(boite(MARGE, 860, ECRAN_L - 2 * MARGE, 76), "Applications", C_APPLIS, 0, STYLE_PLEIN);
  }

  void action(int code, int arg) override {
    switch (code) {
      case C_HORLOGE:
        if (Horloge::valide()) Systeme::ouvrir(appCalendrier());
        else Systeme::ouvrir(appReglagesPage(PAGE_WIFI));
        break;
      case C_TOUT_VOIR: Systeme::ouvrir(appTaches()); break;
      case C_POMODORO:  Systeme::ouvrir(appPomodoro()); break;
      case C_APPLIS:    Systeme::ouvrir(appMenu()); break;
      case C_POMO_DEMARRER:
        Minuteries::pomoDemarrer();
        Systeme::redessiner();
        break;
      case C_COCHER: {
        const Tache* t = Stockage::tache(arg);
        if (t) Stockage::cocherTache(t->id, true);
        Systeme::redessiner();
        break;
      }
      case C_TACHE: {
        const Tache* t = Stockage::tache(arg);
        if (t) Systeme::ouvrir(appTache(t->id));
        break;
      }
      default: break;
    }
  }

  bool bouton(Bouton b) override {
    if (b == BOUTON_PLUS) Systeme::ouvrir(appMenu());
    else Systeme::ouvrir(appTaches());
    return true;
  }

  bool retour() override { return true; }   // déjà tout en bas de la pile

};

AppAccueil g_accueil;

}  // namespace

App* appAccueil() { return &g_accueil; }
