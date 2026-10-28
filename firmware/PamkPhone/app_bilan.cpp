// Bilan de la semaine (28/10/2026) : comme « Temps d'écran », mais en positif. Minutes de
// concentration des 7 derniers jours en histogramme, totaux (minutes, pomodoros, tâches
// faites), comparaison avec la semaine d'avant, série de jours d'affilée avec un pomodoro.
// Rien ne bouge à l'écran : il est relu à chaque dessin (la barre d'état le redessine à la
// minute, rien n'est envoyé à l'e-paper si rien n'a changé).
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stats.h"

namespace {

enum Code { C_POMODORO };

const int NB_JOURS = 7;
const int JOURS_GARDES = 35;          // fenêtre de stats.json : la série ne remonte pas plus loin
const char* const INITIALES[7] = {"L", "M", "M", "J", "V", "S", "D"};

// Histogramme
const int HISTO_BAS = 630;            // pied des barres (axe)
const int HISTO_H = 180;              // hauteur de la barre la plus haute
const int BARRE_L = 40;
const int TUILES_Y = 736;
const int TUILES_H = 112;

JourStats g_jours[JOURS_GARDES];      // d'il y a 34 jours (0) à aujourd'hui (34)

struct Totaux {
  int minutes;
  int pomodoros;
  int taches;
};

Totaux additionner(const JourStats* j, int n) {
  Totaux t = {0, 0, 0};
  for (int i = 0; i < n; i++) {
    t.minutes += j[i].minutes;
    t.pomodoros += j[i].pomodoros;
    t.taches += j[i].taches;
  }
  return t;
}

// Jours d'affilée avec au moins un pomodoro. La journée n'est pas finie : sans pomodoro
// aujourd'hui, la série d'hier tient toujours. auMoins : elle remonte au-delà de la fenêtre.
int serie(const JourStats* j, int n, bool& auMoins) {
  int i = n - 1;
  if (i >= 0 && j[i].pomodoros == 0) i--;
  int s = 0;
  while (i >= 0 && j[i].pomodoros > 0) {
    s++;
    i--;
  }
  auMoins = s > 0 && i < 0;
  return s;
}

// « 45 min », « 2 h », « 2 h 05 »
void texteDuree(char* b, size_t n, int minutes) {
  if (minutes < 60) snprintf(b, n, "%d min", minutes);
  else if (minutes % 60 == 0) snprintf(b, n, "%d h", minutes / 60);
  else snprintf(b, n, "%d h %02d", minutes / 60, minutes % 60);
}

// Au-dessus d'une barre : « 45 », « 1h05 »
void texteCourt(char* b, size_t n, int minutes) {
  if (minutes < 60) snprintf(b, n, "%d", minutes);
  else snprintf(b, n, "%dh%02d", minutes / 60, minutes % 60);
}

// Total en grand, aligné à gauche : chiffres en VT323 125, unités en VT323 75.
void grandeDuree(int x, int yBase, int minutes) {
  char nb[8];
  if (minutes < 60) {
    snprintf(nb, sizeof(nb), "%d", minutes);
    x = Dessin::texte(F_GRANDE, x, yBase, nb, NOIR);   // texte() retourne le x de fin
    Dessin::texte(F_TITRE, x, yBase, " min", GRIS_FONCE);
    return;
  }
  snprintf(nb, sizeof(nb), "%d", minutes / 60);
  x = Dessin::texte(F_GRANDE, x, yBase, nb, NOIR);
  x = Dessin::texte(F_TITRE, x, yBase, minutes % 60 ? " h " : " h", GRIS_FONCE);
  if (minutes % 60) {
    snprintf(nb, sizeof(nb), "%02d", minutes % 60);
    Dessin::texte(F_GRANDE, x, yBase, nb, NOIR);
  }
}

// Comparaison avec la semaine d'avant, toujours sur une note positive.
void comparaison(char* b, size_t n, int cette, int avant) {
  char d[16];
  if (avant == 0) {
    snprintf(b, n, "Beau départ, continue comme ça !");
  } else if (cette > avant) {
    texteDuree(d, sizeof(d), cette - avant);
    snprintf(b, n, "%s de plus que la semaine d'avant", d);
  } else if (cette == avant) {
    snprintf(b, n, "Pile comme la semaine d'avant");
  } else {
    texteDuree(d, sizeof(d), avant);
    snprintf(b, n, "Semaine d'avant : %s · chaque minute compte", d);
  }
}

void tuile(int x, int l, int valeur, const char* libelle, bool plus) {
  Dessin::chanfrein(x, TUILES_Y, l, TUILES_H, CHANFREIN, 2, GRIS);
  char v[12];
  snprintf(v, sizeof(v), "%d%s", valeur, plus ? "+" : "");
  Dessin::texteCentre(F_TITRE, x + l / 2, TUILES_Y + 62, v, NOIR);
  Dessin::texteCentre(F_PETITE, x + l / 2, TUILES_Y + 94, libelle, GRIS_FONCE);
}

class AppBilan : public App {
 public:
  IdApp id() const override { return APP_BILAN; }

