// Notes : liste (la plus récente d'abord), lecture page par page, écriture au clavier.
// La première ligne d'une note lui sert de titre.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"

namespace {

void dateCourte(uint32_t utc, char* b, size_t n) {
  if (utc < 1600000000UL) { b[0] = 0; return; }
  struct tm t;
  Horloge::localeDe(utc, t);
  const int d = Horloge::dateDe(t);
  if (d == Horloge::aujourdhui()) {
    Horloge::texteHeure(b, n, t);
  } else {
    int a, m, j;
    Horloge::decomposer(d, a, m, j);
    snprintf(b, n, "%d %s", j, Horloge::nomMoisCourt(m));
  }
}

void rappelNouvelle(bool ok, const char* texte, void* ctx) {
  (void)ctx;
  if (!ok || !texte) return;
  const char* p = texte;
  while (*p == ' ' || *p == '\n') p++;
  if (!*p) return;
  if (!Stockage::ajouterNote(texte)) Systeme::notifier("Impossible d'ajouter la note");
}

// ---------------------------------------------------------------------------
//  Liste
// ---------------------------------------------------------------------------
enum CodeListe { L_NOUVELLE, L_OUVRIR };
const int LISTE_Y = CONTENU_Y + 8;

// Mise en page d'une ligne de la liste selon la taille du texte (Réglages > Écran) :
// lignes de base du titre et de l'aperçu, hauteur de la ligne. 7 notes par page dans
// les deux cas (7 x 104 = 728 px sur les 732 disponibles).
struct Rang {
  int titre;
  int apercu;
  int h;
};
Rang rang() {
  if (policeGras() == F_GRAS) return {40, 74, LIGNE_H};   // titre 28 px
  return {48, 86, 104};                                     // titre 36 px
}
int parPage() { return (PIED_Y - LISTE_Y) / rang().h; }   // 7

class AppNotes : public App {
 public:
  IdApp id() const override { return APP_NOTES; }

  void entrer() override { m_pages.regler(Stockage::nbNotes(), parPage()); }

  void dessiner() override {
    UI::barreTitre("Notes", true, "+", L_NOUVELLE);
    const int n = Stockage::nbNotes();
    const Rang r = rang();
    const int parPg = parPage();
    m_pages.regler(n, parPg);
    if (n == 0) UI::messageVide(LISTE_Y + 120, "Aucune note", "Touche + pour en écrire une.");
    for (int i = 0; i < parPg; i++) {
      const int k = m_pages.page * parPg + i;
      if (k >= n) break;
      const Note* note = Stockage::note(k);
      const int y = LISTE_Y + i * r.h;
      char titre[96], date[24];
      Stockage::titreNote(note, titre, sizeof(titre));
      dateCourte(note->maj, date, sizeof(date));
      const int ld = Dessin::largeur(F_PETITE, date);
      Dessin::texteTronque(policeGras(), MARGE, y + r.titre, ECRAN_L - 2 * MARGE - ld - 16, titre[0] ? titre : "(sans titre)", NOIR);
      Dessin::texteDroite(F_PETITE, ECRAN_L - MARGE, y + r.titre, date, GRIS_FONCE);
      // Aperçu : la suite du texte, sur une ligne
      char apercu[96];
      apercuNote(note, apercu, sizeof(apercu));
      if (apercu[0]) Dessin::texteTronque(F_PETITE, MARGE, y + r.apercu, ECRAN_L - 2 * MARGE, apercu, GRIS_FONCE);
      UI::separateur(y + r.h - 2);
      UI::zone(boite(0, y, ECRAN_L, r.h), L_OUVRIR, k);
    }
    UI::pagination(m_pages);
  }

  void action(int code, int arg) override {
    switch (code) {
      case L_NOUVELLE:
        Systeme::ouvrir(appClavier("Nouvelle note", "", NOTE_TEXTE_MAX, SAISIE_TEXTE, rappelNouvelle, nullptr));
        break;
      case L_OUVRIR: {
        const Note* n = Stockage::note(arg);
        if (n) Systeme::ouvrir(appNote(n->id));
        break;
      }
      case CODE_PAGE_PREC: if (m_pages.precedente()) Systeme::redessiner(); break;
      case CODE_PAGE_SUIV: if (m_pages.suivante()) Systeme::redessiner(); break;
      default: break;
    }
  }

  void appuiLong(int code, int arg) override {
    if (code == L_OUVRIR) {
      const Note* n = Stockage::note(arg);
      if (!n) return;
      strncpy(m_aSupprimer, n->id, sizeof(m_aSupprimer) - 1);
      m_aSupprimer[sizeof(m_aSupprimer) - 1] = 0;
      char titre[96];
      Stockage::titreNote(n, titre, sizeof(titre));
      Systeme::ouvrir(appDialogue("Supprimer la note ?", titre, "Supprimer", confirmer, this));
      return;
    }
    action(code, arg);
  }

