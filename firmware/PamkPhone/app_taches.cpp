// Tâches : liste (à faire / faites), ajout au clavier, cocher, détail avec échéance, suppression.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"

namespace {

// ---------------------------------------------------------------------------
//  Liste
// ---------------------------------------------------------------------------
enum CodeListe { L_NOUVELLE, L_ONGLET, L_COCHER, L_OUVRIR };

const int ONGLETS_Y = CONTENU_Y + 10;
const int ONGLETS_H = 56;
const int LISTE_Y = ONGLETS_Y + ONGLETS_H + 10;                 // 208
const int PAR_PAGE = (PIED_Y - LISTE_Y) / LIGNE_H;               // 7

int g_indices[TACHES_MAX];

// Dessine une ligne de tâche (utilisée aussi par le calendrier).
void ligneTache(int y, int index, int codeCocher, int codeOuvrir) {
  const Tache* t = Stockage::tache(index);
  if (!t) return;
  UI::caseACocher(MARGE, y + 18, t->faite);
  UI::zone(boite(0, y, 88, LIGNE_H), codeCocher, index);
  const int x = MARGE + CASE_TAILLE + 22;
  const int lmax = ECRAN_L - MARGE - x;
  const int l = Dessin::texteTronque(F_TEXTE, x, y + 44, lmax, t->titre, t->faite ? GRIS : NOIR);
  if (t->faite) Dessin::rectPlein(x, y + 34, l, 2, GRIS);   // barré
  char sous[48] = "";
  if (t->echeance) {
    char d[24];
    Horloge::texteDateRelative(d, sizeof(d), t->echeance);
    const bool retard = !t->faite && t->echeance < Horloge::aujourdhui();
    snprintf(sous, sizeof(sous), "%s%s", retard ? "en retard · " : "", d);
    Dessin::texte(F_PETITE, x, y + 76, sous, retard ? NOIR : GRIS_FONCE);
  }
  UI::separateur(y + LIGNE_H - 2, x, ECRAN_L - MARGE);
  UI::zone(boite(88, y, ECRAN_L - 88, LIGNE_H), codeOuvrir, index);
}

void rappelNouvelle(bool ok, const char* texte, void* ctx) {
  const int echeance = (int)(intptr_t)ctx;
  if (!ok || !texte || !*texte) return;
  if (!Stockage::ajouterTache(texte, echeance)) Systeme::notifier("Impossible d'ajouter la tâche");
}

class AppTaches : public App {
 public:
  IdApp id() const override { return APP_TACHES; }

  void entrer() override { m_pages.regler(compter(), PAR_PAGE); }

  void dessiner() override {
    UI::barreTitre("Tâches", true, "+", L_NOUVELLE);
    // Onglets
    char a[32], f[32];
    snprintf(a, sizeof(a), "À faire (%d)", Stockage::nbTachesAFaire());
    snprintf(f, sizeof(f), "Faites (%d)", Stockage::nbTachesFaites());
    const int lo = (ECRAN_L - 2 * MARGE - 12) / 2;
    UI::bouton(boite(MARGE, ONGLETS_Y, lo, ONGLETS_H), a, L_ONGLET, 0,
               m_filtre == TACHES_A_FAIRE ? STYLE_PLEIN : STYLE_DISCRET);
    UI::bouton(boite(MARGE + lo + 12, ONGLETS_Y, lo, ONGLETS_H), f, L_ONGLET, 1,
               m_filtre == TACHES_FAITES ? STYLE_PLEIN : STYLE_DISCRET);
    // Liste
    const int n = compter();
    m_pages.regler(n, PAR_PAGE);
    if (n == 0) {
      if (m_filtre == TACHES_A_FAIRE) UI::messageVide(LISTE_Y + 90, "Aucune tâche à faire", "Touche + pour en ajouter une.");
      else UI::messageVide(LISTE_Y + 90, "Rien de fini pour l'instant", "Coche une tâche quand c'est fait.");
    }
    for (int i = 0; i < PAR_PAGE; i++) {
      const int k = m_pages.page * PAR_PAGE + i;
      if (k >= n) break;
      ligneTache(LISTE_Y + i * LIGNE_H, g_indices[k], L_COCHER, L_OUVRIR);
    }
    UI::pagination(m_pages);
  }

