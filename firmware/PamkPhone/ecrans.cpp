#include "ecrans.h"
#include "ecran.h"
#include "dessin.h"
#include "horloge.h"
#include "energie.h"
#include "stockage.h"
#include "minuteries.h"
#include "parametres.h"
#include "agenda.h"

namespace Ecrans {

// Ce que montre la dalle pendant le sommeil profond, gardé en mémoire RTC : au réveil on
// redessine exactement le même écran, puis on ne change que les pixels qui diffèrent.
enum TypeMemo : uint8_t { MEMO_AUCUN, MEMO_VEILLE, MEMO_ETEINT, MEMO_VIDE };

struct Memo {
  uint32_t magie;
  uint8_t type;
  bool heureValide;
  bool grandeHeure;            // heure en grand (mise à jour par minuterie) ou « en veille depuis »
  bool profonde;               // sans bouton : sommeil profond, réveil par le bouton de la carte
  char heure[8];
  char date[48];
  char batterie[24];
  char minuterie[64];          // ligne Pomodoro / minuteur ("" = aucune)
  char rdv[64];                // prochain rendez-vous de l'agenda ("" = aucun), 28/10/2026
  uint8_t nbTaches;
  char taches[4][TACHE_TITRE_MAX + 1];
  // 01/11/2026 : note épinglée (la plus récente) en post-it, idée reprise de FolloUp
  uint8_t nbEpinglees;
  char epTitre[64];
  char epLignes[3][72];        // début du texte, déjà coupé à la largeur du post-it
};

static const uint32_t MAGIE = 0x4D454D33;   // "MEM3" (note épinglée ajoutée au mémo)
static const int POSTIT_TEXTE_L = ECRAN_L - 2 * MARGE - 40;   // largeur du texte dans le post-it
static const int Y_FIN_VEILLE = 890;   // rien en dessous : la consigne de réveil est à 932
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
  if (m.rdv[0]) {   // carré creux : rendez-vous (le plein est la minuterie)
    Dessin::rect(MARGE, yy - 22, 16, 16, 3, NOIR);
    Dessin::texteTronque(F_GRAS, MARGE + 30, yy, ECRAN_L - 2 * MARGE - 30, m.rdv, NOIR);
    yy += 64;
  }
  if (m.nbEpinglees > 0) {   // post-it : cadre à coins coupés, titre, début du texte
    int nl = 0;
    while (nl < 3 && m.epLignes[nl][0]) nl++;
    const int haut = yy - 34, h = 62 + nl * 34;
    Dessin::chanfrein(MARGE, haut, ECRAN_L - 2 * MARGE, h, CHANFREIN, TRAIT, NOIR);
    Dessin::rectPlein(MARGE + 20, haut + 26, 10, 10, NOIR);   // la punaise
    int lTitre = POSTIT_TEXTE_L - 20;
    if (m.nbEpinglees > 1) {   // « +2 » : d'autres notes épinglées, à voir dans Notes
      char autres[8];
      snprintf(autres, sizeof(autres), "+%u", (unsigned)(m.nbEpinglees - 1));
      Dessin::texteDroite(F_PETITE, ECRAN_L - MARGE - 20, haut + 40, autres, GRIS_FONCE);
      lTitre -= Dessin::largeur(F_PETITE, autres) + 12;
    }
    Dessin::texteTronque(F_GRAS, MARGE + 40, haut + 42, lTitre, m.epTitre[0] ? m.epTitre : "(sans titre)", NOIR);
    for (int i = 0; i < nl; i++) Dessin::texte(F_PETITE, MARGE + 20, haut + 42 + 34 * (i + 1), m.epLignes[i], GRIS_FONCE);
    yy = haut + h + 52;
  }
  if (m.nbTaches > 0 && yy + 46 <= Y_FIN_VEILLE) {
    Dessin::texte(F_PIXEL, MARGE, yy, "À FAIRE", GRIS_FONCE);
    yy += 46;
    for (int i = 0; i < m.nbTaches && i < 4 && yy <= Y_FIN_VEILLE; i++) {
      Dessin::chanfrein(MARGE, yy - 22, 24, 24, 4, 2, NOIR);
      Dessin::texteTronque(F_TEXTE, MARGE + 40, yy, ECRAN_L - 2 * MARGE - 40, m.taches[i], NOIR);
      yy += 46;
    }
  }
  const char* reveil = "Touche l'écran pour réveiller";
  if (BOUTONS_PRESENTS) reveil = "Appuie sur le bouton du dessus pour réveiller";
  else if (m.profonde) reveil = "Appuie sur le bouton SENSOR_VN de la carte";
  Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 932, reveil, GRIS_FONCE);
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

void veille(bool profonde) {
  Memo& m = r_memo;
  memset(&m, 0, sizeof(m));
  m.magie = MAGIE;
  m.type = MEMO_VEILLE;
  m.profonde = profonde;
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
  Evenement ev;
  if (m.heureValide && Agenda::prochainEvenement(ev)) {
    // « Aujourd'hui 14:30 · Dentiste », « Demain · Férié »…
    char quand[32], h[8] = "";
    Horloge::texteDateRelative(quand, sizeof(quand), ev.date);
    quand[0] = (char)toupper((unsigned char)quand[0]);
    if (ev.debut >= 0) snprintf(h, sizeof(h), " %02d:%02d", ev.debut / 60, ev.debut % 60);
    char ligne[128];
    snprintf(ligne, sizeof(ligne), "%s%s · %s", quand, h, ev.titre);
    copier(m.rdv, sizeof(m.rdv), ligne);
  }
  int indices[4];
  const int n = Stockage::listeTaches(TACHES_A_FAIRE, indices, 4);
  m.nbTaches = (uint8_t)n;
  for (int i = 0; i < n; i++) copier(m.taches[i], sizeof(m.taches[i]), Stockage::tache(indices[i])->titre);
  // Note épinglée : titre, puis la suite du texte coupée en 3 lignes au plus (gardées telles
  // quelles dans le mémo, pour redessiner le même écran après un sommeil profond).
  int ep[NOTES_MAX];
  const int nEp = Stockage::notesEpinglees(ep, NOTES_MAX);
  m.nbEpinglees = (uint8_t)mini(nEp, 255);
  if (nEp > 0) {
    const Note* note = Stockage::note(ep[0]);
    Stockage::titreNote(note, m.epTitre, sizeof(m.epTitre));
    const char* suite = note && note->texte ? note->texte : "";
    while (*suite == ' ' || *suite == '\n') suite++;
    suite = strchr(suite, '\n');   // saute la ligne de titre
    if (suite) {
      while (*suite == ' ' || *suite == '\n') suite++;
      LigneTexte l[3];
      const int nl = Dessin::couperLignes(F_PETITE, suite, POSTIT_TEXTE_L, l, 3);
      for (int i = 0; i < nl && i < 3; i++) {
        char ligne[128];   // copier() coupe ensuite à 71 octets sans couper un caractère
        const int lg = mini(l[i].fin - l[i].debut, (int)sizeof(ligne) - 1);
        memcpy(ligne, suite + l[i].debut, lg);
        ligne[lg] = 0;
        copier(m.epLignes[i], sizeof(m.epLignes[i]), ligne);
      }
    }
  }
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

bool veilleProfonde() { return r_memo.magie == MAGIE && r_memo.type == MEMO_VEILLE && r_memo.profonde; }

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