  bool bouton(Bouton b) override {
    if ((b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente()) Systeme::redessiner();
    return true;
  }

 private:
  Pages m_pages = {0, 1};
  char m_aSupprimer[ID_LONGUEUR + 1] = "";

  static void apercuNote(const Note* n, char* b, size_t taille) {
    b[0] = 0;
    if (!n || !n->texte) return;
    const char* p = n->texte;
    while (*p == ' ' || *p == '\n') p++;
    p = strchr(p, '\n');               // saute la ligne de titre
    if (!p) return;
    size_t o = 0;
    while (*p && o + 4 < taille) {
      char c = *p++;
      if (c == '\n') c = ' ';
      if (c == ' ' && (o == 0 || b[o - 1] == ' ')) continue;
      b[o++] = c;
    }
    while (o > 0 && (((uint8_t)b[o - 1]) & 0xC0) == 0x80) o--;     // pas de caractère coupé
    if (o > 0 && ((uint8_t)b[o - 1]) >= 0xC0) o--;
    b[o] = 0;
  }

  static void confirmer(bool oui, void* ctx) {
    AppNotes* moi = static_cast<AppNotes*>(ctx);
    if (oui && Stockage::supprimerNote(moi->m_aSupprimer)) Systeme::notifier("Note supprimée");
  }
};

AppNotes g_notes;

// ---------------------------------------------------------------------------
//  Lecture d'une note
// ---------------------------------------------------------------------------
enum CodeVue { V_MODIFIER, V_SUPPRIMER, V_TEXTE };
const int TEXTE_Y = CONTENU_Y + 16;
const int LIGNES_MAX = NOTE_TEXTE_MAX + 1;

// Selon la taille du texte (Réglages > Écran) : interligne = celui de la police + 4 px
// (38 pour 28 px, 47 pour 36 px) ; première ligne de base 11 px sous les majuscules.
int interligne() { return Dessin::hauteurLigne(policeTexte()) + 4; }
int premiereBase() { return TEXTE_Y + 11 + Dessin::hautCapitale(policeTexte()); }   // +30 / +37
int lignesPage() { return (PIED_Y - 8 - TEXTE_Y) / interligne(); }                  // 18 / 15

class AppNote : public App {
 public:
  IdApp id() const override { return APP_NOTE; }
  bool reprendreApresVeille() const override { return false; }

  void choisir(const char* id) {
    strncpy(m_id, id ? id : "", sizeof(m_id) - 1);
    m_id[sizeof(m_id) - 1] = 0;
    m_pages.page = 0;
  }

  void dessiner() override {
    const Note* n = Stockage::note(Stockage::indexNote(m_id));
    if (!n) {
      UI::barreTitre("Note", true);
      UI::messageVide(300, "Note introuvable", "Elle a peut-être été supprimée.");
      return;
    }
    char titre[96];
    Stockage::titreNote(n, titre, sizeof(titre));
    UI::barreTitre(titre[0] ? titre : "Note", true, "Modifier", V_MODIFIER);
    if (!m_lignes) m_lignes = (LigneTexte*)ps_malloc(sizeof(LigneTexte) * LIGNES_MAX);
    if (!m_lignes) return;
    const GFXfont* police = policeTexte();
    const int pas = interligne(), parPg = lignesPage(), base = premiereBase();
    const int nb = Dessin::couperLignes(police, n->texte, ECRAN_L - 2 * MARGE, m_lignes, LIGNES_MAX);
    m_pages.regler(nb, parPg);
    UI::zone(boite(0, TEXTE_Y, ECRAN_L, PIED_Y - TEXTE_Y), V_TEXTE);   // appui long = supprimer
    for (int i = 0; i < parPg; i++) {
      const int k = m_pages.page * parPg + i;
      if (k >= nb) break;
      Dessin::texteN(police, MARGE, base + i * pas, n->texte + m_lignes[k].debut,
                     m_lignes[k].fin - m_lignes[k].debut, NOIR);
    }
    // Pied : pages et suppression
    const int y = PIED_Y + 12;
    if (m_pages.nb > 1) {
      UI::pagination(m_pages);   // flèches à gauche et à droite, numéro au centre
    } else {
      UI::bouton(boite(ECRAN_L / 2 - 120, y, 240, 64), "Supprimer", V_SUPPRIMER, 0, STYLE_DISCRET);
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    const Note* n = Stockage::note(Stockage::indexNote(m_id));
    if (!n) return;
    switch (code) {
      case V_MODIFIER:
        Systeme::ouvrir(appClavier("Modifier la note", n->texte, NOTE_TEXTE_MAX, SAISIE_TEXTE, rappelModif, this));
        break;
      case V_SUPPRIMER:
        demanderSuppression();
        break;
      case CODE_PAGE_PREC: if (m_pages.precedente()) Systeme::redessiner(); break;
      case CODE_PAGE_SUIV: if (m_pages.suivante()) Systeme::redessiner(); break;
      default: break;
    }
  }

  void appuiLong(int code, int arg) override {
    if (code == V_TEXTE) demanderSuppression();   // appui long sur le texte : supprimer
    else action(code, arg);
  }

  bool bouton(Bouton b) override {
    if ((b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente()) Systeme::redessiner();
    return true;
  }

 private:
  char m_id[ID_LONGUEUR + 1] = "";
  Pages m_pages = {0, 1};
  LigneTexte* m_lignes = nullptr;

  void demanderSuppression() {
    const Note* n = Stockage::note(Stockage::indexNote(m_id));
    if (!n) return;
    char titre[96];
    Stockage::titreNote(n, titre, sizeof(titre));
    Systeme::ouvrir(appDialogue("Supprimer la note ?", titre, "Supprimer", rappelSuppression, this));
  }

  static void rappelModif(bool ok, const char* texte, void* ctx) {
    AppNote* moi = static_cast<AppNote*>(ctx);
    if (!ok || !texte) return;
    const char* p = texte;
    while (*p == ' ' || *p == '\n') p++;
    if (!*p) {   // texte vidé : on propose de supprimer
      moi->demanderSuppression();
      return;
    }
    Stockage::modifierNote(moi->m_id, texte);
  }

  static void rappelSuppression(bool oui, void* ctx) {
    AppNote* moi = static_cast<AppNote*>(ctx);
    if (!oui) return;
    Stockage::supprimerNote(moi->m_id);
    Systeme::retour();
    Systeme::notifier("Note supprimée");
  }
};

AppNote g_note;

}  // namespace

App* appNotes() { return &g_notes; }

App* appNote(const char* id) {
  g_note.choisir(id);
  return &g_note;
}
