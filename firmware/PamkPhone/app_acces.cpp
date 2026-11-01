// Accès depuis l'ordinateur : état, adresse, code en grand et mode d'emploi. L'accès tourne
// tant que cet écran est ouvert (entrer() le démarre, retour() l'arrête).
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "ecran.h"
#include "reseau.h"
#include "acces.h"

namespace {

enum Code { C_ARRETER, C_RELANCER };

void afficherMaintenant();

class AppAcces : public App {
 public:
  IdApp id() const override { return APP_ACCES; }
  bool reprendreApresVeille() const override { return false; }

  void entrer() override {
    Acces::definirRappelAffichage(afficherMaintenant);
    if (!Acces::actif()) Acces::demarrer();
  }

  void dessiner() override {
    memoriser();
    UI::barreTitre("Ordinateur", true);
    // État
    char etat[72];
    strncpy(etat, m_etat, sizeof(etat) - 1);
    etat[sizeof(etat) - 1] = 0;
    if (etat[0] >= 'a' && etat[0] <= 'z') etat[0] = (char)(etat[0] - 'a' + 'A');
    const int y0 = CONTENU_Y + 16;
    Dessin::pictoWifi(MARGE, y0 + 14, Reseau::barres(), Acces::pret() ? NOIR : GRIS_CLAIR);
    Dessin::texteTronque(F_GRAS, MARGE + 48, y0 + 34, ECRAN_L - 2 * MARGE - 48, etat, NOIR);
    UI::separateur(y0 + 62);
    // Adresse
    UI::etiquette(MARGE, 284, "ADRESSE");
    if (m_adresse[0]) {
      Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, 346, m_adresse, NOIR);
      Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 386, "ou http://pamk-brik.local", GRIS_FONCE);
    } else {
      Dessin::texteCentre(F_TEXTE, ECRAN_L / 2, 346, "en attente du Wi-Fi…", GRIS);
    }
    // Code
    UI::etiquette(MARGE, 450, "CODE");
    const int base = 474 + Dessin::hautCapitale(F_HORLOGE);
    Dessin::texteCentre(F_HORLOGE, ECRAN_L / 2, base, m_code[0] ? m_code : "----", m_code[0] ? NOIR : GRIS_CLAIR);
    // Progression d'une mise à jour
    const int pct = Acces::progression();
    if (pct >= 0) Dessin::barreProgression(boite(MARGE, base + 26, ECRAN_L - 2 * MARGE, 24), pct * 10, GRIS_CLAIR, NOIR);
    // Mode d'emploi
    if (!m_actif && Reseau::nbConnus() == 0) {
      Dessin::texte(F_TEXTE, MARGE, 740, "Ajoute d'abord ton réseau Wi-Fi", NOIR);
      Dessin::texte(F_TEXTE, MARGE, 780, "dans Réglages > Wi-Fi.", NOIR);
    } else {
      static const char* const AIDE[] = {
        "Sur l'ordinateur (même Wi-Fi) : ouvre l'adresse",
        "dans le navigateur, puis tape le code.",
        "Arduino IDE : Outils > Port > port réseau",
        "« pamk-brik » ; mot de passe = le code.",
        "Tout s'arrête quand tu quittes cet écran.",
      };
      for (int i = 0; i < 5; i++) Dessin::texte(F_PETITE, MARGE, 712 + i * 30, AIDE[i], i < 4 ? GRIS_FONCE : GRIS);
    }
    if (m_actif) UI::bouton(boite(MARGE, 868, ECRAN_L - 2 * MARGE, 72), "Arrêter l'accès", C_ARRETER);
    else UI::bouton(boite(MARGE, 868, ECRAN_L - 2 * MARGE, 72), "Relancer l'accès", C_RELANCER, 0, STYLE_PLEIN);
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code == C_ARRETER) {
      retour();
    } else if (code == C_RELANCER) {
      Acces::demarrer();
      Systeme::redessiner();
    }
  }

  bool retour() override {
    Acces::arreter();
    Systeme::retour();
    return true;
  }

  // Redessine seulement quand ce qui est affiché change (état, adresse, code, progression
  // par paliers de 10 % : c'est Acces qui arrondit).
  void seconde() override {
    if (strcmp(m_etat, Acces::etat()) != 0 || strcmp(m_adresse, Acces::adresse()) != 0 ||
        strcmp(m_code, Acces::code()) != 0 || m_actif != Acces::actif() || m_pct != Acces::progression()) {
      Systeme::redessiner();
    }
  }

 private:
  char m_etat[72] = "";
  char m_adresse[32] = "";
  char m_code[5] = "";
  bool m_actif = false;
  int m_pct = -1;

  static void copier(char* d, size_t n, const char* s) {
    strncpy(d, s ? s : "", n - 1);
    d[n - 1] = 0;
  }

  void memoriser() {
    copier(m_etat, sizeof(m_etat), Acces::etat());
    copier(m_adresse, sizeof(m_adresse), Acces::adresse());
    copier(m_code, sizeof(m_code), Acces::code());
    m_actif = Acces::actif();
    m_pct = Acces::progression();
  }
};

AppAcces g_acces;

// Pendant une mise à jour du firmware, la boucle principale est bloquée dans Acces::boucle() :
// on dessine directement (seuls les pixels qui changent sont envoyés à la dalle). La boucle
// refera l'image normale, avec ses zones tactiles, si la mise à jour échoue.
void afficherMaintenant() {
  if (Systeme::courante() != &g_acces) return;
  Ecran::effacer(BLANC);
  UI::viderZones();
  Dessin::sansDecoupe();
  UI::barreEtat(true);
  g_acces.dessiner();
  Ecran::rafraichir();
  Systeme::redessiner();
}

}  // namespace

App* appAcces() { return &g_acces; }
