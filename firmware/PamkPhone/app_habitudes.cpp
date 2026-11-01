// =============================================================================
//  Pamk Brik — Suivi d'habitudes et rituels quotidiens (Habit Tracker)
//  Paginé : maximum 4 habitudes par page, navigation fluide par glissement ou puces
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "langue.h"
#include <Preferences.h>

namespace {

enum Code {
  C_AJOUTER = 1,
  C_COCHE_0 = 10,
  C_SUPPR_0 = 30,
};

const int HABITUDES_PAR_PAGE = 4;
const int MAX_HABITUDES = 12; // jusqu'à 3 pages de 4 habitudes

struct Habitude {
  char titre[32];
  uint32_t historique; // bits 0 = aujourd'hui, bit 1 = hier, ..., bit 6 = il y a 6 jours
  int streak;          // série de jours consécutifs
};

int g_nbHabitudes = 4;
Habitude g_habitudes[MAX_HABITUDES] = {
  {"Sport / Marche", 0, 0},
  {"Lecture (20 min)", 0, 0},
  {"Hydratation (1,5 L)", 0, 0},
  {"Méditation / Zen", 0, 0},
};

uint32_t g_derniereDate = 0; // AAAAMMJJ
bool g_charge = false;

// Calcule la série en jours consécutifs
int calculerStreak(uint32_t hist) {
  int s = 0;
  int bit = 0;
  if (!(hist & 1)) {
    bit = 1;
  }
  while (bit < 30 && (hist & (1UL << bit))) {
    s++;
    bit++;
  }
  return s;
}

void chargerDonnees() {
  Preferences p;
  if (!p.begin("habits", true)) return;
  g_derniereDate = p.getUInt("date", 0);
  g_nbHabitudes = p.getInt("nb", 4);
  if (g_nbHabitudes < 1) g_nbHabitudes = 1;
  if (g_nbHabitudes > MAX_HABITUDES) g_nbHabitudes = MAX_HABITUDES;

  for (int i = 0; i < g_nbHabitudes; i++) {
    char cleH[8], cleT[8];
    snprintf(cleH, sizeof(cleH), "h%d", i);
    snprintf(cleT, sizeof(cleT), "t%d", i);
    g_habitudes[i].historique = p.getUInt(cleH, 0);
    String t = p.getString(cleT, "");
    if (t.length() > 0) {
      strncpy(g_habitudes[i].titre, t.c_str(), sizeof(g_habitudes[i].titre) - 1);
      g_habitudes[i].titre[sizeof(g_habitudes[i].titre) - 1] = 0;
    }
  }
  p.end();
  g_charge = true;
}

void sauvegarderDonnees() {
  Preferences p;
  if (!p.begin("habits", false)) return;
  p.putUInt("date", g_derniereDate);
  p.putInt("nb", g_nbHabitudes);
  for (int i = 0; i < g_nbHabitudes; i++) {
    char cleH[8], cleT[8];
    snprintf(cleH, sizeof(cleH), "h%d", i);
    snprintf(cleT, sizeof(cleT), "t%d", i);
    p.putUInt(cleH, g_habitudes[i].historique);
    p.putString(cleT, g_habitudes[i].titre);
  }
  p.end();
}

void synchroniserJour() {
  if (!g_charge) chargerDonnees();
  if (!Horloge::valide()) return;

  struct tm t;
  Horloge::locale(t);
  const uint32_t aujourdhui = (uint32_t)(t.tm_year + 1900) * 10000UL +
                              (uint32_t)(t.tm_mon + 1) * 100UL +
                              (uint32_t)t.tm_mday;

  if (g_derniereDate == 0) {
    g_derniereDate = aujourdhui;
    sauvegarderDonnees();
  } else if (g_derniereDate != aujourdhui) {
    int ecart = 1;
    if (aujourdhui > g_derniereDate) {
      ecart = (aujourdhui - g_derniereDate);
      if (ecart > 30) ecart = 30;
    }
    for (int i = 0; i < g_nbHabitudes; i++) {
      g_habitudes[i].historique <<= ecart;
    }
    g_derniereDate = aujourdhui;
    sauvegarderDonnees();
  }

  for (int i = 0; i < g_nbHabitudes; i++) {
    g_habitudes[i].streak = calculerStreak(g_habitudes[i].historique);
  }
}

class AppHabitudes;
static AppHabitudes* s_instance = nullptr;

static void rappelNouvelleHabitude(bool valide, const char* texte, void* contexte);
static void rappelSuppression(bool oui, void* contexte);

class AppHabitudes : public App {
 public:
  IdApp id() const override { return APP_HABITUDES; }

