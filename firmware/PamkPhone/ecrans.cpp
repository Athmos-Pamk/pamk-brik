#include "ecrans.h"
#include "ecran.h"
#include "dessin.h"
#include "horloge.h"
#include "energie.h"
#include "stockage.h"
#include "minuteries.h"
#include "parametres.h"

namespace Ecrans {

// Ce que montre la dalle pendant le sommeil profond, gardé en mémoire RTC : au réveil on
// redessine exactement le même écran, puis on ne change que les pixels qui diffèrent.
enum TypeMemo : uint8_t { MEMO_AUCUN, MEMO_VEILLE, MEMO_ETEINT, MEMO_VIDE };

struct Memo {
  uint32_t magie;
  uint8_t type;
  bool heureValide;
  bool grandeHeure;            // heure en grand (mise à jour par minuterie) ou « en veille depuis »
  char heure[8];
  char date[48];
  char batterie[24];
  char minuterie[64];          // ligne Pomodoro / minuteur ("" = aucune)
  uint8_t nbTaches;
  char taches[4][TACHE_TITRE_MAX + 1];
};

static const uint32_t MAGIE = 0x4D454D31;   // "MEM1"
static RTC_DATA_ATTR Memo r_memo;

void demarrage(const char* message) {
  Ecran::effacer(BLANC);
  Dessin::visagePamk(ECRAN_L / 2, 280, 24, VISAGE_NORMAL, NOIR);
  Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 560, PAMK_NOM, NOIR);
  Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 612, "V0 - " PAMK_VERSION, GRIS_FONCE);
  if (message && *message) Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 860, message, GRIS_FONCE);
}

static void copier(char* d, size_t n, const char* s) {
  size_t l = strlen(s ? s : "");
  if (l > n - 1) {
    l = n - 1;
    while (l > 0 && (((uint8_t)s[l]) & 0xC0) == 0x80) l--;   // ne coupe pas un caractère UTF-8
  }
  memcpy(d, s, l);
  d[l] = 0;
}

static void dessinerVeille(const Memo& m) {
  Ecran::effacer(BLANC);
  Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 72, "EN VEILLE", GRIS_FONCE);
  Dessin::visagePamk(ECRAN_L / 2, 112, 20, VISAGE_DORT, NOIR);
  Dessin::texte(F_PIXEL, ECRAN_L / 2 + 124, 150, "z", GRIS);
  Dessin::texte(F_TITRE, ECRAN_L / 2 + 148, 128, "z", GRIS_FONCE);

  const int y = 292;
  if (m.heureValide) {
    if (m.grandeHeure) {
      Dessin::texteCentre(F_GRANDE, ECRAN_L / 2, y + 112, m.heure, NOIR);
    } else {
      char ligne[48];
      snprintf(ligne, sizeof(ligne), "en veille depuis %s", m.heure);
      Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, y + 90, ligne, GRIS_FONCE);
    }
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, y + 160, m.date, NOIR);
  } else {
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, y + 112, "Heure à régler", NOIR);
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, y + 156, "connecte le Wi-Fi au réveil", GRIS_FONCE);
  }
  Dessin::texteCentre(F_PETITE, ECRAN_L / 2, y + 200, m.batterie, GRIS_FONCE);

  int yy = 552;
  if (m.minuterie[0]) {
    Dessin::rectPlein(MARGE, yy - 22, 16, 16, NOIR);
    Dessin::texte(F_GRAS, MARGE + 30, yy, m.minuterie, NOIR);
    yy += 64;
  }
  if (m.nbTaches > 0) {
    Dessin::texte(F_PIXEL, MARGE, yy, "À FAIRE", GRIS_FONCE);
    yy += 46;
    for (int i = 0; i < m.nbTaches && i < 4; i++) {
      Dessin::chanfrein(MARGE, yy - 22, 24, 24, 4, 2, NOIR);
      Dessin::texteTronque(F_TEXTE, MARGE + 40, yy, ECRAN_L - 2 * MARGE - 40, m.taches[i], NOIR);
      yy += 46;
    }
  }
  Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 932,
                      BOUTONS_PRESENTS ? "Appuie sur le bouton du dessus pour réveiller" : "Touche l'écran pour réveiller",
                      GRIS_FONCE);
}

