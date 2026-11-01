// =============================================================================
//  Pamk Brik — Respiration Zen & Cohérence Cardiaque
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"

namespace {

enum Rythme {
  RYTHME_COHERENCE = 0, // 5s inspire, 5s expire
  RYTHME_RELAX_478,     // 4s inspire, 7s retiens, 8s expire
  RYTHME_CARRE,         // 4s inspire, 4s retiens, 4s expire, 4s vide
  RYTHME_NB
};

enum PhaseZen {
  PHASE_INSPIRER,
  PHASE_RETENIR_PLEIN,
  PHASE_EXPIRER,
  PHASE_RETENIR_VIDE,
};

enum EtatZen {
  ZEN_ARRET,
  ZEN_EN_COURS,
  ZEN_PAUSE,
  ZEN_TERMINE,
};

enum Code {
  C_DEMARRER = 1,
  C_PAUSE_REPRISE,
  C_ARRETER,
  C_DUREE_3,
  C_DUREE_5,
  C_RYTHME_0,
  C_RYTHME_1,
  C_RYTHME_2,
};

struct ConfigRythme {
  const char* nom;
  const char* sousTitre;
  int dInspire;
  int dRetenir;
  int dExpire;
  int dVide;
};

const ConfigRythme RYTHMES[RYTHME_NB] = {
  {"Cohérence 5-5", "5s inspire · 5s expire", 5, 0, 5, 0},
  {"Relax 4-7-8", "4s inspire · 7s retiens · 8s expire", 4, 7, 8, 0},
  {"Carrée 4-4", "4s · 4s · 4s · 4s", 4, 4, 4, 4},
};

class AppZen : public App {
 public:
  IdApp id() const override { return APP_ZEN; }

  uint32_t delaiVeilleMs(uint32_t defaut) const override {
    if (m_etat == ZEN_EN_COURS) {
      return 360000; // 6 minutes : ne s'éteint pas pendant la séance
    }
    return defaut;
  }

  void entrer() override {
    if (m_etat == ZEN_TERMINE) m_etat = ZEN_ARRET;
  }

  void seconde() override {
    if (m_etat != ZEN_EN_COURS) return;

    m_secondePhase++;
    int dureePhase = dureePhaseActuelle();

    // On ne redessine qu'au changement de phase (toutes les 4 à 8 s) : un e-paper redessiné
    // chaque seconde clignoterait sans arrêt et s'userait pendant toute la séance.
    if (m_secondePhase >= dureePhase) {
      m_secondePhase = 0;
      passerPhaseSuivante();
      Systeme::redessiner();
    }
  }