  AppHabitudes() { s_instance = this; }

  void allerPage(int p) {
    const int nbPages = (g_nbHabitudes + HABITUDES_PAR_PAGE - 1) / HABITUDES_PAR_PAGE;
    if (p >= 0 && p < nbPages) {
      m_page = p;
    }
  }

  void entrer() override {
    synchroniserJour();
    const int nbPages = (g_nbHabitudes + HABITUDES_PAR_PAGE - 1) / HABITUDES_PAR_PAGE;
    if (m_page >= nbPages) m_page = maxi(0, nbPages - 1);
  }

  void dessiner() override {
    UI::barreTitre("Habitudes", true, g_nbHabitudes < MAX_HABITUDES ? "+" : nullptr, C_AJOUTER);
    synchroniserJour();

    const int nbPages = (g_nbHabitudes + HABITUDES_PAR_PAGE - 1) / HABITUDES_PAR_PAGE;
    if (m_page >= nbPages) m_page = maxi(0, nbPages - 1);

    const int iDebut = m_page * HABITUDES_PAR_PAGE;
    const int iFin = mini(iDebut + HABITUDES_PAR_PAGE, g_nbHabitudes);

    const int carteL = ECRAN_L - 2 * MARGE;
    const int carteY0 = CONTENU_Y + 8;
    const int carteH = 132;
    const int ecartY = 12;

    int totalCompletes = 0;
    for (int i = 0; i < g_nbHabitudes; i++) {
      if ((g_habitudes[i].historique & 1) != 0) totalCompletes++;
    }

    for (int idx = iDebut; idx < iFin; idx++) {
      const int pos = idx - iDebut;
      const int y = carteY0 + pos * (carteH + ecartY);
      const bool faitAujourdhui = (g_habitudes[idx].historique & 1) != 0;

      // Fond de carte
      Dessin::chanfrein(MARGE, y, carteL, carteH, 12, TRAIT, NOIR);

      // Grande case à cocher à gauche
      const int caseDim = 46;
      const int caseX = MARGE + 16;
      const int caseY = y + 24;
      Dessin::chanfrein(caseX, caseY, caseDim, caseDim, 8, TRAIT, NOIR);
      if (faitAujourdhui) {
        Dessin::chanfreinPlein(caseX + 6, caseY + 6, caseDim - 12, caseDim - 12, 4, NOIR);
        Dessin::icone(ICONE_COCHE, caseX + 9, caseY + 11, 4, BLANC);
      }
      UI::zone(boite(caseX - 10, y, caseDim + 26, carteH), C_COCHE_0 + idx);

      // Titre de l'habitude (clic long pour supprimer)
      const int texteX = caseX + caseDim + 18;
      Dessin::texte(F_GRAS, texteX, y + 40, g_habitudes[idx].titre, NOIR);

      // Série (Streak)
      char txtStreak[28];
      if (g_habitudes[idx].streak > 0) {
        snprintf(txtStreak, sizeof(txtStreak), TR("%d j d'affilée"), g_habitudes[idx].streak);
      } else {
        snprintf(txtStreak, sizeof(txtStreak), TR("À commencer"));
      }
      Dessin::texte(F_PETITE, texteX, y + 66, txtStreak, GRIS_FONCE);

      // Zone tactile sur le titre pour modification/suppression
      UI::zone(boite(texteX, y, carteL - (texteX - MARGE) - 20, 68), C_SUPPR_0 + idx);

      // Mini-calendrier des 7 derniers jours (du plus ancien à aujourd'hui)
      const int miniX0 = texteX;
      const int miniY = y + 88;
      const int carreDim = 18;
      const int carreEcart = 10;

      for (int j = 0; j < 7; j++) {
        const int px = miniX0 + j * (carreDim + carreEcart);
        const int bitIndex = 6 - j;
        const bool cocheJour = (g_habitudes[idx].historique & (1UL << bitIndex)) != 0;

        if (cocheJour) {
          Dessin::rectPlein(px, miniY, carreDim, carreDim, NOIR);
        } else {
          Dessin::rect(px, miniY, carreDim, carreDim, 1, GRIS);
        }
      }
    }

    // Puces de pagination si plusieurs pages (bien espacées)
    if (nbPages > 1) {
      const int yPuces = 752;
      const int r = 5;
      for (int p = 0; p < nbPages; p++) {
        const int cx = ECRAN_L / 2 + (p - (nbPages - 1) / 2.0f) * 28;
        if (p == m_page) Dessin::cerclePlein(cx, yPuces, r, NOIR);
        else Dessin::cercle(cx, yPuces, r, 2, GRIS);
      }
      UI::zone(boite(0, yPuces - 20, ECRAN_L / 2, 40), CODE_PAGE_PREC);
      UI::zone(boite(ECRAN_L / 2, yPuces - 20, ECRAN_L / 2, 40), CODE_PAGE_SUIV);
    }

    // Bandeau récapitulatif en bas
    const int basY = 824;
    char resume[48];
    snprintf(resume, sizeof(resume), TR("%d / %d rituels validés aujourd'hui"), totalCompletes, g_nbHabitudes);
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, basY, resume, NOIR);

