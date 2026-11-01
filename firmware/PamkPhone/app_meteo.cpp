// =============================================================================
//  Pamk Brik — Écran Météo Multi-villes & Recherche
// =============================================================================
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "reseau.h"
#include "meteo.h"

namespace {

enum CodeMeteo {
  M_ACTUALISER,
  M_AJOUTER_VILLE,
  M_PRECEDENTE,
  M_SUIVANTE,
  M_SUPPRIMER_VILLE,
};

class AppMeteo : public App {
 public:
  IdApp id() const override { return APP_METEO; }

  void dessiner() override {
    UI::barreTitre("Météo", true, "Actualiser", M_ACTUALISER);

    // Bouton "+" pour ajouter une ville à gauche d'Actualiser
    const Boite bAjout = boite(ECRAN_L - MARGE - 160 - 10 - 64, BARRE_ETAT_H + 10, 64, 64);
    Dessin::chanfrein(bAjout.x, bAjout.y, bAjout.l, bAjout.h, CHANFREIN, TRAIT, NOIR);
    Dessin::pictoPlus(bAjout.x + bAjout.l / 2, bAjout.y + bAjout.h / 2, 28, 5, NOIR);
    UI::zone(bAjout, M_AJOUTER_VILLE);

    const int nb = Meteo::nbVilles();
    const int act = Meteo::villeActive();

    // Navigation entre villes si plusieurs villes
    if (nb > 1) {
      const Boite bG = boite(MARGE, CONTENU_Y + 24, 48, 52);
      const Boite bD = boite(ECRAN_L - MARGE - 48, CONTENU_Y + 24, 48, 52);
      Dessin::chanfrein(bG.x, bG.y, bG.l, bG.h, 6, TRAIT, GRIS);
      Dessin::icone(ICONE_CHEVRON_G, bG.x + (bG.l - 15) / 2, bG.y + (bG.h - 21) / 2, 3, NOIR);
      UI::zone(bG, M_PRECEDENTE);

      Dessin::chanfrein(bD.x, bD.y, bD.l, bD.h, 6, TRAIT, GRIS);
      Dessin::icone(ICONE_CHEVRON_D, bD.x + (bD.l - 15) / 2, bD.y + (bD.h - 21) / 2, 3, NOIR);
      UI::zone(bD, M_SUIVANTE);
    }

    // Lieu de prévision : ajustement automatique si nom très long
    const char* nomLieu = Meteo::lieu();
    const int lMaxNom = ECRAN_L - 2 * MARGE - (nb > 1 ? 120 : 20);
    const int lNom = Dessin::largeur(F_TITRE, nomLieu);
    if (lNom > lMaxNom) {
      // Trop long en F_TITRE : on affiche en F_GRAS, ou tronqué
      if (Dessin::largeur(F_GRAS, nomLieu) <= lMaxNom) {
        Dessin::texteCentre(F_GRAS, ECRAN_L / 2, CONTENU_Y + 44, nomLieu, NOIR);
      } else {
        const int xTxt = MARGE + (nb > 1 ? 60 : 10);
        Dessin::texteTronque(F_GRAS, xTxt, CONTENU_Y + 44, lMaxNom, nomLieu, NOIR);
      }
    } else {
      Dessin::texteCentre(F_TITRE, ECRAN_L / 2, CONTENU_Y + 44, nomLieu, NOIR);
    }

    // Coordonnées GPS sous le nom
    char subTxt[64];
    snprintf(subTxt, sizeof(subTxt), "%.2f°N, %.2f°E", Meteo::latitude(), Meteo::longitude());
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, CONTENU_Y + 74, subTxt, GRIS_FONCE);

    // Pastilles indicatrices des villes
    if (nb > 1) {
      const int rPuce = 5;
      const int ecartPuce = 18;
      const int lPuces = (nb - 1) * ecartPuce;
      const int x0Puce = (ECRAN_L - lPuces) / 2;
      for (int i = 0; i < nb; i++) {
        const int px = x0Puce + i * ecartPuce;
        if (i == act) Dessin::cerclePlein(px, CONTENU_Y + 94, rPuce, NOIR);
        else Dessin::cercle(px, CONTENU_Y + 94, rPuce, 1, GRIS);
      }
    }

    if (!Meteo::disponible()) {
      UI::messageVide(CONTENU_Y + 160, "Aucune donnée chargée",
                      "Touche « Actualiser » pour te connecter au Wi-Fi.");
      return;
    }

    const int yCentre = CONTENU_Y + 120;

    // Grande icône météo
    Meteo::dessinerIcone(Meteo::codeWmo(), (ECRAN_L - 12 * 10) / 2, yCentre, 10, NOIR);

    // Température actuelle
    char tTxt[16];
    snprintf(tTxt, sizeof(tTxt), "%d°C", Meteo::temperature());
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, yCentre + 180, tTxt, NOIR);

