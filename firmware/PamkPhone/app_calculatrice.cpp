// Calculatrice : + − × ÷ avec priorités, pourcentage, ±, virgule décimale.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include <math.h>
#include <cmath>

namespace {

enum TypeJeton : uint8_t { J_NOMBRE, J_OP };
struct Jeton {
  uint8_t type;
  char texte[20];   // nombre : chiffres, ',' et '-' éventuel ; opérateur : '+', '-', '*', '/'
};

const int JETONS_MAX = 24;
const int CHIFFRES_MAX = 12;

enum Touche : uint8_t {
  K_C, K_EFF, K_POURCENT, K_DIV,
  K_7, K_8, K_9, K_MUL,
  K_4, K_5, K_6, K_MOINS,
  K_1, K_2, K_3, K_PLUS,
  K_SIGNE, K_0, K_VIRGULE, K_EGAL,
};
const char* const LIBELLES[20] = {
  "C", "", "%", "÷",
  "7", "8", "9", "×",
  "4", "5", "6", "−",
  "1", "2", "3", "+",
  "±", "0", ",", "=",
};

const int AFFICHAGE_Y = CONTENU_Y;
const int CLAVIER_Y0 = 396;
const int TOUCHE_H = 92;
const int TOUCHE_E = 12;

// Formate un nombre : 10 chiffres significatifs, virgule française.
void formater(double v, char* b, size_t n) {
  if (v == 0) v = 0;   // pas de « -0 »
  if (fabs(v) >= 1e12 || (fabs(v) < 1e-9 && v != 0)) snprintf(b, n, "%.6e", v);
  else snprintf(b, n, "%.10g", v);
  for (char* p = b; *p; p++) if (*p == '.') *p = ',';
}

double valeur(const char* s) {
  char tmp[24];
  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char* p = tmp; *p; p++) if (*p == ',') *p = '.';
  return atof(tmp);
}

const char* symbole(char op) {
  switch (op) {
    case '+': return "+";
    case '-': return "\xE2\x88\x92";   // −
    case '*': return "\xC3\x97";       // ×
    case '/': return "\xC3\xB7";       // ÷
  }
  return "?";
}

class AppCalculatrice : public App {
 public:
  IdApp id() const override { return APP_CALCULATRICE; }

