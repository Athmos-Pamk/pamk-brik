// Calendrier : vue du mois (jours avec tâches marqués), toucher un jour -> ses tâches.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"

namespace {

enum Code { C_MOIS_PREC, C_MOIS_SUIV, C_AUJOURDHUI, C_JOUR, C_AJOUTER, C_COCHER, C_OUVRIR };

const int ENTETE_Y = CONTENU_Y + 8;      // ligne « ‹ octobre 2026 › »
const int ENTETE_H = 72;
const int JOURS_Y = ENTETE_Y + ENTETE_H + 36;   // ligne de base des initiales L M M J V S D
const int GRILLE_Y = JOURS_Y + 16;
const int CASE_L = 72;
const int CASE_H = 96;
const int GRILLE_X = (ECRAN_L - 7 * CASE_L) / 2;   // 18
const int LISTE_Y = CONTENU_Y + 70;
const int PAR_PAGE = (PIED_Y - LISTE_Y) / LIGNE_H;  // 7

int g_indices[TACHES_MAX];

class AppCalendrier : public App {
 public:
  IdApp id() const override { return APP_CALENDRIER; }

  void entrer() override {
    if (!m_initialise) allerAujourdhui();
  }

  void dessiner() override {
    if (m_jour) dessinerJour();
    else dessinerMois();
  }

  void action(int code, int arg) override {
    switch (code) {
      case C_MOIS_PREC: changerMois(-1); break;
      case C_MOIS_SUIV: changerMois(1); break;
      case C_AUJOURDHUI: allerAujourdhui(); m_jour = 0; break;
      case C_JOUR: m_jour = arg; m_pages.page = 0; break;
      case C_AJOUTER: ajouterTacheAuClavier(m_jour); return;
      case C_COCHER: {
        const Tache* t = Stockage::tache(arg);
        if (t) Stockage::cocherTache(t->id, !t->faite);
        break;
      }
      case C_OUVRIR: {
        const Tache* t = Stockage::tache(arg);
        if (t) Systeme::ouvrir(appTache(t->id));
        return;
      }
      case CODE_PAGE_PREC: m_pages.precedente(); break;
      case CODE_PAGE_SUIV: m_pages.suivante(); break;
      default: break;
    }
    Systeme::redessiner();
  }

  bool bouton(Bouton b) override {
    if (m_jour) {   // jour suivant / précédent
      m_jour = Horloge::ajouterJours(m_jour, b == BOUTON_PLUS ? 1 : -1);
      int a, m, j;
      Horloge::decomposer(m_jour, a, m, j);
      m_annee = a;
      m_mois = m;
      m_pages.page = 0;
    } else {
      changerMois(b == BOUTON_PLUS ? 1 : -1);
    }
    Systeme::redessiner();
    return true;
  }

  bool retour() override {
    if (m_jour) {   // de la vue du jour on revient au mois
      m_jour = 0;
      Systeme::redessiner();
      return true;
    }
    m_initialise = false;   // la prochaine ouverture repart d'aujourd'hui
    Systeme::retour();
    return true;
  }

 private:
  bool m_initialise = false;
  int m_annee = 2026;
  int m_mois = 1;
  int m_jour = 0;          // AAAAMMJJ du jour ouvert, 0 = vue du mois
  Pages m_pages = {0, 1};

  void allerAujourdhui() {
    int a, m, j;
    Horloge::decomposer(Horloge::aujourdhui(), a, m, j);
    if (a < 2000) { a = 2026; m = 1; }
    m_annee = a;
    m_mois = m;
    m_initialise = true;
  }

  void changerMois(int d) {
    m_mois += d;
    if (m_mois < 1) { m_mois = 12; m_annee--; }
    if (m_mois > 12) { m_mois = 1; m_annee++; }
  }

