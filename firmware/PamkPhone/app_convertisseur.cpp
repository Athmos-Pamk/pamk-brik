// =============================================================================
//  Pamk Brik — Convertisseur d'unités
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "langue.h"
#include <math.h>

namespace {

enum Categorie {
  CAT_LONGUEUR = 0,
  CAT_MASSE,
  CAT_TEMP,
  CAT_VOLUME,
  CAT_VITESSE,
  CAT_DONNEES,
  CAT_NB
};

struct PaireUnites {
  const char* nomA;
  const char* nomB;
  double facteur; // B = A * facteur (sauf température)
};

const PaireUnites UNITES_PAR_CAT[CAT_NB][3] = {
  // Longueur
  {{"km", "mi", 0.621371}, {"m", "ft", 3.28084}, {"cm", "in", 0.393701}},
  // Masse
  {{"kg", "lb", 2.20462}, {"g", "oz", 0.035274}, {"t", "kg", 1000.0}},
  // Température
  {{"°C", "°F", 0.0}, {"°C", "K", 0.0}, {"", "", 0.0}},
  // Volume
  {{"L", "gal", 0.264172}, {"ml", "fl oz", 0.033814}, {"m³", "L", 1000.0}},
  // Vitesse
  {{"km/h", "mph", 0.621371}, {"m/s", "km/h", 3.6}, {"knots", "km/h", 1.852}},
  // Données
  {{"Go", "To", 0.0009765625}, {"Mo", "Go", 0.0009765625}, {"Ko", "Mo", 0.0009765625}},
};

enum Code {
  C_CAT_0 = 10,
  C_CAT_1,
  C_CAT_2,
  C_CAT_3,
  C_CAT_4,
  C_CAT_5,
  C_SOUS_UNITE_SUIV,
  C_INVERSER,
  C_CHIFFRE_0 = 20,
  // 20 à 29 = chiffres 0 à 9
  C_VIRGULE = 30,
  C_DOUBLE_ZERO,
  C_SIGNE,
  C_EFFACER,
  C_TOUT_EFFACER,
};

class AppConvertisseur : public App {
 public:
  IdApp id() const override { return APP_CONVERTISSEUR; }