  void dessiner() override {
    UI::barreTitre("Calculatrice", true);
    // Ligne du dessus : expression précédente ou aperçu du résultat
    char haut[96] = "";
    if (m_resultat) {
      strncpy(haut, m_precedente, sizeof(haut) - 1);
      haut[sizeof(haut) - 1] = 0;
    } else if (m_nb > 1) {
      double r;
      if (evaluer(r)) {
        char v[32];
        formater(r, v, sizeof(v));
        snprintf(haut, sizeof(haut), "= %s", v);
      }
    }
    dessinerDroite(F_PIXEL, haut, AFFICHAGE_Y + 64, GRIS_FONCE);
    // Expression en cours (police aussi grande que possible)
    char expr[128];
    if (m_erreur) strcpy(expr, "Erreur");
    else construire(expr, sizeof(expr));
    if (!expr[0]) strcpy(expr, "0");
    const int lmax = ECRAN_L - 2 * MARGE;
    const GFXfont* p = F_GRANDE;
    if (Dessin::largeur(p, expr) > lmax) p = F_TITRE;
    if (Dessin::largeur(p, expr) > lmax) p = F_PIXEL;
    dessinerDroite(p, expr, AFFICHAGE_Y + 210, NOIR);
    UI::separateur(CLAVIER_Y0 - 22);
    // Clavier 4 x 5
    const int lt = (ECRAN_L - 2 * MARGE - 3 * TOUCHE_E) / 4;
    for (int i = 0; i < 20; i++) {
      const Boite b = boite(MARGE + (i % 4) * (lt + TOUCHE_E), CLAVIER_Y0 + (i / 4) * (TOUCHE_H + TOUCHE_E), lt, TOUCHE_H);
      const bool operateur = (i % 4) == 3;
      if (i == K_EGAL) Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, NOIR);
      else if (operateur || i < 4) Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, CHANFREIN, GRIS_CLAIR);
      else Dessin::chanfrein(b.x, b.y, b.l, b.h, CHANFREIN, TRAIT, NOIR);
      if (i == K_EFF) {
        Dessin::icone(ICONE_EFFACER, b.x + b.l / 2 - 25, b.y + b.h / 2 - 12, 5, NOIR);
      } else {
        Dessin::texteCentre(F_TITRE, b.x + b.l / 2, Dessin::baseCentree(F_TITRE, b.y, b.h), LIBELLES[i],
                            i == K_EGAL ? BLANC : NOIR);
      }
      UI::zone(b, i);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code < 0 || code >= 20) return;
    appuyer((Touche)code);
    Systeme::redessiner();
  }

  void appuiLong(int code, int arg) override {
    if (code == K_EFF) {   // appui long sur effacer = tout effacer
      appuyer(K_C);
      Systeme::redessiner();
      return;
    }
    action(code, arg);
  }

 private:
  Jeton m_j[JETONS_MAX];
  int m_nb = 0;
  bool m_resultat = false;    // le nombre affiché est un résultat
  bool m_erreur = false;
  char m_precedente[96] = "";

  Jeton* dernier() { return m_nb > 0 ? &m_j[m_nb - 1] : nullptr; }

  void dessinerDroite(const GFXfont* p, const char* s, int y, uint8_t c) {
    const int lmax = ECRAN_L - 2 * MARGE;
    // Trop long : on montre la fin
    const char* debut = s;
    while (*debut && Dessin::largeur(p, debut) > lmax) {
      const char* q = debut;
      Dessin::utf8Lire(q);
      debut = q;
    }
    Dessin::texteDroite(p, ECRAN_L - MARGE, y, debut, c);
  }

  void construire(char* b, size_t n) {
    b[0] = 0;
    for (int i = 0; i < m_nb; i++) {
      const size_t l = strlen(b);
      if (m_j[i].type == J_OP) {
        snprintf(b + l, n - l, " %s ", symbole(m_j[i].texte[0]));
      } else {
        const char* t = m_j[i].texte;
        if (t[0] == '-') snprintf(b + l, n - l, "\xE2\x88\x92%s", t + 1);
        else snprintf(b + l, n - l, "%s", t);
      }
    }
  }

  bool evaluer(double& r) {
    // Deux passes : × ÷ d'abord, puis + −. Un opérateur final est ignoré.
    double termes[JETONS_MAX];
    char ops[JETONS_MAX];
    int nt = 0, no = 0;
    int fin = m_nb;
    if (fin > 0 && m_j[fin - 1].type == J_OP) fin--;
    if (fin == 0) return false;
    double courant = valeur(m_j[0].texte);
    for (int i = 1; i + 1 < fin; i += 2) {   // nombre, op, nombre, op, nombre...
      const char op = m_j[i].texte[0];
      const double v = valeur(m_j[i + 1].texte);
      if (op == '*') courant *= v;
      else if (op == '/') {
        if (v == 0) return false;
        courant /= v;
      } else {
        termes[nt++] = courant;
        ops[no++] = op;
        courant = v;
      }
    }
    termes[nt++] = courant;
    r = termes[0];
    for (int i = 0; i < no; i++) r = (ops[i] == '+') ? r + termes[i + 1] : r - termes[i + 1];
    return std::isfinite(r);
  }

  void nouveauNombre(const char* t) {
    if (m_nb >= JETONS_MAX) return;
    m_j[m_nb].type = J_NOMBRE;
    strncpy(m_j[m_nb].texte, t, sizeof(m_j[m_nb].texte) - 1);
    m_j[m_nb].texte[sizeof(m_j[m_nb].texte) - 1] = 0;
    m_nb++;
  }

  int nbChiffres(const char* t) {
    int n = 0;
    for (; *t; t++) if (*t >= '0' && *t <= '9') n++;
    return n;
  }

  void appuyer(Touche k) {
    if (m_erreur && k != K_C) { m_erreur = false; m_nb = 0; }
    Jeton* d = dernier();
    if (k >= K_7 && k != K_MUL && k != K_MOINS && k != K_PLUS && k != K_SIGNE && k != K_VIRGULE && k != K_EGAL) {
      // Chiffre
      static const char CHIFFRE[20] = {0, 0, 0, 0, '7', '8', '9', 0, '4', '5', '6', 0, '1', '2', '3', 0, 0, '0', 0, 0};
      const char c = CHIFFRE[k];
      if (m_resultat) { m_nb = 0; m_resultat = false; d = nullptr; }
      if (d && d->type == J_NOMBRE) {
        if (nbChiffres(d->texte) >= CHIFFRES_MAX) return;
        size_t l = strlen(d->texte);
        if (strcmp(d->texte, "0") == 0 || strcmp(d->texte, "-0") == 0) l--;   // remplace le 0 initial
        d->texte[l] = c;
        d->texte[l + 1] = 0;
      } else {
        const char t[2] = {c, 0};
        nouveauNombre(t);
      }
      return;
    }
    switch (k) {
      case K_C:
        m_nb = 0;
        m_resultat = false;
        m_erreur = false;
        m_precedente[0] = 0;
        break;
      case K_EFF:
        m_resultat = false;
        if (!d) break;
        if (d->type == J_OP) { m_nb--; break; }
        {
          size_t l = strlen(d->texte);
          if (l > 0) d->texte[--l] = 0;
          if (l == 0 || strcmp(d->texte, "-") == 0) m_nb--;
        }
        break;
      case K_VIRGULE:
        if (m_resultat) { m_nb = 0; m_resultat = false; d = nullptr; }
        if (d && d->type == J_NOMBRE) {
          if (!strchr(d->texte, ',') && strlen(d->texte) + 1 < sizeof(d->texte)) strcat(d->texte, ",");
        } else {
          nouveauNombre("0,");
        }
        break;
      case K_SIGNE:
        if (d && d->type == J_NOMBRE) {
          if (d->texte[0] == '-') memmove(d->texte, d->texte + 1, strlen(d->texte));
          else if (strlen(d->texte) + 1 < sizeof(d->texte)) {
            memmove(d->texte + 1, d->texte, strlen(d->texte) + 1);
            d->texte[0] = '-';
          }
          m_resultat = false;
        }
        break;
      case K_POURCENT:
        if (d && d->type == J_NOMBRE) {
          formater(valeur(d->texte) / 100.0, d->texte, sizeof(d->texte));
          m_resultat = false;
        }
        break;
      case K_PLUS:
      case K_MOINS:
      case K_MUL:
      case K_DIV: {
        const char op = (k == K_PLUS) ? '+' : (k == K_MOINS) ? '-' : (k == K_MUL) ? '*' : '/';
        m_resultat = false;
        if (!d) {
          if (op == '-') nouveauNombre("-");   // nombre négatif en tête
          break;
        }
        if (d->type == J_OP) { d->texte[0] = op; break; }
        if (strcmp(d->texte, "-") == 0) break;
        if (m_nb < JETONS_MAX) {
          m_j[m_nb].type = J_OP;
          m_j[m_nb].texte[0] = op;
          m_j[m_nb].texte[1] = 0;
          m_nb++;
        }
        break;
      }
      case K_EGAL: {
        if (m_nb == 0) break;
        double r;
        construire(m_precedente, sizeof(m_precedente));
        if (!evaluer(r)) {
          m_erreur = true;
          m_nb = 0;
          break;
        }
        char v[20];
        formater(r, v, sizeof(v));
        m_nb = 0;
        nouveauNombre(v);
        m_resultat = true;
        break;
      }
      default:
        break;
    }
  }
};

AppCalculatrice g_calculatrice;

}  // namespace

App* appCalculatrice() { return &g_calculatrice; }