  void dessinerMois() {
    UI::barreTitre("Calendrier", true, "Auj.", C_AUJOURDHUI);
    // En-tête : mois et flèches
    const Boite g = boite(MARGE, ENTETE_Y, 80, ENTETE_H - 8);
    const Boite d = boite(ECRAN_L - MARGE - 80, ENTETE_Y, 80, ENTETE_H - 8);
    Dessin::chanfrein(g.x, g.y, g.l, g.h, CHANFREIN, TRAIT, NOIR);
    Dessin::icone(ICONE_CHEVRON_G, g.x + 30, g.y + 18, 4, NOIR);
    Dessin::chanfrein(d.x, d.y, d.l, d.h, CHANFREIN, TRAIT, NOIR);
    Dessin::icone(ICONE_CHEVRON_D, d.x + 30, d.y + 18, 4, NOIR);
    UI::zone(g, C_MOIS_PREC);
    UI::zone(d, C_MOIS_SUIV);
    char titre[32];
    snprintf(titre, sizeof(titre), "%s %d", Horloge::nomMois(m_mois), m_annee);
    titre[0] = (char)toupper((unsigned char)titre[0]);
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, Dessin::baseCentree(F_GRAS, g.y, g.h), titre, NOIR);
    // Initiales des jours
    static const char* const INITIALES[7] = {"L", "M", "M", "J", "V", "S", "D"};
    for (int i = 0; i < 7; i++) {
      Dessin::texteCentre(F_PIXEL, GRILLE_X + i * CASE_L + CASE_L / 2, JOURS_Y, INITIALES[i], i >= 5 ? GRIS : GRIS_FONCE);
    }
    // Grille
    uint32_t aFaire = 0, faites = 0;
    Stockage::joursAvecTaches(m_annee, m_mois, aFaire, faites);
    const int premier = Horloge::composer(m_annee, m_mois, 1);
    const int decalage = Horloge::jourSemaine(premier);   // 0 = lundi
    const int nbJours = Horloge::joursDansMois(m_annee, m_mois);
    const int auj = Horloge::valide() ? Horloge::aujourdhui() : 0;
    for (int j = 1; j <= nbJours; j++) {
      const int k = decalage + j - 1;
      const int x = GRILLE_X + (k % 7) * CASE_L;
      const int y = GRILLE_Y + (k / 7) * CASE_H;
      const int date = Horloge::composer(m_annee, m_mois, j);
      char num[4];
      snprintf(num, sizeof(num), "%d", j);
      const bool estAuj = date == auj;
      if (estAuj) Dessin::chanfreinPlein(x + 4, y + 4, CASE_L - 8, CASE_H - 8, 8, NOIR);
      Dessin::texteCentre(F_GRAS, x + CASE_L / 2, y + 46, num, estAuj ? BLANC : ((k % 7) >= 5 ? GRIS_FONCE : NOIR));
      // Marques : carrés noirs = tâches à faire, carré gris = tâches faites
      const uint8_t marque = estAuj ? BLANC : NOIR;
      if (aFaire & (1UL << j)) {
        int idx[3];
        const int n = Stockage::tachesDuJour(date, idx, 3);
        int nAF = 0;
        for (int i = 0; i < n; i++) if (!Stockage::tache(idx[i])->faite) nAF++;
        nAF = maxi(1, mini(3, nAF));
        const int lm = nAF * 10 + (nAF - 1) * 6;
        for (int i = 0; i < nAF; i++) Dessin::rectPlein(x + (CASE_L - lm) / 2 + i * 16, y + 62, 10, 10, marque);
      } else if (faites & (1UL << j)) {
        Dessin::rectPlein(x + CASE_L / 2 - 5, y + 62, 10, 10, estAuj ? GRIS_CLAIR : GRIS);
      }
      UI::zone(boite(x, y, CASE_L, CASE_H), C_JOUR, date);
    }
    // Résumé du mois
    int total = 0;
    for (int j = 1; j <= 31; j++) if (aFaire & (1UL << j)) total++;
    char resume[64];
    if (total == 0) snprintf(resume, sizeof(resume), "Aucune tâche à faire ce mois-ci");
    else snprintf(resume, sizeof(resume), "%d jour%s avec des tâches à faire", total, total > 1 ? "s" : "");
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, PIED_Y + 50, resume, GRIS_FONCE);
  }

  void dessinerJour() {
    char titre[48];
    Horloge::texteDateCourte(titre, sizeof(titre), m_jour);
    UI::barreTitre(titre, true, "+", C_AJOUTER);
    char longue[64];
    Horloge::texteDateLongue(longue, sizeof(longue), m_jour, true);
    longue[0] = (char)toupper((unsigned char)longue[0]);
    Dessin::texte(F_GRAS, MARGE, CONTENU_Y + 46, longue, NOIR);
    const int n = Stockage::tachesDuJour(m_jour, g_indices, TACHES_MAX);
    m_pages.regler(n, PAR_PAGE);
    if (n == 0) {
      UI::messageVide(LISTE_Y + 90, "Rien de prévu ce jour-là", nullptr);
      UI::bouton(boite(MARGE + 60, LISTE_Y + 330, ECRAN_L - 2 * MARGE - 120, 76), "Ajouter une tâche", C_AJOUTER);
    }
    for (int i = 0; i < PAR_PAGE; i++) {
      const int k = m_pages.page * PAR_PAGE + i;
      if (k >= n) break;
      dessinerLigneTache(LISTE_Y + i * LIGNE_H, g_indices[k], C_COCHER, C_OUVRIR);
    }
    UI::pagination(m_pages);
  }
};

AppCalendrier g_calendrier;

}  // namespace

App* appCalendrier() { return &g_calendrier; }