  void action(int code, int arg) override {
    switch (code) {
      case L_NOUVELLE:
        Systeme::ouvrir(appClavier("Nouvelle tâche", "", TACHE_TITRE_MAX, SAISIE_LIGNE, rappelNouvelle, (void*)0));
        break;
      case L_ONGLET:
        m_filtre = arg ? TACHES_FAITES : TACHES_A_FAIRE;
        m_pages.page = 0;
        Systeme::redessiner();
        break;
      case L_COCHER: {
        const Tache* t = Stockage::tache(arg);
        if (t) Stockage::cocherTache(t->id, !t->faite);
        Systeme::redessiner();
        break;
      }
      case L_OUVRIR: {
        const Tache* t = Stockage::tache(arg);
        if (t) Systeme::ouvrir(appTache(t->id));
        break;
      }
      case CODE_PAGE_PREC: if (m_pages.precedente()) Systeme::redessiner(); break;
      case CODE_PAGE_SUIV: if (m_pages.suivante()) Systeme::redessiner(); break;
      default: break;
    }
  }

  void appuiLong(int code, int arg) override {
    if (code == L_OUVRIR || code == L_COCHER) {
      const Tache* t = Stockage::tache(arg);
      if (!t) return;
      strncpy(m_aSupprimer, t->id, sizeof(m_aSupprimer) - 1);
      m_aSupprimer[sizeof(m_aSupprimer) - 1] = 0;
      Systeme::ouvrir(appDialogue("Supprimer la tâche ?", t->titre, "Supprimer", confirmerSuppression, this));
      return;
    }
    action(code, arg);
  }

  bool bouton(Bouton b) override {
    const bool ok = (b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente();
    if (ok) Systeme::redessiner();
    return true;
  }

 private:
  FiltreTaches m_filtre = TACHES_A_FAIRE;
  Pages m_pages = {0, 1};
  char m_aSupprimer[ID_LONGUEUR + 1] = "";

  int compter() { return Stockage::listeTaches(m_filtre, g_indices, TACHES_MAX); }

  static void confirmerSuppression(bool oui, void* ctx) {
    AppTaches* moi = static_cast<AppTaches*>(ctx);
    if (oui && Stockage::supprimerTache(moi->m_aSupprimer)) Systeme::notifier("Tâche supprimée");
  }
};

AppTaches g_taches;

// ---------------------------------------------------------------------------
//  Détail d'une tâche
// ---------------------------------------------------------------------------
enum CodeDetail { D_TITRE, D_FAITE, D_ECHEANCE, D_MOINS, D_PLUS, D_SUPPRIMER };

class AppTache : public App {
 public:
  IdApp id() const override { return APP_TACHE; }
  bool reprendreApresVeille() const override { return false; }

  void choisir(const char* id) {
    strncpy(m_id, id ? id : "", sizeof(m_id) - 1);
    m_id[sizeof(m_id) - 1] = 0;
  }

  void dessiner() override {
    UI::barreTitre("Tâche", true);
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (!t) {
      UI::messageVide(300, "Tâche introuvable", "Elle a peut-être été supprimée.");
      return;
    }
    // Titre (toucher pour modifier)
    LigneTexte l[4];
    const int n = Dessin::couperLignes(F_GRAS, t->titre, ECRAN_L - 2 * MARGE - 40, l, 4);
    const int h = 40 + n * 38 + 34;
    const int y0 = CONTENU_Y + 16;
    Dessin::chanfrein(MARGE, y0, ECRAN_L - 2 * MARGE, h, CHANFREIN, 2, GRIS);
    for (int i = 0; i < n; i++) {
      Dessin::texteN(F_GRAS, MARGE + 20, y0 + 48 + i * 38, t->titre + l[i].debut, l[i].fin - l[i].debut,
                     t->faite ? GRIS_FONCE : NOIR);
    }
    Dessin::texte(F_PETITE, MARGE + 20, y0 + h - 16, "Toucher pour modifier", GRIS);
    UI::zone(boite(MARGE, y0, ECRAN_L - 2 * MARGE, h), D_TITRE);

    int y = y0 + h + 20;
    UI::ligneInterrupteur(y, "Faite", t->faite, D_FAITE);
    y += 84;
    // Échéance
    char e[48];
    if (t->echeance) Horloge::texteDateLongue(e, sizeof(e), t->echeance, true);
    else strcpy(e, "aucune");
    Dessin::texte(F_TEXTE, MARGE, y + 50, "Échéance", NOIR);
    Dessin::texteDroite(F_GRAS, ECRAN_L - MARGE, y + 50, e, GRIS_FONCE);
    y += 76;
    static const char* const RAPIDES[4] = {"Aucune", "Auj.", "Demain", "+7 j"};
    const int lb = (ECRAN_L - 2 * MARGE - 3 * 12) / 4;
    for (int i = 0; i < 4; i++) {
      UI::bouton(boite(MARGE + i * (lb + 12), y, lb, 64), RAPIDES[i], D_ECHEANCE, i, STYLE_CONTOUR, F_PETITE);
    }
    y += 80;
    if (t->echeance) {
      UI::bouton(boite(MARGE, y, 150, 64), "- 1 jour", D_MOINS, 0, STYLE_DISCRET, F_PETITE);
      UI::bouton(boite(ECRAN_L - MARGE - 150, y, 150, 64), "+ 1 jour", D_PLUS, 0, STYLE_DISCRET, F_PETITE);
      char c[24];
      Horloge::texteDateCourte(c, sizeof(c), t->echeance);
      Dessin::texteCentre(F_GRAS, ECRAN_L / 2, Dessin::baseCentree(F_GRAS, y, 64), c, NOIR);
      y += 80;
    }
    // Dates de création / modification
    char cr[40], md[40], ligne[96];
    dateHeure(t->cree, cr, sizeof(cr));
    dateHeure(t->maj, md, sizeof(md));
    snprintf(ligne, sizeof(ligne), "Créée le %s", cr);
    Dessin::texte(F_PETITE, MARGE, y + 30, ligne, GRIS_FONCE);
    snprintf(ligne, sizeof(ligne), "Modifiée le %s", md);
    Dessin::texte(F_PETITE, MARGE, y + 60, ligne, GRIS_FONCE);

    UI::bouton(boite(MARGE, 860, ECRAN_L - 2 * MARGE, 76), "Supprimer la tâche", D_SUPPRIMER);
  }