  void dessiner() override {
    UI::barreTitre("Bilan", true);
    if (Stats::derniersJours(g_jours, JOURS_GARDES) < JOURS_GARDES) {
      UI::messageVide(300, "Heure inconnue", "Règle l'heure pour suivre ta semaine.");
      return;
    }
    const JourStats* semaine = g_jours + JOURS_GARDES - NB_JOURS;   // les 7 derniers jours
    const Totaux t = additionner(semaine, NB_JOURS);
    const Totaux ta = additionner(semaine - NB_JOURS, NB_JOURS);    // les 7 d'avant
    if (t.minutes == 0 && t.pomodoros == 0 && t.taches == 0) {
      if (ta.minutes > 0) UI::messageVide(260, "Une semaine toute neuve", "Un pomodoro pour bien démarrer ?");
      else UI::messageVide(260, "Pas encore de bilan", "Lance un pomodoro : il comptera ici.");
      UI::bouton(boite(MARGE, 540, ECRAN_L - 2 * MARGE, 84), "Ouvrir le Pomodoro", C_POMODORO, 0, STYLE_PLEIN);
      return;
    }

    // Total de la semaine
    UI::etiquette(MARGE, CONTENU_Y + 54, "7 DERNIERS JOURS");
    Dessin::visagePamk(ECRAN_L - MARGE - 44, CONTENU_Y + 92, 8,
                       t.minutes >= ta.minutes ? VISAGE_CONTENT : VISAGE_NORMAL, NOIR);
    grandeDuree(MARGE, CONTENU_Y + 160, t.minutes);
    Dessin::texte(F_TEXTE, MARGE, CONTENU_Y + 200, "de concentration", GRIS_FONCE);
    char c[96];
    comparaison(c, sizeof(c), t.minutes, ta.minutes);
    Dessin::texteTronque(F_PETITE, MARGE, CONTENU_Y + 238, ECRAN_L - 2 * MARGE, c, NOIR);

    // Histogramme : aujourd'hui en noir (barre et initiale inversée), les autres en gris
    const int colL = (ECRAN_L - 2 * MARGE) / NB_JOURS;
    const int x0 = MARGE + (ECRAN_L - 2 * MARGE - colL * NB_JOURS) / 2;
    int echelle = 30;   // quelques minutes ne remplissent pas tout le graphique
    for (int i = 0; i < NB_JOURS; i++) echelle = maxi(echelle, semaine[i].minutes);
    UI::separateur(HISTO_BAS);
    for (int i = 0; i < NB_JOURS; i++) {
      const JourStats& j = semaine[i];
      const bool auj = i == NB_JOURS - 1;
      const int cx = x0 + i * colL + colL / 2;
      if (j.minutes > 0) {
        const int h = maxi(6, (int)((long)j.minutes * HISTO_H / echelle));
        Dessin::rectPlein(cx - BARRE_L / 2, HISTO_BAS - h, BARRE_L, h, auj ? NOIR : GRIS_FONCE);
        char v[12];
        texteCourt(v, sizeof(v), j.minutes);
        Dessin::texteCentre(F_PETITE, cx, HISTO_BAS - h - 10, v, auj ? NOIR : GRIS_FONCE);
      }
      const char* ini = INITIALES[Horloge::jourSemaine(j.date)];
      const int yb = Dessin::baseCentree(F_GRAS, HISTO_BAS + 16, 44);
      if (auj) {
        Dessin::chanfreinPlein(cx - 22, HISTO_BAS + 16, 44, 44, 7, NOIR);
        Dessin::texteCentre(F_GRAS, cx, yb, ini, BLANC);
      } else {
        Dessin::texteCentre(F_GRAS, cx, yb, ini, GRIS_FONCE);
      }
    }

    // Totaux
    bool auMoins = false;
    const int s = serie(g_jours, JOURS_GARDES, auMoins);
    const int lT = (ECRAN_L - 2 * MARGE - 2 * 12) / 3;
    tuile(MARGE, lT, t.pomodoros, t.pomodoros > 1 ? "pomodoros" : "pomodoro", false);
    tuile(MARGE + lT + 12, lT, t.taches, t.taches > 1 ? "tâches faites" : "tâche faite", false);
    tuile(MARGE + 2 * (lT + 12), lT, s, s > 1 ? "jours d'affilée" : "jour d'affilée", auMoins);

    // Petit mot du bas : garder la série, sinon le meilleur jour
    char m[80] = "";
    if (s > 0 && semaine[NB_JOURS - 1].pomodoros == 0) {
      snprintf(m, sizeof(m), "Un pomodoro aujourd'hui pour garder ta série !");
    } else {
      int meilleur = 0;
      for (int i = 1; i < NB_JOURS; i++) if (semaine[i].minutes > semaine[meilleur].minutes) meilleur = i;
      if (semaine[meilleur].minutes > 0) {
        char d[16];
        texteDuree(d, sizeof(d), semaine[meilleur].minutes);
        if (meilleur == NB_JOURS - 1) snprintf(m, sizeof(m), "Meilleur jour : aujourd'hui · %s", d);
        else snprintf(m, sizeof(m), "Meilleur jour : %s · %s", Horloge::nomJour(Horloge::jourSemaine(semaine[meilleur].date)), d);
      }
    }
    if (m[0]) Dessin::texteCentre(F_PETITE, ECRAN_L / 2, TUILES_Y + TUILES_H + 52, m, GRIS_FONCE);
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code == C_POMODORO) Systeme::ouvrir(appPomodoro());
  }
};

AppBilan g_bilan;

}  // namespace

App* appBilan() { return &g_bilan; }
