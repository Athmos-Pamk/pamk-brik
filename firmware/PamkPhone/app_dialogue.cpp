// Fenêtre de confirmation (dessinée par-dessus l'appli précédente).
#include "app.h"
#include "systeme.h"
#include "ui.h"

namespace {

enum Code { C_ANNULER, C_OUI };
const Boite CADRE = {MARGE, 290, ECRAN_L - 2 * MARGE, 380};

class AppDialogue : public App {
 public:
  IdApp id() const override { return APP_DIALOGUE; }
  bool modale() const override { return true; }
  bool reprendreApresVeille() const override { return false; }

  void configurer(const char* titre, const char* message, const char* oui, RappelConfirmation rappel, void* ctx) {
    copier(m_titre, sizeof(m_titre), titre);
    copier(m_message, sizeof(m_message), message);
    copier(m_oui, sizeof(m_oui), oui ? oui : "Oui");
    m_rappel = rappel;
    m_ctx = ctx;
  }

  void dessiner() override {
    const Boite& c = CADRE;
    Dessin::chanfreinPlein(c.x - 6, c.y - 6, c.l + 12, c.h + 12, 20, BLANC);   // marge blanche autour
    Dessin::chanfreinPlein(c.x, c.y, c.l, c.h, 18, NOIR);
    Dessin::chanfreinPlein(c.x + 5, c.y + 5, c.l - 10, c.h - 10, 15, BLANC);
    Dessin::visagePamk(c.x + c.l - 70, c.y + 30, 6, VISAGE_NORMAL, GRIS);
    Dessin::texteTronque(F_GRAS, c.x + 28, c.y + 64, c.l - 140, m_titre, NOIR);
    LigneTexte lignes[5];
    const int n = Dessin::couperLignes(F_TEXTE, m_message, c.l - 56, lignes, 5);
    for (int i = 0; i < n; i++) {
      Dessin::texteN(F_TEXTE, c.x + 28, c.y + 120 + i * 38, m_message + lignes[i].debut,
                     lignes[i].fin - lignes[i].debut, GRIS_FONCE);
    }
    const int lb = (c.l - 28 * 2 - 20) / 2;
    UI::bouton(boite(c.x + 28, c.y + c.h - 96, lb, 68), "Annuler", C_ANNULER);
    UI::bouton(boite(c.x + 28 + lb + 20, c.y + c.h - 96, lb, 68), m_oui, C_OUI, 0, STYLE_PLEIN);
  }

  void action(int code, int arg) override {
    (void)arg;
    fermer(code == C_OUI);
  }

  bool retour() override {
    fermer(false);
    return true;
  }

 private:
  char m_titre[64] = "";
  char m_message[192] = "";
  char m_oui[24] = "Oui";
  RappelConfirmation m_rappel = nullptr;
  void* m_ctx = nullptr;

  static void copier(char* d, size_t n, const char* s) {
    strncpy(d, s ? s : "", n - 1);
    d[n - 1] = 0;
  }

  void fermer(bool oui) {
    Systeme::retour();
    if (m_rappel) m_rappel(oui, m_ctx);
  }
};

AppDialogue g_dialogue;

}  // namespace

App* appDialogue(const char* titre, const char* message, const char* oui, RappelConfirmation rappel, void* ctx) {
  g_dialogue.configurer(titre, message, oui, rappel, ctx);
  return &g_dialogue;
}