  void dessiner() override {
    UI::barreTitre("Convertisseur", true);

    // Onglets de catégories en haut
    static const char* const NOMS_CAT[CAT_NB] = {"Dist", "Masse", "Temp", "Vol", "Vit", "Info"};
    const int tabL = (ECRAN_L - 2 * MARGE - (CAT_NB - 1) * 6) / CAT_NB;
    const int tabY = CONTENU_Y + 10;
    const int tabH = 46;

    for (int i = 0; i < CAT_NB; i++) {
      const int tx = MARGE + i * (tabL + 6);
      const bool actif = (i == m_cat);
      if (actif) {
        Dessin::chanfreinPlein(tx, tabY, tabL, tabH, 8, NOIR);
        Dessin::texteCentre(F_GRAS, tx + tabL / 2, tabY + 28, NOMS_CAT[i], BLANC);
      } else {
        Dessin::chanfrein(tx, tabY, tabL, tabH, 8, TRAIT, NOIR);
        Dessin::texteCentre(F_PETITE, tx + tabL / 2, tabY + 28, NOMS_CAT[i], NOIR);
      }
      UI::zone(boite(tx, tabY, tabL, tabH), C_CAT_0 + i);
    }

    const PaireUnites& u = UNITES_PAR_CAT[m_cat][m_sousIndex];
    const char* uniteDepart = m_inverse ? u.nomB : u.nomA;
    const char* uniteArrivee = m_inverse ? u.nomA : u.nomB;

    // Calcul de la conversion
    double valDepart = atof(m_saisie);
    double valArrivee = calculerConversion(valDepart);

    char txtResultat[32];
    formaterNombre(valArrivee, txtResultat, sizeof(txtResultat));

    // Carte 1 : Valeur source
    const int carteL = ECRAN_L - 2 * MARGE;
    const int carte1Y = tabY + tabH + 16;
    const int carteH = 126;
    Dessin::chanfrein(MARGE, carte1Y, carteL, carteH, 12, TRAIT, NOIR);
    Dessin::texte(F_PETITE, MARGE + 18, carte1Y + 30, "De :", GRIS_FONCE);
    Dessin::texte(F_GRAS, MARGE + 18, carte1Y + 92, uniteDepart, NOIR);

    const char* txtAffich = m_saisie[0] ? m_saisie : "0";
    const int largSaisie = Dessin::largeur(F_TITRE, txtAffich);
    Dessin::texte(F_TITRE, MARGE + carteL - 20 - largSaisie, carte1Y + 92, txtAffich, NOIR);

    // Bouton Inverser & Changer sous-unité au milieu
    const int miY = carte1Y + carteH + 8;
    const int miBtnL = (carteL - 12) / 2;
    UI::bouton(boite(MARGE, miY, miBtnL, 48), "⇄ Inverser", C_INVERSER, 0, STYLE_CONTOUR);
    UI::bouton(boite(MARGE + miBtnL + 12, miY, miBtnL, 48), "Autre unité", C_SOUS_UNITE_SUIV, 0, STYLE_CONTOUR);

    // Carte 2 : Résultat calculé
    const int carte2Y = miY + 56;
    Dessin::chanfrein(MARGE, carte2Y, carteL, carteH, 12, TRAIT, NOIR);
    Dessin::texte(F_PETITE, MARGE + 18, carte2Y + 30, "Vers :", GRIS_FONCE);

    char txtRatio[48];
    if (m_cat == CAT_TEMP) {
      snprintf(txtRatio, sizeof(txtRatio), TR("Formule : °F = °C × 1,8 + 32"));
    } else if (u.facteur > 0) {
      double r = m_inverse ? 1.0 / u.facteur : u.facteur;
      snprintf(txtRatio, sizeof(txtRatio), "1 %s = %.4g %s", uniteDepart, r, uniteArrivee);
    } else {
      txtRatio[0] = 0;
    }
    if (txtRatio[0]) Dessin::texte(F_PETITE, MARGE + 80, carte2Y + 30, txtRatio, GRIS);

    Dessin::texte(F_GRAS, MARGE + 18, carte2Y + 92, uniteArrivee, NOIR);

    const int largRes = Dessin::largeur(F_TITRE, txtResultat);
    Dessin::texte(F_TITRE, MARGE + carteL - 20 - largRes, carte2Y + 92, txtResultat, NOIR);

    // Clavier numérique
    dessinerClavier(carte2Y + carteH + 16);
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code >= C_CAT_0 && code < C_CAT_0 + CAT_NB) {
      m_cat = (Categorie)(code - C_CAT_0);
      m_sousIndex = 0;
      m_inverse = false;
      Systeme::redessiner();
      return;
    }

    if (code == C_SOUS_UNITE_SUIV) {
      int next = (m_sousIndex + 1) % 3;
      if (UNITES_PAR_CAT[m_cat][next].nomA[0] == 0) next = 0;
      m_sousIndex = next;
      Systeme::redessiner();
      return;
    }

    if (code == C_INVERSER) {
      m_inverse = !m_inverse;
      Systeme::redessiner();
      return;
    }

    if (code >= C_CHIFFRE_0 && code <= C_CHIFFRE_0 + 9) {
      int ch = code - C_CHIFFRE_0;
      size_t len = strlen(m_saisie);
      if (len < 10) {
        if (len == 1 && m_saisie[0] == '0') {
          m_saisie[0] = '0' + ch;
        } else {
          m_saisie[len] = '0' + ch;
          m_saisie[len + 1] = 0;
        }
        Systeme::redessiner();
      }
      return;
    }

    if (code == C_DOUBLE_ZERO) {
      size_t len = strlen(m_saisie);
      if (len > 0 && len <= 8 && strcmp(m_saisie, "0") != 0) {
        strcat(m_saisie, "00");
        Systeme::redessiner();
      }
      return;
    }

