// Tâches : liste (à faire / faites), ajout au clavier, cocher, détail avec échéance, heure,
// répétition et rappel (28/10/2026), suppression.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"

namespace {

// Marques discrètes des listes (8 x 8, en pixels de 2 = hauteur des capitales de F_PETITE).
const char* const MOTIF_REPETITION[] = {   // flèche qui tourne
  "..###...", ".#...#..", "#...####", "#....##.", "#.......", "#......#", ".#....#.", "..####..",
};
const char* const MOTIF_RAPPEL[] = {       // cloche
  "...##...", "..####..", ".######.", ".######.", ".######.", "########", "........", "...##...",
};

// Heure d'une tâche : « 9:00 », « 14:30 ».
void texteHeureTache(int minutes, char* b, size_t n) { snprintf(b, n, "%d:%02d", minutes / 60, minutes % 60); }

// Coche ou décoche. Une tâche répétée ne disparaît pas quand on la coche : son échéance
// avance (Stockage::cocherTache), on dit quand elle revient.
void basculer(const Tache* t) {
  if (!t) return;
  const bool repetee = !t->faite && t->repetition != REP_AUCUNE;
  Stockage::cocherTache(t->id, !t->faite);
  if (repetee) {
    char d[24], m[48];
    Horloge::texteDateRelative(d, sizeof(d), t->echeance);   // t pointe toujours sur la tâche
    snprintf(m, sizeof(m), "Revient %s", d);
    Systeme::notifier(m);
  }
}

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
  const GFXfont* p = policeTexte();
  const int l = Dessin::texteTronque(p, x, y + 44, lmax, t->titre, t->faite ? GRIS : NOIR);
  if (t->faite) Dessin::rectPlein(x, y + 44 - Dessin::hautCapitale(p) / 2, l, 2, GRIS);   // barré
  // Dessous : échéance et heure, puis les marques de répétition et de rappel
  char sous[64] = "";
  bool retard = false;
  if (t->echeance) {
    char d[24];
    Horloge::texteDateRelative(d, sizeof(d), t->echeance);
    retard = !t->faite && t->echeance < Horloge::aujourdhui();
    snprintf(sous, sizeof(sous), "%s%s", retard ? "en retard · " : "", d);
  }
  if (t->heure >= 0) {
    char h[8];
    texteHeureTache(t->heure, h, sizeof(h));
    const size_t n = strlen(sous);
    snprintf(sous + n, sizeof(sous) - n, "%s%s", n ? " · " : "", h);
  }
  int xm = x;
  if (sous[0]) xm = Dessin::texte(F_PETITE, x, y + 76, sous, retard ? NOIR : GRIS_FONCE) + 12;
  if (t->repetition != REP_AUCUNE) {
    Dessin::motif(MOTIF_REPETITION, 8, xm, y + 60, 2, GRIS_FONCE);
    xm += 26;
  }
  if (t->rappel && !t->faite) Dessin::motif(MOTIF_RAPPEL, 8, xm, y + 60, 2, GRIS_FONCE);
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
      case L_COCHER:
        basculer(Stockage::tache(arg));
        Systeme::redessiner();
        break;
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
// De haut en bas : cadre du titre (case à cocher à gauche), échéance (- / + 1 jour, choix
// rapides), heure (- / + 15 min, choix rapides), répétition, rappel, dates, suppression.
enum CodeDetail {
  D_TITRE, D_FAITE, D_ECHEANCE, D_MOINS, D_PLUS, D_SUPPRIMER,
  D_HEURE, D_HEURE_MOINS, D_HEURE_PLUS, D_REPETER, D_RAPPEL,
};

const int DETAIL_Y = CONTENU_Y + 12;             // 144 : haut du cadre du titre
const int VALEUR_H = 60;                          // lignes « Échéance » et « Heure »
const int RAPIDES_H = 56;                         // rangées de choix rapides
const int BLOC_H = VALEUR_H + 4 + RAPIDES_H + 16; // 136 : une valeur et ses choix rapides
const int DATES_H = 62;                           // « Créée le » et « Modifiée le »
const int SUPPRIMER_Y = 860;
const int SOUS_TITRE_H = 2 * BLOC_H + 84 + 84 + DATES_H;   // 502 : tout ce qui suit le titre

const int HEURES_RAPIDES[4] = {-1, 9 * 60, 12 * 60, 18 * 60};
// Ordre du cycle « Répéter » : du plus fréquent au plus rare.
const uint8_t CYCLE_REPETITION[5] = {REP_AUCUNE, REP_JOUR, REP_JOURS_OUVRES, REP_SEMAINE, REP_MOIS};

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
    const int auj = Horloge::aujourdhui();
    int y = dessinerTitre(t);