    // Barre de progression globale
    const int barreL = ECRAN_L - 4 * MARGE;
    const int pourMille = (totalCompletes * 1000) / maxi(1, g_nbHabitudes);
    Dessin::barreProgression(boite(2 * MARGE, basY + 18, barreL, 16), pourMille, GRIS_CLAIR, NOIR);
  }

  void action(int code, int arg) override {
    (void)arg;
    const int nbPages = (g_nbHabitudes + HABITUDES_PAR_PAGE - 1) / HABITUDES_PAR_PAGE;

    if (code == CODE_PAGE_PREC) {
      if (m_page > 0) {
        m_page--;
        Systeme::redessiner();
      }
      return;
    }

    if (code == CODE_PAGE_SUIV) {
      if (m_page + 1 < nbPages) {
        m_page++;
        Systeme::redessiner();
      }
      return;
    }

    if (code == C_AJOUTER) {
      if (g_nbHabitudes < MAX_HABITUDES) {
        Systeme::ouvrir(appClavier("Nouvelle habitude", "", 30, SAISIE_LIGNE, rappelNouvelleHabitude, nullptr));
      }
      return;
    }

    if (code >= C_COCHE_0 && code < C_COCHE_0 + g_nbHabitudes) {
      const int idx = code - C_COCHE_0;
      g_habitudes[idx].historique ^= 1UL;
      g_habitudes[idx].streak = calculerStreak(g_habitudes[idx].historique);
      sauvegarderDonnees();
      Systeme::redessiner();
      return;
    }

    if (code >= C_SUPPR_0 && code < C_SUPPR_0 + g_nbHabitudes) {
      const int idx = code - C_SUPPR_0;
      char msg[64];
      snprintf(msg, sizeof(msg), TR("Supprimer « %s » ?"), g_habitudes[idx].titre);
      Systeme::ouvrir(appDialogue("Supprimer l'habitude", msg, "Supprimer", rappelSuppression, (void*)(intptr_t)idx));
      return;
    }
  }

  void glisser(int dx, int dy) override {
    const int nbPages = (g_nbHabitudes + HABITUDES_PAR_PAGE - 1) / HABITUDES_PAR_PAGE;
    if (abs(dx) > abs(dy)) {
      if (dx < -35 && m_page + 1 < nbPages) {
        m_page++;
        Systeme::redessiner();
      } else if (dx > 35 && m_page > 0) {
        m_page--;
        Systeme::redessiner();
      }
    }
  }

  void appuiLong(int code, int arg) override {
    action(code, arg);
  }

 private:
  int m_page = 0;
};

static void rappelNouvelleHabitude(bool valide, const char* texte, void* contexte) {
  (void)contexte;
  if (!valide || !texte || !texte[0]) return;
  if (g_nbHabitudes < MAX_HABITUDES) {
    strncpy(g_habitudes[g_nbHabitudes].titre, texte, sizeof(g_habitudes[0].titre) - 1);
    g_habitudes[g_nbHabitudes].titre[sizeof(g_habitudes[0].titre) - 1] = 0;
    g_habitudes[g_nbHabitudes].historique = 0;
    g_habitudes[g_nbHabitudes].streak = 0;
    g_nbHabitudes++;
    sauvegarderDonnees();
    if (s_instance) {
      s_instance->allerPage((g_nbHabitudes - 1) / HABITUDES_PAR_PAGE);
    }
    Systeme::notifier("Habitude ajoutée !");
  }
}

static void rappelSuppression(bool oui, void* contexte) {
  if (!oui) return;
  const int idx = (int)(intptr_t)contexte;
  if (idx >= 0 && idx < g_nbHabitudes) {
    for (int i = idx; i + 1 < g_nbHabitudes; i++) {
      g_habitudes[i] = g_habitudes[i + 1];
    }
    g_nbHabitudes--;
    sauvegarderDonnees();
    Systeme::notifier("Habitude supprimée");
    Systeme::redessiner();
  }
}

AppHabitudes g_appHabitudes;

} // namespace

App* appHabitudes() { return &g_appHabitudes; }