    if (code == C_VIRGULE) {
      if (!strchr(m_saisie, '.') && strlen(m_saisie) < 9) {
        size_t len = strlen(m_saisie);
        if (len == 0) {
          strcpy(m_saisie, "0.");
        } else {
          m_saisie[len] = '.';
          m_saisie[len + 1] = 0;
        }
        Systeme::redessiner();
      }
      return;
    }

    if (code == C_SIGNE) {
      if (m_saisie[0] == '-') {
        memmove(m_saisie, m_saisie + 1, strlen(m_saisie));
      } else if (strlen(m_saisie) < 10) {
        memmove(m_saisie + 1, m_saisie, strlen(m_saisie) + 1);
        m_saisie[0] = '-';
      }
      Systeme::redessiner();
      return;
    }

    if (code == C_EFFACER) {
      size_t len = strlen(m_saisie);
      if (len > 0) {
        m_saisie[len - 1] = 0;
        Systeme::redessiner();
      }
      return;
    }

    if (code == C_TOUT_EFFACER) {
      m_saisie[0] = 0;
      Systeme::redessiner();
      return;
    }
  }

 private:
  Categorie m_cat = CAT_LONGUEUR;
  int m_sousIndex = 0;
  bool m_inverse = false;
  char m_saisie[16] = "1";

  double calculerConversion(double val) {
    if (m_cat == CAT_TEMP) {
      if (m_sousIndex == 0) { // °C <-> °F
        if (!m_inverse) return (val * 9.0 / 5.0) + 32.0;
        else return (val - 32.0) * 5.0 / 9.0;
      } else { // °C <-> K
        if (!m_inverse) return val + 273.15;
        else return val - 273.15;
      }
    }

    const PaireUnites& u = UNITES_PAR_CAT[m_cat][m_sousIndex];
    if (u.facteur == 0.0) return 0.0;

    if (!m_inverse) {
      return val * u.facteur;
    } else {
      return val / u.facteur;
    }
  }

  void formaterNombre(double val, char* b, size_t n) {
    if (fabs(val) < 0.0001 && val != 0.0) {
      snprintf(b, n, "%.4e", val);
    } else if (fabs(val) >= 100000.0) {
      snprintf(b, n, "%.2f", val);
    } else {
      snprintf(b, n, "%.4g", val);
    }
  }

  void dessinerClavier(int y0) {
    const int nbCol = 4;
    const int nbLig = 4;
    const int ecart = 10;
    const int btnL = (ECRAN_L - 2 * MARGE - (nbCol - 1) * ecart) / nbCol;
    const int btnH = 68;

    struct Touche {
      const char* etiquette;
      int code;
    };

    static const Touche TOUCHES[nbLig][nbCol] = {
      {{"7", C_CHIFFRE_0 + 7}, {"8", C_CHIFFRE_0 + 8}, {"9", C_CHIFFRE_0 + 9}, {"⌫", C_EFFACER}},
      {{"4", C_CHIFFRE_0 + 4}, {"5", C_CHIFFRE_0 + 5}, {"6", C_CHIFFRE_0 + 6}, {"C", C_TOUT_EFFACER}},
      {{"1", C_CHIFFRE_0 + 1}, {"2", C_CHIFFRE_0 + 2}, {"3", C_CHIFFRE_0 + 3}, {"±", C_SIGNE}},
      {{".", C_VIRGULE}, {"0", C_CHIFFRE_0}, {"00", C_DOUBLE_ZERO}, {"⇄", C_INVERSER}},
    };

    for (int l = 0; l < nbLig; l++) {
      for (int c = 0; c < nbCol; c++) {
        const int bx = MARGE + c * (btnL + ecart);
        const int by = y0 + l * (btnH + ecart);
        const Touche& t = TOUCHES[l][c];

        Dessin::chanfrein(bx, by, btnL, btnH, 10, TRAIT, NOIR);
        Dessin::texteCentre(F_TITRE, bx + btnL / 2, by + 46, t.etiquette, NOIR);
        UI::zone(boite(bx, by, btnL, btnH), t.code);
      }
    }
  }
};

AppConvertisseur g_appConvertisseur;

} // namespace

App* appConvertisseur() { return &g_appConvertisseur; }
