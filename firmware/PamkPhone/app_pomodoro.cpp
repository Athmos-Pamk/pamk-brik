// Pomodoro (travail / pause / grande pause) et minuteur.
// Affichage à la minute près : l'e-paper n'est rafraîchi que quand le nombre change.
// Mode Concentration (28/10/2026) : on choisit sur quoi on travaille (une tâche à faire, ou
// rien) ; pendant une phase de travail, l'écran ne montre que le temps restant en très grand,
// la tâche et les boutons pause / arrêter / tâche faite. L'onglet Minuteur et les réglages
// restent accessibles hors des phases de travail.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "parametres.h"
#include "minuteries.h"
#include "stockage.h"
#include "langue.h"

namespace {

enum Code {
  C_ONGLET, C_DEMARRER, C_PAUSE, C_PASSER, C_ARRETER, C_REGLAGES,
  C_MIN_AJOUT, C_MIN_PRESET, C_MIN_DEMARRER, C_MIN_PAUSE, C_MIN_ARRETER,
  // Concentration (28/10/2026)
  C_CHOISIR, C_CHOIX, C_NOUVELLE, C_TACHE_FAITE,
};

enum Vue : uint8_t { VUE_POMODORO, VUE_CHOIX };   // VUE_CHOIX : « sur quoi tu travailles ? »

const int ONGLETS_Y = CONTENU_Y + 10;
const int ONGLETS_H = 56;
const int PRESETS[9] = {1, 3, 5, 10, 15, 20, 30, 45, 60};
const int AJOUTS[4] = {-5, -1, 1, 5};
const int CHOIX_Y = CONTENU_Y + 8;                         // liste des tâches à choisir
const int CHOIX_PAR_PAGE = (PIED_Y - CHOIX_Y) / LIGNE_H;   // 7
const int TACHE_Y = 566;                                   // écran Prêt : ligne de la tâche
const int TACHE_H = 96;

int g_indices[TACHES_MAX];

// Tâche choisie pour le Pomodoro, si elle existe encore et reste à faire.
const Tache* tacheChoisie() {
  const char* id = Minuteries::pomoTache();
  if (!id[0]) return nullptr;
  const Tache* t = Stockage::tache(Stockage::indexTache(id));
  return (t && !t->faite) ? t : nullptr;
}

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
  snprintf(b, n, TR("Fin à %s"), h);
}

class AppPomodoro : public App {
 public:
  IdApp id() const override { return APP_POMODORO; }
  bool barreEtat() const override { return !concentration(); }   // rien ne distrait

  void entrer() override {
    if (!m_garderVue) m_vue = VUE_POMODORO;   // retour du clavier « Nouvelle tâche » : on reste
    m_garderVue = false;
    if (!tacheChoisie()) Minuteries::pomoChoisirTache("");   // faite ou supprimée entre-temps
    // Si Pomodoro tourne, on ouvre l'onglet Pomodoro (1) ; sinon Minuteur (0)
    if (Minuteries::pomoPhase() != POMO_ARRET && !Minuteries::minActif()) {
      m_onglet = 1;
    } else {
      m_onglet = 0;
    }
  }