static void dessinerEteint() {
  Ecran::effacer(BLANC);
  Dessin::visagePamk(ECRAN_L / 2, 300, 20, VISAGE_ETEINT, GRIS_FONCE);
  Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 560, "Éteint", NOIR);
  if (BOUTONS_PRESENTS) {
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 620, "Maintiens le bouton du dessus", GRIS_FONCE);
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 658, "2 secondes pour rallumer.", GRIS_FONCE);
  } else {
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 620, "Pour rallumer, appuie sur", GRIS_FONCE);
    Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 658, "le bouton RST de la carte.", GRIS_FONCE);
  }
}

static void dessinerVide() {
  Ecran::effacer(BLANC);
  Dessin::visagePamk(ECRAN_L / 2, 220, 18, VISAGE_FATIGUE, NOIR);
  const int x = ECRAN_L / 2 - 110, y = 420;
  Dessin::rect(x, y, 200, 100, 8, NOIR);
  Dessin::rectPlein(x + 200, y + 30, 20, 40, NOIR);
  Dessin::rectPlein(x + 16, y + 16, 24, 68, NOIR);
  Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 640, "Batterie vide", NOIR);
  Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 700, "Branche-moi en USB-C.", GRIS_FONCE);
}

void veille() {
  Memo& m = r_memo;
  memset(&m, 0, sizeof(m));
  m.magie = MAGIE;
  m.type = MEMO_VEILLE;
  struct tm t;
  Horloge::locale(t);
  m.heureValide = Horloge::valide();
  m.grandeHeure = Param.horlogeVeilleMin > 0;
  Horloge::texteHeure(m.heure, sizeof(m.heure), t);
  Horloge::texteDateLongue(m.date, sizeof(m.date), Horloge::dateDe(t), false);
  m.date[0] = (char)toupper((unsigned char)m.date[0]);
  if (Energie::mesureValide()) snprintf(m.batterie, sizeof(m.batterie), "Batterie %d %%", Energie::pourcentage());
  else snprintf(m.batterie, sizeof(m.batterie), "Batterie --");
  const uint32_t fin = Minuteries::prochaineEcheance();
  if (fin) {
    struct tm tf;
    Horloge::localeDe(fin, tf);
    char h[8];
    Horloge::texteHeure(h, sizeof(h), tf);
    if (Minuteries::pomoPhase() != POMO_ARRET && !Minuteries::pomoSuspendu()) {
      snprintf(m.minuterie, sizeof(m.minuterie), "%s jusqu'à %s", Minuteries::nomPhase(Minuteries::pomoPhase()), h);
    } else {
      snprintf(m.minuterie, sizeof(m.minuterie), "Minuteur jusqu'à %s", h);
    }
  }
  int indices[4];
  const int n = Stockage::listeTaches(TACHES_A_FAIRE, indices, 4);
  m.nbTaches = (uint8_t)n;
  for (int i = 0; i < n; i++) copier(m.taches[i], sizeof(m.taches[i]), Stockage::tache(indices[i])->titre);
  dessinerVeille(m);
}

void eteint() {
  r_memo.magie = MAGIE;
  r_memo.type = MEMO_ETEINT;
  dessinerEteint();
}

void batterieVide() {
  r_memo.magie = MAGIE;
  r_memo.type = MEMO_VIDE;
  dessinerVide();
}

bool redessinerMemorise() {
  if (r_memo.magie != MAGIE) return false;
  switch (r_memo.type) {
    case MEMO_VEILLE: dessinerVeille(r_memo); return true;
    case MEMO_ETEINT: dessinerEteint(); return true;
    case MEMO_VIDE:   dessinerVide(); return true;
    default:          return false;
  }
}

}  // namespace Ecrans