    // Condition
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, yCentre + 230,
                        Meteo::texteCondition(Meteo::codeWmo()), NOIR);

    // Min / Max
    char mmTxt[48];
    snprintf(mmTxt, sizeof(mmTxt), "Minimum %d°C · Maximum %d°C",
             Meteo::tempMin(), Meteo::tempMax());
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, yCentre + 270, mmTxt, GRIS_FONCE);

    UI::separateur(yCentre + 305);

    // Bloc infos
    const int yInfo = yCentre + 330;
    const Boite bInfo = boite(MARGE, yInfo, ECRAN_L - 2 * MARGE, 120);
    Dessin::chanfrein(bInfo.x, bInfo.y, bInfo.l, bInfo.h, CHANFREIN, TRAIT, NOIR);

    Dessin::texteTronque(F_GRAS, bInfo.x + 20, bInfo.y + 44, bInfo.l - 40, nomLieu, NOIR);
    char maj[64];
    if (Meteo::donnees().majUtc > 0) {
      struct tm t;
      Horloge::localeDe(Meteo::donnees().majUtc, t);
      snprintf(maj, sizeof(maj), "Mis à jour à %02d:%02d via Open-Meteo", t.tm_hour, t.tm_min);
    } else {
      snprintf(maj, sizeof(maj), "Fourni par Open-Meteo (sans clé)");
    }
    Dessin::texte(F_PETITE, bInfo.x + 20, bInfo.y + 80, maj, GRIS_FONCE);

    // Bouton supprimer la ville si plus d'une ville
    if (nb > 1) {
      const Boite bSup = boite(MARGE, yInfo + 134, ECRAN_L - 2 * MARGE, 56);
      Dessin::chanfrein(bSup.x, bSup.y, bSup.l, bSup.h, 8, TRAIT, GRIS_FONCE);
      Dessin::texteCentre(F_PETITE, bSup.x + bSup.l / 2, bSup.y + 36, "Retirer cette ville", GRIS_FONCE);
      UI::zone(bSup, M_SUPPRIMER_VILLE);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    switch (code) {
      case M_ACTUALISER:
        if (Reseau::etat() != RESEAU_CONNECTE) {
          Systeme::notifier("Connexion Wi-Fi...");
          Reseau::garderConnecte();
        } else {
          Systeme::notifier("Mise à jour météo...");
          if (Meteo::synchroniser()) {
            Systeme::notifier("Météo actualisée !");
          } else {
            Systeme::notifier("Erreur connexion météo");
          }
        }
        Systeme::redessiner();
        break;

      case M_AJOUTER_VILLE:
        Systeme::ouvrir(appClavier("Ajouter une ville", "", 32, SAISIE_LIGNE, rappelRechercheVille, this));
        break;

      case M_PRECEDENTE:
        Meteo::villePrecedente();
        Systeme::redessiner();
        break;

      case M_SUIVANTE:
        Meteo::villeSuivante();
        Systeme::redessiner();
        break;

      case M_SUPPRIMER_VILLE: {
        const char* nom = Meteo::lieu();
        char msg[64];
        snprintf(msg, sizeof(msg), "Supprimer %s ?", nom);
        Systeme::ouvrir(appDialogue(msg, "Elle ne sera plus suivie.", "Supprimer", rappelSuppression, this));
        break;
      }

      default:
        break;
    }
  }

  void glisser(int dx, int dy) override {
    if (abs(dx) > abs(dy) && abs(dx) > 40) {
      if (dx < 0) Meteo::villeSuivante();
      else Meteo::villePrecedente();
      Systeme::redessiner();
    }
  }

  void seconde() override {
    static bool s_tentee = false;
    if (Reseau::etat() == RESEAU_CONNECTE && !s_tentee) {
      s_tentee = true;
      if (Meteo::synchroniser()) {
        Systeme::notifier("Météo actualisée !");
        Systeme::redessiner();
      }
    } else if (Reseau::etat() != RESEAU_CONNECTE) {
      s_tentee = false;
    }
  }

 private:
  static void rappelRechercheVille(bool ok, const char* texte, void* ctx) {
    if (!ok || !texte || !*texte) return;
    char nomTrouve[36];
    float lat = 0.0f, lon = 0.0f;
    Systeme::notifier("Recherche de la ville...");
    if (Meteo::rechercherVille(texte, nomTrouve, sizeof(nomTrouve), lat, lon)) {
      if (Meteo::ajouterVille(nomTrouve, lat, lon)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s ajoutée !", nomTrouve);
        Systeme::notifier(msg);
        if (Reseau::etat() == RESEAU_CONNECTE) {
          Meteo::synchroniser();
        }
      } else {
        Systeme::notifier("Maximum 6 villes atteint");
      }
    } else {
      Systeme::notifier("Ville introuvable");
    }
    Systeme::redessiner();
  }

  static void rappelSuppression(bool oui, void* ctx) {
    if (oui) {
      Meteo::supprimerVille(Meteo::villeActive());
      Systeme::notifier("Ville retirée");
      Systeme::redessiner();
    }
  }
};

AppMeteo g_meteo;

}  // namespace

App* appMeteo() { return &g_meteo; }