  void dessiner() override {
    if (m_vue == VUE_CHOIX) {
      dessinerChoix();
    } else if (concentration()) {
      dessinerConcentration();
    } else {
      UI::barreTitre("Minuteur", true);
      const int lo = (ECRAN_L - 2 * MARGE - 12) / 2;
      UI::bouton(boite(MARGE, ONGLETS_Y, lo, ONGLETS_H), "Décompte", C_ONGLET, 0, m_onglet == 0 ? STYLE_PLEIN : STYLE_DISCRET);
      UI::bouton(boite(MARGE + lo + 12, ONGLETS_Y, lo, ONGLETS_H), "Pomodoro", C_ONGLET, 1, m_onglet == 1 ? STYLE_PLEIN : STYLE_DISCRET);
      if (m_onglet == 0) dessinerMinuteur();
      else dessinerPomodoro();
    }
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
      case C_CHOISIR:
        m_vue = VUE_CHOIX;
        m_apresFaite = false;
        m_pages.page = 0;
        break;
      case C_CHOIX: {   // arg : index de la tâche, -1 = sans tâche
        const Tache* t = Stockage::tache(arg);
        Minuteries::pomoChoisirTache(t ? t->id : "");
        m_vue = VUE_POMODORO;
        break;
      }
      case C_NOUVELLE: {
        App* c = appClavier("Nouvelle tâche", "", TACHE_TITRE_MAX, SAISIE_LIGNE, rappelNouvelle, this);
        if (c) {
          m_garderVue = true;
          Systeme::ouvrir(c);
        }
        return;
      }
      case C_TACHE_FAITE: {
        const Tache* t = tacheChoisie();
        if (!t) break;
        char id[ID_LONGUEUR + 1];
        strncpy(id, t->id, sizeof(id) - 1);
        id[sizeof(id) - 1] = 0;
        Stockage::cocherTache(id, true);
        Minuteries::pomoChoisirTache("");
        Systeme::notifier("Tâche faite, bravo !");
        if (Stockage::nbTachesAFaire() > 0) {   // la phase continue : sur quoi ensuite ?
          m_vue = VUE_CHOIX;
          m_apresFaite = true;
          m_pages.page = 0;
        }
        break;
      }
      case CODE_PAGE_PREC: m_pages.precedente(); break;
      case CODE_PAGE_SUIV: m_pages.suivante(); break;
      default: break;
    }
    Systeme::redessiner();
  }

  bool retour() override {
    if (m_vue == VUE_CHOIX) {   // le choix se ferme sans rien changer
      m_vue = VUE_POMODORO;
      Systeme::redessiner();
      return true;
    }
    return App::retour();
  }

  bool bouton(Bouton b) override {
    if (m_vue == VUE_CHOIX) {
      const bool ok = (b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente();
      if (ok) Systeme::redessiner();
      return true;
    }
    if (concentration()) {
      if (b == BOUTON_PLUS) {
        Minuteries::pomoPauseReprise();
        Systeme::redessiner();
      }
      return true;
    }
    if (m_onglet == 1) {   // onglet Pomodoro (0 = Décompte, le minuteur)
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
    if (m_vue == VUE_CHOIX) {   // pages de la liste
      App::glisser(dx, dy);
      return;
    }
    if (concentration()) return;
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
  Vue m_vue = VUE_POMODORO;
  bool m_apresFaite = false;   // choix ouvert juste après « Tâche faite »
  bool m_garderVue = false;    // le prochain entrer() garde la vue (clavier refermé)
  Pages m_pages = {0, 1};

  // Phase de travail affichée : l'écran épuré remplace les onglets.
  bool concentration() const { return m_vue == VUE_POMODORO && Minuteries::pomoPhase() == POMO_TRAVAIL; }

  // Ce qui est affiché ; un changement demande un rafraîchissement.
  long signature() {
    const bool conc = concentration();
    long s = m_vue;
    s = s * 2 + m_onglet;
    s = s * 8 + Minuteries::pomoPhase();
    s = s * 2 + (Minuteries::pomoSuspendu() ? 1 : 0);
    s = s * 2 + (Minuteries::minActif() ? 1 : 0);
    s = s * 2 + (Minuteries::minSuspendu() ? 1 : 0);
    int r = (m_onglet == 1 || conc) ? Minuteries::pomoRestantS() : Minuteries::minRestantS();   // 1 = Pomodoro
    if (m_vue == VUE_CHOIX) r = 0;   // la liste ne dépend pas du temps
    s = s * 1000 + (r > 0 && r < 60 ? 999 : (r + 59) / 60);
    return s;
  }

  static void rappelNouvelle(bool ok, const char* texte, void* ctx) {
    AppPomodoro* moi = static_cast<AppPomodoro*>(ctx);
    if (!ok || !texte || !*texte) return;   // annulé : on reste sur le choix
    const Tache* t = Stockage::ajouterTache(texte, 0);
    if (!t) {
      Systeme::notifier("Impossible d'ajouter la tâche");
      return;
    }
    Minuteries::pomoChoisirTache(t->id);
    moi->m_vue = VUE_POMODORO;
    Systeme::redessiner();
  }

  // --- Concentration : seulement le temps restant, la tâche et trois boutons ---
  void dessinerConcentration() {
    const bool suspendu = Minuteries::pomoSuspendu();
    // Pas de barre de titre : un chevron discret pour revenir (glisser du bord gauche aussi)
    Dessin::icone(ICONE_CHEVRON_G, MARGE + 2, 64, 5, GRIS);
    UI::zone(boite(0, 30, RETOUR_L, 100), CODE_RETOUR);
    Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 96, suspendu ? "EN PAUSE" : "CONCENTRATION", GRIS_FONCE);
    // Temps restant, arrondi au-dessus comme la barre d'état
    const int minutes = (Minuteries::pomoRestantS() + 59) / 60;
    char num[8];
    snprintf(num, sizeof(num), "%d", minutes);
    Dessin::texteCentre(F_HORLOGE, ECRAN_L / 2, 310, num, suspendu ? GRIS : NOIR);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 390, minutes > 1 ? "minutes" : "minute", GRIS_FONCE);
    Dessin::rectPlein(ECRAN_L / 2 - 30, 436, 60, 3, GRIS_CLAIR);
    // Tâche (toucher pour en changer)
    const Tache* t = tacheChoisie();
    if (t) {
      const GFXfont* p = policeGras();
      const int lmax = ECRAN_L - 2 * MARGE;
      LigneTexte l[3];
      const int n = Dessin::couperLignes(p, t->titre, lmax, l, 3);
      const int pas = Dessin::hauteurLigne(p);
      for (int i = 0; i < n; i++) {
        const int y = 500 + i * pas;
        if (i == n - 1 && l[i].fin < (int)strlen(t->titre)) {   // trop long : « … » au bout
          Dessin::texteTronque(p, MARGE, y, lmax, t->titre + l[i].debut, NOIR);
        } else {
          const int lg = Dessin::largeurN(p, t->titre + l[i].debut, l[i].fin - l[i].debut);
          Dessin::texteN(p, (ECRAN_L - lg) / 2, y, t->titre + l[i].debut, l[i].fin - l[i].debut, NOIR);
        }
      }
    } else {
      Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 500, "Sans tâche", GRIS_FONCE);
      Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 536, "toucher pour en choisir une", GRIS);
    }
    UI::zone(boite(MARGE, 450, ECRAN_L - 2 * MARGE, 200), C_CHOISIR);
    // Boutons
    if (t) UI::bouton(boite(MARGE, 680, ECRAN_L - 2 * MARGE, 84), "Tâche faite", C_TACHE_FAITE);
    const int lb = (ECRAN_L - 2 * MARGE - 16) / 2;
    UI::bouton(boite(MARGE, 790, lb, 84), suspendu ? "Reprendre" : "Pause", C_PAUSE, 0, STYLE_PLEIN);
    UI::bouton(boite(MARGE + lb + 16, 790, lb, 84), "Arrêter", C_ARRETER, 0, STYLE_DISCRET);
  }

  // --- Choix de la tâche : « Sans tâche » puis les tâches à faire ---
  void dessinerChoix() {
    UI::barreTitre(m_apresFaite ? "Et ensuite ?" : "Sur quoi ?", true, "+", C_NOUVELLE);
    const int nbTaches = Stockage::listeTaches(TACHES_A_FAIRE, g_indices, TACHES_MAX);
    const int n = nbTaches + 1;   // + « Sans tâche » en tête
    m_pages.regler(n, CHOIX_PAR_PAGE);
    const Tache* choisie = tacheChoisie();
    for (int i = 0; i < CHOIX_PAR_PAGE; i++) {
      const int k = m_pages.page * CHOIX_PAR_PAGE + i;
      if (k >= n) break;
      const int index = k == 0 ? -1 : g_indices[k - 1];
      const Tache* t = Stockage::tache(index);
      ligneChoix(CHOIX_Y + i * LIGNE_H, index, t ? t == choisie : choisie == nullptr);
    }
    if (nbTaches == 0) UI::messageVide(CHOIX_Y + LIGNE_H + 60, "Aucune tâche à faire", "Touche + pour en ajouter une.");
    UI::pagination(m_pages);
  }

  // Une ligne du choix : index = -1 pour « Sans tâche », sinon l'index de la tâche.
  void ligneChoix(int y, int index, bool choisie) {
    const int cx = MARGE + 20, cy = y + LIGNE_H / 2;
    Dessin::cercle(cx, cy, 18, TRAIT, NOIR);
    if (choisie) Dessin::cerclePlein(cx, cy, 9, NOIR);
    const int x = MARGE + 60;
    const Tache* t = Stockage::tache(index);
    if (!t) {
      Dessin::texte(F_TEXTE, x, y + 44, "Sans tâche", NOIR);
      Dessin::texte(F_PETITE, x, y + 76, "juste le temps de concentration", GRIS_FONCE);
    } else {
      Dessin::texteTronque(F_TEXTE, x, y + 44, ECRAN_L - MARGE - x, t->titre, NOIR);
      if (t->echeance) {
        char d[24], sous[48];
        Horloge::texteDateRelative(d, sizeof(d), t->echeance);
        const bool retard = t->echeance < Horloge::aujourdhui();
        snprintf(sous, sizeof(sous), "%s%s", retard ? TR("en retard · ") : "", d);
        Dessin::texte(F_PETITE, x, y + 76, sous, retard ? NOIR : GRIS_FONCE);
      }
    }
    UI::separateur(y + LIGNE_H - 2, x, ECRAN_L - MARGE);
    UI::zone(boite(0, y, ECRAN_L, LIGNE_H), C_CHOIX, index);
  }

  // Ligne « Sur quoi tu travailles ? » de l'écran Prêt : ouvre le choix.
  void ligneTacheChoisie() {
    const Boite b = boite(MARGE, TACHE_Y, ECRAN_L - 2 * MARGE, TACHE_H);
    Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, 2, GRIS);
    Dessin::texte(F_PETITE, b.x + 20, b.y + 34, "Sur quoi tu travailles ?", GRIS_FONCE);
    const Tache* t = tacheChoisie();
    Dessin::texteTronque(F_GRAS, b.x + 20, b.y + 74, b.l - 40 - 30, t ? t->titre : "Sans tâche", t ? NOIR : GRIS_FONCE);
    Dessin::icone(ICONE_CHEVRON_D, b.x + b.l - 20 - 15, b.y + (b.h - 21) / 2, 3, GRIS);
    UI::zone(b, C_CHOISIR);
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
    snprintf(t, sizeof(t), TR("%d pomodoro%s terminé%s"), Minuteries::pomoCyclesFaits(),
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
      ligneTacheChoisie();
      UI::bouton(boite(MARGE, 680, ECRAN_L - 2 * MARGE, 84), "Démarrer", C_DEMARRER, 0, STYLE_PLEIN);
    } else {
      UI::bouton(boite(MARGE, 630, lb, 84), Minuteries::pomoSuspendu() ? "Reprendre" : "Pause", C_PAUSE, 0, STYLE_PLEIN);
      UI::bouton(boite(MARGE + lb + 16, 630, lb, 84), "Passer", C_PASSER);
      UI::bouton(boite(MARGE, 730, ECRAN_L - 2 * MARGE, 76), "Arrêter la série", C_ARRETER, 0, STYLE_DISCRET);
    }
    snprintf(t, sizeof(t), TR("%u min · pause %u · grande pause %u · %u cycles"),
             (unsigned)Param.pomoTravail, (unsigned)Param.pomoPause, (unsigned)Param.pomoLongue, (unsigned)Param.pomoCycles);
    UI::separateur(842);
    Dessin::texteTronque(F_PETITE, MARGE, 890, ECRAN_L - 2 * MARGE - 30, t, GRIS_FONCE);
    Dessin::icone(ICONE_CHEVRON_D, ECRAN_L - MARGE - 15, 868, 3, GRIS);
    UI::zone(boite(0, 846, ECRAN_L, 80), C_REGLAGES);
  }

  void dessinerMinuteur() {
    char t[48];
    const int cx = ECRAN_L / 2;
    const int cyDial = 360;

    if (Minuteries::minActif()) {
      Dessin::texteCentre(F_PIXEL, cx, 230, Minuteries::minSuspendu() ? "EN PAUSE" : "EN COURS", GRIS_FONCE);
      const int restant = Minuteries::minRestantS();
      const int duree = maxi(1, Minuteries::minDureeS());

      // Cadran circulaire élégant
      Dessin::cercle(cx, cyDial, 105, 3, NOIR);
      Dessin::cercle(cx, cyDial, 115, 1, GRIS_CLAIR);

      // Minutes seulement : un e-paper ne se redessine pas chaque seconde (clignotement, usure,
      // batterie). L'écran change une fois par minute, comme la signature().
      char numMin[12];
      if (restant > 0 && restant < 60) snprintf(numMin, sizeof(numMin), "<1");
      else snprintf(numMin, sizeof(numMin), "%d", (restant + 59) / 60);
      Dessin::texteCentre(F_GRANDE, cx, cyDial + 30, numMin, NOIR);
      Dessin::texteCentre(F_PIXEL, cx, cyDial + 78, "min", GRIS_FONCE);

      // Barre de progression circulaire / rectangulaire
      Dessin::barreProgression(boite(MARGE, cyDial + 128, ECRAN_L - 2 * MARGE, 14),
                               (int)((long)(duree - restant) * 1000 / duree), GRIS_CLAIR, NOIR);
      if (!Minuteries::minSuspendu()) {
        heureFin(restant, t, sizeof(t));
        Dessin::texteCentre(F_TEXTE, cx, cyDial + 175, t, NOIR);
      }
      const int lb = (ECRAN_L - 2 * MARGE - 16) / 2;
      UI::bouton(boite(MARGE, 730, lb, 84), Minuteries::minSuspendu() ? "Reprendre" : "Pause", C_MIN_PAUSE, 0, STYLE_PLEIN);
      UI::bouton(boite(MARGE + lb + 16, 730, lb, 84), "Arrêter", C_MIN_ARRETER, 0, STYLE_CONTOUR);
      return;
    }

    // Cadran circulaire en attente
    Dessin::cercle(cx, cyDial, 95, 3, NOIR);
    Dessin::cercle(cx, cyDial, 103, 1, GRIS_CLAIR);

    char valMin[12];
    snprintf(valMin, sizeof(valMin), "%d", Param.minuteurMin);
    Dessin::texteCentre(F_GRANDE, cx, cyDial + 24, valMin, NOIR);
    Dessin::texteCentre(F_TITRE, cx, cyDial + 62, "min", GRIS_FONCE);

    // Boutons de réglage fin (− 5, − 1, + 1, + 5)
    static const char* const LIB_AJOUTS[4] = {"- 5", "- 1", "+ 1", "+ 5"};
    const int la = (ECRAN_L - 2 * MARGE - 3 * 12) / 4;
    for (int i = 0; i < 4; i++) {
      UI::bouton(boite(MARGE + i * (la + 12), 486, la, 56), LIB_AJOUTS[i], C_MIN_AJOUT, i, STYLE_CONTOUR);
    }

    // 6 Préréglages rapides (3, 5, 10, 15, 25, 45 min)
    static const int PRESETS_COURTS[6] = {3, 5, 10, 15, 25, 45};
    const int lp = (ECRAN_L - 2 * MARGE - 2 * 12) / 3;
    for (int i = 0; i < 6; i++) {
      snprintf(t, sizeof(t), "%d min", PRESETS_COURTS[i]);
      const int px = MARGE + (i % 3) * (lp + 12);
      const int py = 560 + (i / 3) * 66;
      int presetIdx = 0;
      for (int k = 0; k < 9; k++) if (PRESETS[k] == PRESETS_COURTS[i]) presetIdx = k;
      UI::bouton(boite(px, py, lp, 54), t, C_MIN_PRESET, presetIdx,
                 Param.minuteurMin == PRESETS_COURTS[i] ? STYLE_PLEIN : STYLE_CONTOUR);
    }

    UI::bouton(boite(MARGE, 730, ECRAN_L - 2 * MARGE, 84), "Démarrer le minuteur", C_MIN_DEMARRER, 0, STYLE_PLEIN);
  }
};

AppPomodoro g_pomodoro;

}  // namespace

App* appPomodoro() { return &g_pomodoro; }