  void dessiner() override {
    UI::barreTitre("Respiration", true);

    const int cy = 390;
    const int cx = ECRAN_L / 2;

    if (m_etat == ZEN_ARRET || m_etat == ZEN_TERMINE) {
      // Vrai visage officiel de Pamk en grand (matrice Athmos Robotics)
      Dessin::visagePamk(cx, 160, 12, m_etat == ZEN_TERMINE ? VISAGE_CONTENT : VISAGE_NORMAL, NOIR);

      if (m_etat == ZEN_TERMINE) {
        Dessin::texteCentre(F_TITRE, cx, 320, "Séance terminée !", NOIR);
        Dessin::texteCentre(F_GRAS, cx, 366, "Esprit apaisé et recentré.", GRIS_FONCE);
      } else {
        Dessin::texteCentre(F_TITRE, cx, 320, "Respiration", NOIR);
        Dessin::texteCentre(F_TITRE, cx, 366, "Guidée", NOIR);
        Dessin::texteCentre(F_TEXTE, cx, 414, RYTHMES[m_rythme].sousTitre, GRIS_FONCE);
      }

      // Onglets de rythme
      const int tabY = 466;
      const int tabH = 56;
      const int tabL = (ECRAN_L - 2 * MARGE - 16) / 3;
      static const char* const NOMS_ONGLET[3] = {"Cohérence", "Relax", "Carrée"};

      for (int i = 0; i < 3; i++) {
        const int tx = MARGE + i * (tabL + 8);
        const bool actif = (i == m_rythme);
        if (actif) {
          Dessin::chanfreinPlein(tx, tabY, tabL, tabH, 8, NOIR);
          Dessin::texteCentre(F_GRAS, tx + tabL / 2, tabY + 34, NOMS_ONGLET[i], BLANC);
        } else {
          Dessin::chanfrein(tx, tabY, tabL, tabH, 8, TRAIT, NOIR);
          Dessin::texteCentre(F_PETITE, tx + tabL / 2, tabY + 34, NOMS_ONGLET[i], NOIR);
        }
        UI::zone(boite(tx, tabY, tabL, tabH), C_RYTHME_0 + i);
      }

      // Sélecteur de durée
      const int btnY = 556;
      const int btnL = (ECRAN_L - 3 * MARGE) / 2;
      UI::bouton(boite(MARGE, btnY, btnL, 64), "3 minutes", C_DUREE_3, 0, m_dureeMin == 3 ? STYLE_PLEIN : STYLE_CONTOUR);
      UI::bouton(boite(MARGE + btnL + MARGE, btnY, btnL, 64), "5 minutes", C_DUREE_5, 0, m_dureeMin == 5 ? STYLE_PLEIN : STYLE_CONTOUR);

      // Gros bouton Démarrer tout en bas
      UI::bouton(boite(MARGE, 840, ECRAN_L - 2 * MARGE, 76), "Commencer la séance", C_DEMARRER, 0, STYLE_PLEIN);

    } else {
      // Séance en cours ou en pause
      const ConfigRythme& r = RYTHMES[m_rythme];
      const int dPhase = dureePhaseActuelle();
      const int rMin = 75;
      const int rMax = 165;
      // Une image par phase : l'anneau montre où aller (poumons pleins ou vides).
      const int rayon = (m_phase == PHASE_INSPIRER || m_phase == PHASE_RETENIR_PLEIN) ? rMax : rMin;

      // Repères visuels de respiration
      Dessin::cercle(cx, cy, rMax, 1, GRIS_CLAIR); // repère poumons pleins
      Dessin::cercle(cx, cy, rMin, 1, GRIS_CLAIR);  // repère poumons vides
      Dessin::cercle(cx, cy, rayon, 3, NOIR);       // anneau dynamique

      // Vrai Pamk au centre en grand avec expression synchronisée au souffle
      Expression expr = VISAGE_NORMAL;
      if (m_phase == PHASE_INSPIRER) expr = VISAGE_CONTENT;
      else if (m_phase == PHASE_RETENIR_PLEIN) expr = VISAGE_NORMAL;
      else expr = VISAGE_DORT;

      Dessin::visagePamk(cx, cy - 35, 10, expr, NOIR);

      // Consigne textuelle claire
      const char* consigne = "EN PAUSE";
      if (m_etat == ZEN_EN_COURS) {
        if (m_phase == PHASE_INSPIRER) consigne = "INSPIREZ...";
        else if (m_phase == PHASE_RETENIR_PLEIN) consigne = "RETENEZ...";
        else if (m_phase == PHASE_EXPIRER) consigne = "EXPIREZ...";
        else consigne = "PAUSE VIDE...";
      }
      Dessin::texteCentre(F_TITRE, cx, 600, consigne, NOIR);

      // Durée de la phase (pas de compteur à la seconde : voir seconde())
      char txtSec[24];
      snprintf(txtSec, sizeof(txtSec), "%d secondes", dPhase);
      Dessin::texteCentre(F_GRAS, cx, 638, txtSec, GRIS_FONCE);

      // Avancement global
      char txtProg[32];
      snprintf(txtProg, sizeof(txtProg), "Cycle %d / %d · %s", m_cycleActuel + 1, m_totalCycles, r.nom);
      Dessin::texteCentre(F_PETITE, cx, 672, txtProg, GRIS_FONCE);

      const int barreL = ECRAN_L - 4 * MARGE;
      const int pourMille = (m_cycleActuel * 1000) / maxi(1, m_totalCycles);
      Dessin::barreProgression(boite(2 * MARGE, 700, barreL, 10), pourMille, GRIS_CLAIR, NOIR);

      // Boutons de contrôle tout en bas
      const int btnY = 840;
      const int btnL = (ECRAN_L - 3 * MARGE) / 2;
      UI::bouton(boite(MARGE, btnY, btnL, 76), m_etat == ZEN_PAUSE ? "Reprendre" : "Pause", C_PAUSE_REPRISE, 0, STYLE_CONTOUR);
      UI::bouton(boite(MARGE + btnL + MARGE, btnY, btnL, 76), "Arrêter", C_ARRETER, 0, STYLE_DISCRET);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    switch (code) {
      case C_RYTHME_0: m_rythme = RYTHME_COHERENCE; Systeme::redessiner(); break;
      case C_RYTHME_1: m_rythme = RYTHME_RELAX_478;   Systeme::redessiner(); break;
      case C_RYTHME_2: m_rythme = RYTHME_CARRE;       Systeme::redessiner(); break;

      case C_DUREE_3: m_dureeMin = 3; Systeme::redessiner(); break;
      case C_DUREE_5: m_dureeMin = 5; Systeme::redessiner(); break;

      case C_DEMARRER: {
        m_etat = ZEN_EN_COURS;
        m_phase = PHASE_INSPIRER;
        m_cycleActuel = 0;
        m_secondePhase = 0;
        const ConfigRythme& r = RYTHMES[m_rythme];
        const int dureeCycle = r.dInspire + r.dRetenir + r.dExpire + r.dVide;
        m_totalCycles = (m_dureeMin * 60) / maxi(1, dureeCycle);
        Systeme::redessiner();
        break;
      }

      case C_PAUSE_REPRISE:
        if (m_etat == ZEN_PAUSE) m_etat = ZEN_EN_COURS;
        else m_etat = ZEN_PAUSE;
        Systeme::redessiner();
        break;

      case C_ARRETER:
        m_etat = ZEN_ARRET;
        Systeme::redessiner();
        break;
    }
  }

 private:
  Rythme m_rythme = RYTHME_COHERENCE;
  EtatZen m_etat = ZEN_ARRET;
  PhaseZen m_phase = PHASE_INSPIRER;
  int m_dureeMin = 3;
  int m_totalCycles = 18;
  int m_cycleActuel = 0;
  int m_secondePhase = 0;

  int dureePhaseActuelle() const {
    const ConfigRythme& r = RYTHMES[m_rythme];
    switch (m_phase) {
      case PHASE_INSPIRER: return r.dInspire;
      case PHASE_RETENIR_PLEIN: return r.dRetenir;
      case PHASE_EXPIRER: return r.dExpire;
      case PHASE_RETENIR_VIDE: return r.dVide;
      default: return 5;
    }
  }

  void passerPhaseSuivante() {
    const ConfigRythme& r = RYTHMES[m_rythme];
    if (m_phase == PHASE_INSPIRER) {
      if (r.dRetenir > 0) m_phase = PHASE_RETENIR_PLEIN;
      else m_phase = PHASE_EXPIRER;
    } else if (m_phase == PHASE_RETENIR_PLEIN) {
      m_phase = PHASE_EXPIRER;
    } else if (m_phase == PHASE_EXPIRER) {
      if (r.dVide > 0) {
        m_phase = PHASE_RETENIR_VIDE;
      } else {
        finDeCycle();
      }
    } else { // PHASE_RETENIR_VIDE
      finDeCycle();
    }
  }

  void finDeCycle() {
    m_cycleActuel++;
    if (m_cycleActuel >= m_totalCycles) {
      m_etat = ZEN_TERMINE;
    } else {
      m_phase = PHASE_INSPIRER;
    }
  }
};

AppZen g_appZen;

} // namespace

App* appZen() { return &g_appZen; }