    // Échéance
    etiquetteValeur(y, "Échéance");
    if (t->echeance) {
      char d[24];
      Horloge::texteDateRelative(d, sizeof(d), t->echeance);
      valeurAvecFleches(y, d, D_MOINS, D_PLUS);
    } else {
      valeurAucune(y);
    }
    static const char* const DATES[4] = {"Aucune", "Auj.", "Demain", "+7 j"};
    int choisi = -1;
    if (!t->echeance) choisi = 0;
    else if (t->echeance == auj) choisi = 1;
    else if (t->echeance == Horloge::ajouterJours(auj, 1)) choisi = 2;
    else if (t->echeance == Horloge::ajouterJours(auj, 7)) choisi = 3;
    choixRapides(y + VALEUR_H + 4, DATES, D_ECHEANCE, choisi);
    y += BLOC_H;

    // Heure
    etiquetteValeur(y, "Heure");
    if (t->heure >= 0) {
      char h[8];
      texteHeureTache(t->heure, h, sizeof(h));
      valeurAvecFleches(y, h, D_HEURE_MOINS, D_HEURE_PLUS);
    } else {
      valeurAucune(y);
    }
    static const char* const HEURES[4] = {"Aucune", "9:00", "12:00", "18:00"};
    choisi = -1;
    for (int i = 0; i < 4; i++) if (t->heure == HEURES_RAPIDES[i]) choisi = i;
    choixRapides(y + VALEUR_H + 4, HEURES, D_HEURE, choisi);
    y += BLOC_H;

    // Répétition (toucher : la suivante du cycle)
    char rep[40];
    if (t->repetition == REP_SEMAINE && t->echeance) {
      snprintf(rep, sizeof(rep), "chaque %s", Horloge::nomJour(Horloge::jourSemaine(t->echeance)));
    } else {
      Stockage::texteRepetition(t->repetition, rep, sizeof(rep));
    }
    UI::ligneReglage(y, "Répéter", rep, D_REPETER);
    y += 84;

    // Rappel : seulement avec une date et une heure
    if (t->echeance && t->heure >= 0) {
      char lib[48] = "Rappel à l'heure";
      const uint32_t m = Stockage::momentRappel(t);
      if (t->rappelReporte && m > Horloge::maintenant()) {
        struct tm hm;
        Horloge::localeDe(m, hm);
        snprintf(lib, sizeof(lib), "Rappel (reporté à %d:%02d)", hm.tm_hour, hm.tm_min);
      }
      UI::ligneInterrupteur(y, lib, t->rappel, D_RAPPEL);
    } else {
      const int base = Dessin::baseCentree(F_TEXTE, y, 84);
      Dessin::texte(F_TEXTE, MARGE, base, "Rappel", GRIS);
      Dessin::texteDroite(F_PETITE, ECRAN_L - MARGE, base, "choisis d'abord une heure", GRIS);
      UI::separateur(y + 82);
      UI::zone(boite(0, y, ECRAN_L, 84), D_RAPPEL);
    }
    y += 84;

    // Dates de création / modification
    char cr[40], md[40], ligne[96];
    dateHeure(t->cree, cr, sizeof(cr));
    dateHeure(t->maj, md, sizeof(md));
    snprintf(ligne, sizeof(ligne), "Créée le %s", cr);
    Dessin::texte(F_PETITE, MARGE, y + 28, ligne, GRIS_FONCE);
    snprintf(ligne, sizeof(ligne), "Modifiée le %s", md);
    Dessin::texte(F_PETITE, MARGE, y + 54, ligne, GRIS_FONCE);

    UI::bouton(boite(MARGE, SUPPRIMER_Y, ECRAN_L - 2 * MARGE, 76), "Supprimer la tâche", D_SUPPRIMER);
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
        basculer(t);
        break;
      case D_ECHEANCE: {
        // « Aucune » enlève aussi l'heure, le rappel et la répétition (Stockage::echeanceTache).
        static const int DECALAGE[4] = {-1, 0, 1, 7};
        const int k = borner(arg, 0, 3);
        Stockage::echeanceTache(m_id, k == 0 ? 0 : Horloge::ajouterJours(auj, DECALAGE[k]));
        break;
      }
      case D_MOINS:
        if (t->echeance) Stockage::echeanceTache(m_id, Horloge::ajouterJours(t->echeance, -1));
        break;
      case D_PLUS:
        if (t->echeance) Stockage::echeanceTache(m_id, Horloge::ajouterJours(t->echeance, 1));
        break;
      case D_HEURE:   // sans date, une heure met l'échéance à aujourd'hui (Stockage::heureTache)
        Stockage::heureTache(m_id, HEURES_RAPIDES[borner(arg, 0, 3)]);
        break;
      case D_HEURE_MOINS:   // au quart d'heure précédent / suivant, sans changer de jour
        if (t->heure >= 0) Stockage::heureTache(m_id, ((t->heure % 15 ? t->heure - t->heure % 15 : t->heure - 15) + 1440) % 1440);
        break;
      case D_HEURE_PLUS:
        if (t->heure >= 0) Stockage::heureTache(m_id, ((t->heure / 15 + 1) * 15) % 1440);
        break;
      case D_REPETER:
        Stockage::repetitionTache(m_id, repetitionSuivante(t->repetition));
        break;
      case D_RAPPEL:
        if (!t->echeance || t->heure < 0) {
          Systeme::notifier("Choisis d'abord une heure");
          return;
        }
        Stockage::rappelTache(m_id, !t->rappel);
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

