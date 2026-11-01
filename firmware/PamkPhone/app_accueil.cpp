// Accueil : grande horloge, date, Pamk, prochain rendez-vous de l'agenda, tâches
// d'aujourd'hui (capture rapide avec « + »), Pomodoro, accès aux applis.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"
#include "parametres.h"
#include "minuteries.h"
#include "agenda.h"
#include "meteo.h"
#include "stats.h"

namespace {

enum Code {
  C_HORLOGE, C_COCHER, C_TACHE, C_TOUT_VOIR, C_POMODORO, C_POMO_DEMARRER, C_APPLIS,
  C_AJOUTER, C_AGENDA,
};

// Mise en page (28/10/2026), de haut en bas sous la barre d'état (0..47) :
const int Y_HORLOGE = 182;        // ligne de base de l'horloge (chiffres de 112 px : 70..182)
const int Y_DATE = 226;           // ligne de base de la date
const int Y_PAMK = 260;           // haut du bloc Pamk + salutation (bien aéré sous la date)
const int Y_SUITE = 330;          // rendez-vous éventuel, puis la section « Aujourd'hui »
const int RDV_H = 48;             // ligne « prochain rendez-vous »
const int SECTION_H = 64;         // titre « AUJOURD'HUI », « Tout voir » et bouton +
const int H_TACHE = 64;
const int NB_TACHES_MAX = 5;      // 5 lignes sans rendez-vous, 4 avec
const int Y_POMO = 760;           // ligne de base de l'étiquette « Pomodoro »
const int Y_FIN_TACHES = Y_POMO - 54;   // 706 : les lignes de tâches s'arrêtent avant
const int Y_APPLIS = 860;

// Heure d'une tâche ou d'un rendez-vous : « 9:00 », « 14:30 ».
void texteMinutes(int minutes, char* b, size_t n) { snprintf(b, n, "%d:%02d", minutes / 60, minutes % 60); }

void rappelCapture(bool ok, const char* texte, void* ctx) {
  (void)ctx;
  if (!ok || !texte) return;
  const char* p = texte;
  while (*p == ' ') p++;
  if (!*p) return;
  // Capture rapide : la tâche est pour aujourd'hui (sans date si l'heure n'est pas réglée).
  if (!Stockage::ajouterTache(texte, Horloge::valide() ? Horloge::aujourdhui() : 0)) {
    Systeme::notifier("Impossible d'ajouter la tâche");
  }
}

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
    Dessin::texteCentre(F_HORLOGE, ECRAN_L / 2, Y_HORLOGE, h, NOIR);
    char date[48];
    if (Horloge::valide()) {
      Horloge::texteDateLongue(date, sizeof(date), Horloge::dateDe(t), false);
      date[0] = (char)toupper((unsigned char)date[0]);
      if (Meteo::disponible()) {
        const size_t len = strlen(date);
        snprintf(date + len, sizeof(date) - len, " · %d°C", Meteo::temperature());
      }
    } else {
      // Jamais synchronisée (premier démarrage, RTC d'usine) : on le dit au lieu d'afficher
      // une heure fausse. Toucher ici ouvre Réglages > Wi-Fi.
      strcpy(date, "Heure à régler : connecte le Wi-Fi");
    }
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, Y_DATE, date, NOIR);
    UI::zone(boite(0, BARRE_ETAT_H + 4, ECRAN_L, Y_DATE + 14 - BARRE_ETAT_H - 4), C_HORLOGE);

    // Pamk et salutation (centrés horizontalement au milieu de l'écran)
    char salut[64];
    const char* bonjour = "Bonjour";
    if (t.tm_hour >= 18 || t.tm_hour < 5) bonjour = "Bonsoir";
    else if (t.tm_hour >= 12) bonjour = "Bon après-midi";
    if (Param.prenom[0]) snprintf(salut, sizeof(salut), "%s %s", bonjour, Param.prenom);
    else snprintf(salut, sizeof(salut), "%s !", bonjour);

    char resume[64];
    const int aFaire = Stockage::nbTachesAFaire();
    const int retard = Stockage::nbTachesEnRetard();
    if (aFaire == 0) snprintf(resume, sizeof(resume), "Rien à faire pour l'instant");
    else if (retard > 0) snprintf(resume, sizeof(resume), "%d à faire, dont %d en retard", aFaire, retard);
    else snprintf(resume, sizeof(resume), "%d tâche%s à faire", aFaire, aFaire > 1 ? "s" : "");

    const int pPamk = 7;
    const int wPamk = 11 * pPamk;
    const int ecart = 16;
    const int lTxt = mini(ECRAN_L - 2 * MARGE - wPamk - ecart, maxi(Dessin::largeur(F_GRAS, salut), Dessin::largeur(F_PETITE, resume)));
    const int lTotal = wPamk + ecart + lTxt;
    const int xBloc = (ECRAN_L - lTotal) / 2;

    Dessin::visagePamk(xBloc + wPamk / 2, Y_PAMK + 4, pPamk, VISAGE_NORMAL, NOIR);
    Dessin::texteTronque(F_GRAS, xBloc + wPamk + ecart, Y_PAMK + 26, ECRAN_L - MARGE - (xBloc + wPamk + ecart), salut, NOIR);
    Dessin::texteTronque(F_PETITE, xBloc + wPamk + ecart, Y_PAMK + 54, ECRAN_L - MARGE - (xBloc + wPamk + ecart), resume, GRIS_FONCE);

    // Prochain rendez-vous (agenda en lecture seule), s'il y en a un
    int y = Y_SUITE;
    if (ligneRendezVous(y)) y += RDV_H;

    // Aujourd'hui
    UI::separateur(y + 2);
    dessinerTaches(y + 4);

    // Pomodoro / minuteur
    UI::separateur(Y_POMO - 52);
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
    UI::zone(boite(0, Y_POMO - 40, ECRAN_L - MARGE - 160, Y_APPLIS - 8 - (Y_POMO - 40)), C_POMODORO);

    // Applications
    UI::bouton(boite(MARGE, Y_APPLIS, ECRAN_L - 2 * MARGE, 76), "Applications", C_APPLIS, 0, STYLE_PLEIN);
  }

  void action(int code, int arg) override {
    switch (code) {
      case C_HORLOGE:
        if (Horloge::valide()) Systeme::ouvrir(appCalendrier());
        else Systeme::ouvrir(appReglagesPage(PAGE_WIFI));
        break;
      case C_AGENDA:    Systeme::ouvrir(appCalendrier()); break;
      case C_TOUT_VOIR: Systeme::ouvrir(appTaches()); break;
      case C_POMODORO:  Systeme::ouvrir(appPomodoro()); break;
      case C_APPLIS:    Systeme::ouvrir(appMenu()); break;
      case C_AJOUTER:
        Systeme::ouvrir(appClavier("Pour aujourd'hui", "", TACHE_TITRE_MAX, SAISIE_LIGNE, rappelCapture, nullptr));
        break;
      case C_POMO_DEMARRER:
        Minuteries::pomoDemarrer();
        Systeme::redessiner();
        break;
      case C_COCHER: {
        const Tache* t = Stockage::tache(arg);
        if (!t) break;
        const bool repetee = t->repetition != REP_AUCUNE;
        Stockage::cocherTache(t->id, true);
        if (repetee) {   // elle ne disparaît pas : son échéance a avancé
          char d[24], m[48];
          Horloge::texteDateRelative(d, sizeof(d), t->echeance);
          snprintf(m, sizeof(m), "Revient %s", d);
          Systeme::notifier(m);
        }
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

 private:
  // « 14:30 », « En cours », « Demain 9:00 », « Lun. 2 nov. »…
  static void texteQuand(const Evenement& e, char* b, size_t n) {
    const int auj = Horloge::aujourdhui();
    char hm[8] = "";
    if (e.debut >= 0) texteMinutes(e.debut, hm, sizeof(hm));
    if (e.date == auj) {
      struct tm t;
      Horloge::locale(t);
      const int mn = t.tm_hour * 60 + t.tm_min;
      if (e.debut < 0) snprintf(b, n, "Aujourd'hui");
      else if (mn >= e.debut && e.fin > e.debut && mn < e.fin) snprintf(b, n, "En cours");
      else snprintf(b, n, "%s", hm);
      return;
    }
    char d[24];
    Horloge::texteDateRelative(d, sizeof(d), e.date);
    d[0] = (char)toupper((unsigned char)d[0]);
    if (hm[0]) snprintf(b, n, "%s %s", d, hm);
    else snprintf(b, n, "%s", d);
  }

  // Une ligne : icône, quand, titre. false (rien dessiné) sans agenda ou sans rendez-vous.
  bool ligneRendezVous(int y) {
    if (!Agenda::configure()) return false;
    Evenement e;
    if (!Agenda::prochainEvenement(e)) return false;
    char quand[40];
    texteQuand(e, quand, sizeof(quand));
    Dessin::icone(ICONE_CALENDRIER, MARGE, y + 12, 2, GRIS_FONCE);   // 24 x 24
    const int base = y + 32;
    const int x = Dessin::texte(F_GRAS, MARGE + 38, base, quand, NOIR) + 12;
    Dessin::texteTronque(F_TEXTE, x, base, ECRAN_L - MARGE - x, e.titre, NOIR);
    UI::zone(boite(0, y, ECRAN_L, RDV_H), C_AGENDA);
    return true;
  }

  // Section « Aujourd'hui » : en retard puis du jour (avec l'heure). S'il reste de la place,
  // les suivantes (plus tard, date en gris, ou sans date) sous un pointillé.
  // 01/11/2026 (idée reprise de FolloUp) : progression du jour, « 2/5 » à côté du titre et
  // une barre noire sur le trait du dessus, qui se remplit à chaque tâche cochée.
  void dessinerTaches(int y) {
    UI::etiquette(MARGE, y + 48, "AUJOURD'HUI");
    // Reste à faire aujourd'hui : en retard, du jour, et sans date (rien ne dit qu'elles
    // attendront) ; les tâches prévues un autre jour n'en font pas partie.
    JourStats jour;
    Stats::derniersJours(&jour, 1);
    const int faites = jour.taches;
    int tmp[50];
    int reste = Stockage::tachesAujourdhui(tmp, 50);
    for (int i = 0; i < Stockage::nbTaches(); i++) {
      const Tache* ta = Stockage::tache(i);
      if (ta && !ta->faite && !ta->echeance) reste++;
    }
    const int total = faites + reste;
    if (total > 0) {
      char prog[16];
      snprintf(prog, sizeof(prog), "%d/%d", faites, total);
      // police normale : « 12/15 » en pixel (20 px par caractère) toucherait « Tout voir »
      Dessin::texte(F_PETITE, MARGE + Dessin::largeur(F_PIXEL, "AUJOURD'HUI") + 12, y + 44, prog, NOIR);
      Dessin::rectPlein(MARGE, y - 4, (int)((long)(ECRAN_L - 2 * MARGE) * faites / total), 4, NOIR);
    }
    const Boite plus = boite(ECRAN_L - MARGE - 64, y + 8, 64, 50);
    Dessin::chanfrein(plus.x, plus.y, plus.l, plus.h, CHANFREIN, TRAIT, NOIR);
    Dessin::pictoPlus(plus.x + plus.l / 2, plus.y + plus.h / 2, 26, 5, NOIR);
    UI::zone(boite(plus.x - 8, y, plus.l + 8 + MARGE, SECTION_H), C_AJOUTER);
    const int xTv = plus.x - 20;
    const int lTv = Dessin::largeur(F_GRAS, "Tout voir");
    Dessin::texteDroite(F_GRAS, xTv, y + 44, "Tout voir", NOIR);
    UI::zone(boite(xTv - lTv - 12, y + 4, lTv + 24, SECTION_H - 8), C_TOUT_VOIR);

    int yy = y + SECTION_H;
    const int nb = mini(NB_TACHES_MAX, (Y_FIN_TACHES - yy) / H_TACHE);
    int idx[NB_TACHES_MAX];
    const int nAuj = Stockage::tachesAujourdhui(idx, nb);
    // Les tâches à faire triées commencent par les mêmes (retard et jour, même ordre) :
    // on complète avec la suite.
    int n = nAuj;
    if (nAuj < nb) n = Stockage::listeTaches(TACHES_A_FAIRE, idx, nb);
    if (n == 0) {
      if (faites > 0) Dessin::texte(F_GRAS, MARGE, yy + 42, "Tout est fait. Bravo !", NOIR);
      else Dessin::texte(F_TEXTE, MARGE, yy + 42, "Rien à faire. Profite !", GRIS_FONCE);
      return;
    }
    if (nAuj == 0 && reste == 0) {   // plus rien pour aujourd'hui : on le dit, puis les jours suivants
      if (faites > 0) Dessin::texte(F_GRAS, MARGE, yy + 42, "Tout est fait pour aujourd'hui !", NOIR);
      else Dessin::texte(F_TEXTE, MARGE, yy + 42, "Rien pour aujourd'hui.", GRIS_FONCE);
      yy += H_TACHE;
      if (n > nb - 1) n = nb - 1;
    }
    const int auj = Horloge::aujourdhui();
    for (int i = 0; i < n; i++) {
      if (i == nAuj && i > 0) Dessin::pointille(MARGE, yy, ECRAN_L - 2 * MARGE, GRIS);
      ligneTache(yy, idx[i], auj);
      yy += H_TACHE;
    }
  }

  void ligneTache(int y, int index, int auj) {
    const Tache* ta = Stockage::tache(index);
    if (!ta) return;
    UI::caseACocher(MARGE, y + (H_TACHE - CASE_TAILLE) / 2, false);
    UI::zone(boite(0, y, 84, H_TACHE), C_COCHER, index);
    // À droite : l'heure (aujourd'hui), la date en noir (en retard), ou la date en gris (plus tard)
    char d[40] = "";
    char hm[8] = "";
    if (ta->heure >= 0) texteMinutes(ta->heure, hm, sizeof(hm));
    const GFXfont* pd = F_PETITE;
    uint8_t cd = GRIS_FONCE;
    if (ta->echeance == auj) {
      strcpy(d, hm);
      pd = F_GRAS;
      cd = NOIR;
    } else if (ta->echeance) {
      char r[24];
      Horloge::texteDateRelative(r, sizeof(r), ta->echeance);
      if (hm[0]) snprintf(d, sizeof(d), "%s %s", r, hm);
      else snprintf(d, sizeof(d), "%s", r);
      if (ta->echeance < auj) cd = NOIR;
    }
    const GFXfont* p = policeTexte();
    const int base = Dessin::baseCentree(p, y, H_TACHE);
    int largeurDate = 0;
    if (d[0]) {
      largeurDate = Dessin::largeur(pd, d) + 12;
      Dessin::texteDroite(pd, ECRAN_L - MARGE, base, d, cd);
    }
    Dessin::texteTronque(p, MARGE + 60, base, ECRAN_L - 2 * MARGE - 60 - largeurDate, ta->titre, NOIR);
    UI::zone(boite(84, y, ECRAN_L - 84, H_TACHE), C_TACHE, index);
  }
};

AppAccueil g_accueil;

}  // namespace

App* appAccueil() { return &g_accueil; }
