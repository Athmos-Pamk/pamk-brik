// =============================================================================
//  Pamk Brik — Volet de réglages rapides (accessible en balayant vers le bas)
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "energie.h"
#include "reseau.h"
#include "parametres.h"
#include "meteo.h"
#include "langue.h"

namespace {

enum CodeVolet {
  V_FERMER,
  V_WIFI,
  V_NPD,
  V_NETTOYER,
  V_VEILLE,
  V_METEO,
  V_TOUS_REGLAGES,
};

class AppVolet : public App {
 public:
  IdApp id() const override { return APP_VOLET; }

  void dessiner() override {
    // Fond plein et délimiteur
    Dessin::rectPlein(0, 0, ECRAN_L, ECRAN_H, BLANC);

    // En-tête : Heure en grand, Date, Bouton Fermer
    const int yHaut = 16;
    if (Horloge::valide()) {
      struct tm t;
      Horloge::locale(t);
      char h[16], d[48];
      Horloge::texteHeure(h, sizeof(h), t);
      Horloge::texteDateLongue(d, sizeof(d), Horloge::dateDe(t), false);
      d[0] = (char)toupper((unsigned char)d[0]);
      Dessin::texte(F_TITRE, MARGE, yHaut + 44, h, NOIR);
      Dessin::texte(F_PETITE, MARGE, yHaut + 74, d, GRIS_FONCE);
    } else {
      Dessin::texte(F_TITRE, MARGE, yHaut + 44, "Réglages rapides", NOIR);
    }

    // Bouton Fermer [X] en haut à droite
    const Boite bFermer = boite(ECRAN_L - MARGE - 64, yHaut + 10, 64, 64);
    Dessin::chanfrein(bFermer.x, bFermer.y, bFermer.l, bFermer.h, CHANFREIN, TRAIT, NOIR);
    Dessin::texteCentre(F_GRAS, bFermer.x + bFermer.l / 2, bFermer.y + 44, "X", NOIR);
    UI::zone(bFermer, V_FERMER);

    UI::separateur(yHaut + 92);

    // Section 1 : Batterie & Autonomie
    const int yBatt = yHaut + 108;
    const Boite bBatt = boite(MARGE, yBatt, ECRAN_L - 2 * MARGE, 90);
    Dessin::chanfrein(bBatt.x, bBatt.y, bBatt.l, bBatt.h, CHANFREIN, TRAIT, NOIR);

    char pctTxt[16] = "--%";
    if (Energie::mesureValide()) snprintf(pctTxt, sizeof(pctTxt), "%d %%", Energie::pourcentage());
    Dessin::texte(F_TITRE, bBatt.x + 20, bBatt.y + 56, pctTxt, NOIR);

    char etatBatt[64] = "";
    if (Energie::enCharge()) {
      snprintf(etatBatt, sizeof(etatBatt), TR("En charge (USB)"));
    } else {
      char autoTxt[40] = "";
      if (Energie::texteAutonomie(autoTxt, sizeof(autoTxt))) {
        snprintf(etatBatt, sizeof(etatBatt), TR("%d mV · reste %s"), Energie::millivolts(), autoTxt);
      } else {
        snprintf(etatBatt, sizeof(etatBatt), TR("%d mV · sur batterie"), Energie::millivolts());
      }
    }
    Dessin::texte(F_PETITE, bBatt.x + 20, bBatt.y + 80, etatBatt, GRIS_FONCE);

    // Section 2 : 4 Raccourcis rapides (2 colonnes x 2 lignes)
    const int yGrille = yBatt + 110;
    const int tuileL = (ECRAN_L - 3 * MARGE) / 2;
    const int tuileH = 110;

    // Tuile 1 : Wi-Fi
    const Boite bWifi = boite(MARGE, yGrille, tuileL, tuileH);
    const bool wifiOn = Reseau::actif();
    const bool wifiConnecte = Reseau::etat() == RESEAU_CONNECTE;
    Dessin::chanfreinPlein(bWifi.x, bWifi.y, bWifi.l, bWifi.h, CHANFREIN, wifiConnecte ? NOIR : BLANC);
    if (!wifiConnecte) Dessin::chanfrein(bWifi.x, bWifi.y, bWifi.l, bWifi.h, CHANFREIN, TRAIT, NOIR);
    Dessin::texte(F_GRAS, bWifi.x + 16, bWifi.y + 44, "Wi-Fi", wifiConnecte ? BLANC : NOIR);
    char wifiInfo[32];
    if (wifiConnecte) snprintf(wifiInfo, sizeof(wifiInfo), "%s", Reseau::ssidActuel());
    else if (wifiOn) snprintf(wifiInfo, sizeof(wifiInfo), TR("Connexion..."));
    else snprintf(wifiInfo, sizeof(wifiInfo), TR("Éteint"));
    Dessin::texte(F_PETITE, bWifi.x + 16, bWifi.y + 80, wifiInfo, wifiConnecte ? GRIS_CLAIR : GRIS_FONCE);
    UI::zone(bWifi, V_WIFI);

    // Tuile 2 : Ne pas déranger
    const Boite bNpd = boite(MARGE + tuileL + MARGE, yGrille, tuileL, tuileH);
    const bool npdActif = Param.nePasDeranger;
    Dessin::chanfreinPlein(bNpd.x, bNpd.y, bNpd.l, bNpd.h, CHANFREIN, npdActif ? NOIR : BLANC);
    if (!npdActif) Dessin::chanfrein(bNpd.x, bNpd.y, bNpd.l, bNpd.h, CHANFREIN, TRAIT, NOIR);
    Dessin::texte(F_GRAS, bNpd.x + 16, bNpd.y + 44, "Silencieux", npdActif ? BLANC : NOIR);
    Dessin::texte(F_PETITE, bNpd.x + 16, bNpd.y + 80, npdActif ? "Ne pas déranger" : "Alertes actives",
                  npdActif ? GRIS_CLAIR : GRIS_FONCE);
    UI::zone(bNpd, V_NPD);

    // Tuile 3 : Nettoyer l'écran
    const Boite bNet = boite(MARGE, yGrille + tuileH + 16, tuileL, tuileH);
    Dessin::chanfrein(bNet.x, bNet.y, bNet.l, bNet.h, CHANFREIN, TRAIT, NOIR);
    Dessin::texte(F_GRAS, bNet.x + 16, bNet.y + 44, "Nettoyer", NOIR);
    Dessin::texte(F_PETITE, bNet.x + 16, bNet.y + 80, "Anti-fantôme", GRIS_FONCE);
    UI::zone(bNet, V_NETTOYER);

    // Tuile 4 : Mettre en veille
    const Boite bVeille = boite(MARGE + tuileL + MARGE, yGrille + tuileH + 16, tuileL, tuileH);
    Dessin::chanfrein(bVeille.x, bVeille.y, bVeille.l, bVeille.h, CHANFREIN, TRAIT, NOIR);
    Dessin::texte(F_GRAS, bVeille.x + 16, bVeille.y + 44, "Veille", NOIR);
    Dessin::texte(F_PETITE, bVeille.x + 16, bVeille.y + 80, "Écran de veille", GRIS_FONCE);
    UI::zone(bVeille, V_VEILLE);

    // Section 3 : Météo rapide
    const int yMeteo = yGrille + 2 * tuileH + 36;
    const Boite bMeteo = boite(MARGE, yMeteo, ECRAN_L - 2 * MARGE, 110);
    Dessin::chanfrein(bMeteo.x, bMeteo.y, bMeteo.l, bMeteo.h, CHANFREIN, TRAIT, NOIR);
    if (Meteo::disponible()) {
      Meteo::dessinerIcone(Meteo::codeWmo(), bMeteo.x + 18, bMeteo.y + 18, 6, NOIR);
      char tTxt[16];
      snprintf(tTxt, sizeof(tTxt), "%d°C", Meteo::temperature());
      Dessin::texte(F_TITRE, bMeteo.x + 110, bMeteo.y + 54, tTxt, NOIR);
      char det[48];
      snprintf(det, sizeof(det), "%s · min %d° max %d°",
               Meteo::texteCondition(Meteo::codeWmo()), Meteo::tempMin(), Meteo::tempMax());
      Dessin::texte(F_PETITE, bMeteo.x + 110, bMeteo.y + 86, det, GRIS_FONCE);
    } else {
      Dessin::icone(ICONE_METEO, bMeteo.x + 20, bMeteo.y + 25, 5, GRIS);
      Dessin::texte(F_GRAS, bMeteo.x + 100, bMeteo.y + 50, "Météo locale", NOIR);
      Dessin::texte(F_PETITE, bMeteo.x + 100, bMeteo.y + 80, "Synchroniser avec le Wi-Fi", GRIS_FONCE);
    }
    UI::zone(bMeteo, V_METEO);

    // Section 4 : Bouton "Tous les réglages" en bas
    const int yReg = yMeteo + 130;
    const Boite bReg = boite(MARGE, yReg, ECRAN_L - 2 * MARGE, 76);
    UI::bouton(bReg, "Tous les réglages...", V_TOUS_REGLAGES, 0, STYLE_DISCRET);

    // Invite de fermeture par glissé
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, ECRAN_H - 24, "^ Glisse vers le haut pour fermer ^", GRIS);
  }

  void action(int code, int arg) override {
    (void)arg;
    switch (code) {
      case V_FERMER:
        Systeme::retour();
        break;
      case V_WIFI:
        if (Reseau::actif()) {
          Reseau::eteindre();
        } else {
          Reseau::garderConnecte();
        }
        Systeme::redessiner();
        break;
      case V_NPD:
        Param.nePasDeranger = !Param.nePasDeranger;
        sauverParametres();
        Systeme::notifier(Param.nePasDeranger ? "Mode Ne pas déranger activé" : "Alertes et rappels réactivés");
        Systeme::redessiner();
        break;
      case V_NETTOYER:
        Systeme::nettoyerEcran();
        break;
      case V_VEILLE:
        Systeme::mettreEnVeille();
        break;
      case V_METEO:
        Systeme::ouvrir(appMeteo());
        break;
      case V_TOUS_REGLAGES:
        Systeme::ouvrir(appReglages());
        break;
    }
  }

  void glisser(int dx, int dy) override {
    (void)dx;
    if (dy < -50) Systeme::retour();   // glissé vers le haut ferme le volet
  }
};

AppVolet g_volet;

}  // namespace

App* appVolet() { return &g_volet; }