  void action(int code, int arg) override {
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (!t) return;
    const int auj = Horloge::aujourdhui();
    switch (code) {
      case D_TITRE:
        Systeme::ouvrir(appClavier("Modifier la tâche", t->titre, TACHE_TITRE_MAX, SAISIE_LIGNE, rappelTitre, this));
        return;
      case D_FAITE:
        Stockage::cocherTache(m_id, !t->faite);
        break;
      case D_ECHEANCE: {
        static const int DECALAGE[4] = {-1, 0, 1, 7};
        Stockage::echeanceTache(m_id, arg == 0 ? 0 : Horloge::ajouterJours(auj, DECALAGE[arg]));
        break;
      }
      case D_MOINS:
        if (t->echeance) Stockage::echeanceTache(m_id, Horloge::ajouterJours(t->echeance, -1));
        break;
      case D_PLUS:
        if (t->echeance) Stockage::echeanceTache(m_id, Horloge::ajouterJours(t->echeance, 1));
        break;
      case D_SUPPRIMER:
        Systeme::ouvrir(appDialogue("Supprimer la tâche ?", t->titre, "Supprimer", rappelSuppression, this));
        return;
      default:
        break;
    }
    Systeme::redessiner();
  }

  bool bouton(Bouton b) override {
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (t && t->echeance) {
      Stockage::echeanceTache(m_id, Horloge::ajouterJours(t->echeance, b == BOUTON_PLUS ? 1 : -1));
      Systeme::redessiner();
    }
    return true;
  }

 private:
  char m_id[ID_LONGUEUR + 1] = "";

  static void dateHeure(uint32_t utc, char* b, size_t n) {
    if (utc < 1600000000UL) { snprintf(b, n, "?"); return; }
    struct tm t;
    Horloge::localeDe(utc, t);
    char d[24], h[8];
    Horloge::texteDateCourte(d, sizeof(d), Horloge::dateDe(t));
    Horloge::texteHeure(h, sizeof(h), t);
    snprintf(b, n, "%s %d à %s", d, t.tm_year + 1900, h);
  }

  static void rappelTitre(bool ok, const char* texte, void* ctx) {
    AppTache* moi = static_cast<AppTache*>(ctx);
    if (ok && texte && *texte) Stockage::renommerTache(moi->m_id, texte);
  }

  static void rappelSuppression(bool oui, void* ctx) {
    AppTache* moi = static_cast<AppTache*>(ctx);
    if (!oui) return;
    Stockage::supprimerTache(moi->m_id);
    Systeme::retour();   // ferme le détail
    Systeme::notifier("Tâche supprimée");
  }
};

AppTache g_tache;

}  // namespace

App* appTaches() { return &g_taches; }

App* appTache(const char* id) {
  g_tache.choisir(id);
  return &g_tache;
}

// Pour le calendrier
void dessinerLigneTache(int y, int index, int codeCocher, int codeOuvrir) { ligneTache(y, index, codeCocher, codeOuvrir); }
void ajouterTacheAuClavier(int echeance) {
  Systeme::ouvrir(appClavier("Nouvelle tâche", "", TACHE_TITRE_MAX, SAISIE_LIGNE, rappelNouvelle,
                             (void*)(intptr_t)echeance));
}