  // Cadre du titre : case à cocher à gauche, titre à droite (toucher pour le modifier).
  // Autant de lignes que la place le permet (3 au plus), la dernière coupée par « … ».
  int dessinerTitre(const Tache* t) {
    const GFXfont* p = policeGras();
    const int il = Dessin::hauteurLigne(p) + 4;
    const int cap = Dessin::hautCapitale(p);
    const int haut = 30 + cap;                               // première ligne de base
    const int dispo = SUPPRIMER_Y - 8 - SOUS_TITRE_H - 12 - DETAIL_Y;
    const int nMax = borner(1 + (dispo - haut - 52) / il, 1, 3);
    const int xt = MARGE + 76;
    const int lt = ECRAN_L - MARGE - 20 - xt;
    LigneTexte l[3];
    const int n = maxi(1, Dessin::couperLignes(p, t->titre, lt, l, nMax));
    const int h = haut + (n - 1) * il + 52;
    const int y0 = DETAIL_Y;
    Dessin::chanfrein(MARGE, y0, ECRAN_L - 2 * MARGE, h, CHANFREIN, 2, GRIS);
    UI::caseACocher(MARGE + 18, y0 + haut - cap / 2 - CASE_TAILLE / 2, t->faite);
    UI::zone(boite(MARGE, y0, 76, h), D_FAITE);
    const uint8_t c = t->faite ? GRIS_FONCE : NOIR;
    const int total = (int)strlen(t->titre);
    for (int i = 0; i < n; i++) {
      const int base = y0 + haut + i * il;
      if (i == n - 1 && l[i].fin < total) Dessin::texteTronque(p, xt, base, lt, t->titre + l[i].debut, c);
      else Dessin::texteN(p, xt, base, t->titre + l[i].debut, l[i].fin - l[i].debut, c);
    }
    Dessin::texte(F_PETITE, xt, y0 + h - 16, "Toucher pour modifier", GRIS);
    UI::zone(boite(xt, y0, ECRAN_L - MARGE - xt, h), D_TITRE);
    return y0 + h + 12;
  }

  static void etiquetteValeur(int y, const char* libelle) {
    Dessin::texte(F_TEXTE, MARGE, Dessin::baseCentree(F_TEXTE, y, VALEUR_H), libelle, NOIR);
  }

  static void valeurAucune(int y) {
    Dessin::texteDroite(F_GRAS, ECRAN_L - MARGE, Dessin::baseCentree(F_GRAS, y, VALEUR_H), "aucune", GRIS_FONCE);
  }

  // « [−] valeur [+] » à droite d'une ligne de VALEUR_H.
  static void valeurAvecFleches(int y, const char* valeur, int codeMoins, int codePlus) {
    const int b = 52, lv = 176;
    const int xPlus = ECRAN_L - MARGE - b;
    const int xMoins = xPlus - 8 - lv - 8 - b;
    const int yb = y + (VALEUR_H - b) / 2;
    UI::bouton(boite(xMoins, yb, b, b), "−", codeMoins, 0, STYLE_DISCRET);
    UI::bouton(boite(xPlus, yb, b, b), "+", codePlus, 0, STYLE_DISCRET);
    Dessin::texteCentre(F_GRAS, xPlus - 8 - lv / 2, Dessin::baseCentree(F_GRAS, y, VALEUR_H), valeur, NOIR);
  }

  // Quatre choix rapides ; celui qui correspond à la valeur actuelle est plein.
  static void choixRapides(int y, const char* const* libelles, int code, int choisi) {
    const int lb = (ECRAN_L - 2 * MARGE - 3 * 12) / 4;
    for (int i = 0; i < 4; i++) {
      UI::bouton(boite(MARGE + i * (lb + 12), y, lb, RAPIDES_H), libelles[i], code, i,
                 i == choisi ? STYLE_PLEIN : STYLE_CONTOUR, F_PETITE);
    }
  }

  static uint8_t repetitionSuivante(uint8_t r) {
    for (int i = 0; i < 5; i++) {
      if (CYCLE_REPETITION[i] == r) return CYCLE_REPETITION[(i + 1) % 5];
    }
    return REP_AUCUNE;
  }

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
