// Tâches : liste (à faire / faites), ajout au clavier, cocher, détail avec échéance, heure,
// répétition et rappel (28/10/2026), suppression.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"
#include "langue.h"

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
    snprintf(m, sizeof(m), TR("Revient %s"), d);
    Systeme::notifier(m);
  }
}

// ---------------------------------------------------------------------------
//  Liste
// ---------------------------------------------------------------------------
enum CodeListe { L_NOUVELLE, L_ONGLET, L_COCHER, L_OUVRIR, L_RECHERCHER, L_EFFACER_RECHERCHE };

static int plierCar(const char*& s, char* sortie) {
  static const char BASE[] = "aaaaaa*ceeeeiiiidnooooo=ouuuuyt?";
  const uint32_t cp = Dessin::utf8Lire(s);
  if (cp >= 'A' && cp <= 'Z') { sortie[0] = (char)(cp + 32); return 1; }
  if (cp < 0x80) { sortie[0] = (char)cp; return 1; }
  char a = 0, b = 0;
  if (cp >= 0xC0 && cp <= 0xFF) {
    const char c = BASE[cp & 0x1F];
    if (c == '*') { a = 'a'; b = 'e'; }
    else if (c == '?') { if (cp == 0xDF) { a = 's'; b = 's'; } else { a = 'y'; } }
    else if (c != '=') a = c;
  } else if (cp == 0x152 || cp == 0x153) { a = 'o'; b = 'e'; }
  else if (cp == 0x178) { a = 'y'; }
  else if (cp == 0x2018 || cp == 0x2019) { a = '\''; }
  else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) { a = '-'; }
  else if (cp == 0xA0) { a = ' '; }
  if (a) {
    sortie[0] = a;
    if (!b) return 1;
    sortie[1] = b;
    return 2;
  }
  sortie[0] = '?';
  return 1;
}

static void plierChaine(const char* src, char* dst, size_t n) {
  size_t j = 0;
  const char* p = src;
  while (*p && j + 4 < n) {
    char b[4];
    const int k = plierCar(p, b);
    for (int i = 0; i < k && j + 1 < n; i++) dst[j++] = b[i];
  }
  dst[j] = 0;
}

static bool titreCorrespond(const char* titre, const char* requeteRepliee) {
  if (!requeteRepliee || !*requeteRepliee) return true;
  char titrePlie[TACHE_TITRE_MAX * 2];
  plierChaine(titre, titrePlie, sizeof(titrePlie));
  return strstr(titrePlie, requeteRepliee) != nullptr;
}

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
    snprintf(sous, sizeof(sous), "%s%s", retard ? TR("en retard · ") : "", d);
  }
  if (t->heure >= 0) {
    char h[8];
    texteHeureTache(t->heure, h, sizeof(h));
    const size_t n = strlen(sous);
    snprintf(sous + n, sizeof(sous) - n, "%s%s", n ? " · " : "", h);
  }
  int xm = x;
  if (sous[0]) {
    xm = Dessin::texte(F_PETITE, x, y + 76, sous, retard ? NOIR : GRIS_FONCE) + 12;
  } else if (t->description[0]) {
    xm = Dessin::texteTronque(F_PETITE, x, y + 76, ECRAN_L - MARGE - x - 30, t->description, GRIS_FONCE) + 12;
  }
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
  const Tache* t = Stockage::ajouterTache(texte, echeance);
  if (!t) {
    Systeme::notifier("Impossible d'ajouter la tâche");
  } else {
    Systeme::ouvrir(appTache(t->id, true));   // fiche en mode création (Annuler / Ajouter)
  }
}

class AppTaches : public App {
 public:
  IdApp id() const override { return APP_TACHES; }

  void entrer() override { m_pages.regler(compter(), PAR_PAGE); }

  void dessiner() override {
    UI::barreTitre("Tâches", true, "+", L_NOUVELLE);

    // Bouton Loupe (Recherche) à gauche du bouton +
    const Boite bRech = boite(ECRAN_L - MARGE - 68 - 12 - 68, BARRE_ETAT_H + 10, 68, 64);
    const bool rechActive = m_recherchePliee[0] != 0;
    if (rechActive) {
      Dessin::chanfreinPlein(bRech.x, bRech.y, bRech.l, bRech.h, CHANFREIN, NOIR);
      Dessin::icone(ICONE_RECHERCHE, bRech.x + (bRech.l - 60) / 2 + 18, bRech.y + 14, 3, BLANC);
    } else {
      Dessin::chanfrein(bRech.x, bRech.y, bRech.l, bRech.h, CHANFREIN, TRAIT, NOIR);
      Dessin::icone(ICONE_RECHERCHE, bRech.x + (bRech.l - 60) / 2 + 18, bRech.y + 14, 3, NOIR);
    }
    UI::zone(bRech, L_RECHERCHER);

    // Onglets
    char a[32], f[32];
    snprintf(a, sizeof(a), TR("À faire (%d)"), Stockage::nbTachesAFaire());
    snprintf(f, sizeof(f), TR("Faites (%d)"), Stockage::nbTachesFaites());
    const int lo = (ECRAN_L - 2 * MARGE - 12) / 2;
    UI::bouton(boite(MARGE, ONGLETS_Y, lo, ONGLETS_H), a, L_ONGLET, 0,
               m_filtre == TACHES_A_FAIRE ? STYLE_PLEIN : STYLE_DISCRET);
    UI::bouton(boite(MARGE + lo + 12, ONGLETS_Y, lo, ONGLETS_H), f, L_ONGLET, 1,
               m_filtre == TACHES_FAITES ? STYLE_PLEIN : STYLE_DISCRET);

    // Bandeau d'indication si filtre de recherche actif
    if (rechActive) {
      const int yBandeau = ONGLETS_Y + ONGLETS_H + 6;
      char txtFiltre[64];
      snprintf(txtFiltre, sizeof(txtFiltre), TR("Recherche : « %s »"), m_recherche);
      Dessin::texte(F_PETITE, MARGE, yBandeau + 22, txtFiltre, NOIR);
      const Boite bEff = boite(ECRAN_L - MARGE - 100, yBandeau + 2, 100, 32);
      Dessin::chanfrein(bEff.x, bEff.y, bEff.l, bEff.h, 6, TRAIT, GRIS_FONCE);
      Dessin::texteCentre(F_PETITE, bEff.x + bEff.l / 2, bEff.y + 22, "Effacer X", NOIR);
      UI::zone(bEff, L_EFFACER_RECHERCHE);
    }

    // Liste
    const int n = compter();
    m_pages.regler(n, PAR_PAGE);
    if (n == 0) {
      if (rechActive) UI::messageVide(LISTE_Y + 90, "Aucune tâche trouvée", "Touche « Effacer » pour réafficher la liste.");
      else if (m_filtre == TACHES_A_FAIRE) UI::messageVide(LISTE_Y + 90, "Aucune tâche à faire", "Touche + pour en ajouter une.");
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
      case L_NOUVELLE:   // d'abord le nom, puis la fiche en mode création
        ajouterTacheAuClavier(0);
        break;
      case L_RECHERCHER:
        Systeme::ouvrir(appClavier("Rechercher une tâche", m_recherche, 40, SAISIE_LIGNE, rappelRecherche, this));
        break;
      case L_EFFACER_RECHERCHE:
        m_recherche[0] = 0;
        m_recherchePliee[0] = 0;
        m_pages.page = 0;
        Systeme::redessiner();
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
  char m_recherche[48] = "";
  char m_recherchePliee[48] = "";

  int compter() {
    const int total = Stockage::listeTaches(m_filtre, g_indices, TACHES_MAX);
    if (!m_recherchePliee[0]) return total;
    int retenus = 0;
    for (int i = 0; i < total; i++) {
      const Tache* t = Stockage::tache(g_indices[i]);
      if (t && titreCorrespond(t->titre, m_recherchePliee)) {
        g_indices[retenus++] = g_indices[i];
      }
    }
    return retenus;
  }

  static void rappelRecherche(bool ok, const char* texte, void* ctx) {
    AppTaches* moi = static_cast<AppTaches*>(ctx);
    if (!ok || !texte) return;
    strncpy(moi->m_recherche, texte, sizeof(moi->m_recherche) - 1);
    moi->m_recherche[sizeof(moi->m_recherche) - 1] = 0;
    plierChaine(moi->m_recherche, moi->m_recherchePliee, sizeof(moi->m_recherchePliee));
    moi->m_pages.page = 0;
    Systeme::redessiner();
  }

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
  D_TITRE, D_FAITE, D_DESCRIPTION, D_ECHEANCE, D_MOINS, D_PLUS, D_SUPPRIMER,
  D_HEURE, D_HEURE_SAISIR, D_HEURE_MOINS_H, D_HEURE_PLUS_H, D_HEURE_MOINS_M, D_HEURE_PLUS_M, D_HEURE_EFFACER,
  D_REPETER,
  D_JOUR_0, D_JOUR_1, D_JOUR_2, D_JOUR_3, D_JOUR_4, D_JOUR_5, D_JOUR_6,
  D_RAPPEL,
  D_ANNULER, D_AJOUTER,   // fiche d'une tâche qu'on vient de créer (01/11/2026)
};

const int DETAIL_Y = CONTENU_Y + 12;             // 144 : haut du cadre du titre
const int VALEUR_H = 54;                          // lignes « Échéance » et « Heure »
const int RAPIDES_H = 48;                         // rangées de choix rapides
const int BLOC_H = VALEUR_H + 2 + RAPIDES_H + 10; // une valeur et ses choix rapides
const int DATES_H = 52;                           // « Créée le » et « Modifiée le »
const int SUPPRIMER_Y = 860;
const int SOUS_TITRE_H = 2 * BLOC_H + 84 + 84 + DATES_H;

const int HEURES_RAPIDES[4] = {-1, 9 * 60, 12 * 60, 18 * 60};
// Ordre du cycle « Répéter » : du plus fréquent au plus rare.
const uint8_t CYCLE_REPETITION[5] = {REP_AUCUNE, REP_JOUR, REP_JOURS_OUVRES, REP_SEMAINE, REP_MOIS};

class AppTache : public App {
 public:
  IdApp id() const override { return APP_TACHE; }
  bool reprendreApresVeille() const override { return false; }

  void choisir(const char* id, bool nouvelle) {
    strncpy(m_id, id ? id : "", sizeof(m_id) - 1);
    m_id[sizeof(m_id) - 1] = 0;
    m_nouvelle = nouvelle;
  }

  void dessiner() override {
    UI::barreTitre(m_nouvelle ? "Nouvelle tâche" : "Tâche", true);
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (!t) {
      UI::messageVide(300, "Tâche introuvable", "Elle a peut-être été supprimée.");
      return;
    }
    const int auj = Horloge::aujourdhui();
    int y = dessinerTitre(t);

    // Description visible dans son cadre dédié (sur 1 ou 2 lignes)
    const Boite bDesc = boite(MARGE, y, ECRAN_L - 2 * MARGE, 66);
    Dessin::chanfrein(bDesc.x, bDesc.y, bDesc.l, bDesc.h, 10, 2, GRIS);
    if (t->description[0]) {
      LigneTexte lDesc[2];
      const int nbL = Dessin::couperLignes(F_TEXTE, t->description, bDesc.l - 32, lDesc, 2);
      if (nbL <= 1) {
        Dessin::texteTronque(F_TEXTE, bDesc.x + 16, bDesc.y + 40, bDesc.l - 32, t->description, NOIR);
      } else {
        Dessin::texteN(F_TEXTE, bDesc.x + 16, bDesc.y + 28, t->description + lDesc[0].debut, lDesc[0].fin - lDesc[0].debut, NOIR);
        Dessin::texteTronque(F_TEXTE, bDesc.x + 16, bDesc.y + 54, bDesc.l - 32, t->description + lDesc[1].debut, NOIR);
      }
    } else {
      Dessin::texte(F_TEXTE, bDesc.x + 16, bDesc.y + 41, "Ajouter une description...", GRIS);
    }
    UI::zone(bDesc, D_DESCRIPTION);
    y += 66 + 10;

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
    choixRapides(y + VALEUR_H + 2, DATES, D_ECHEANCE, choisi);
    y += BLOC_H;

    // Heure (toucher l'heure permet de taper directement 7:15, 14h30, etc.)
    etiquetteValeur(y, "Heure");
    if (t->heure >= 0) {
      char h[8];
      texteHeureTache(t->heure, h, sizeof(h));
      // Cadre cliquable pour taper l'heure exacte
      const int bW = 50, lvW = 160;
      const int xPlus = ECRAN_L - MARGE - bW;
      const int xVal = xPlus - 8 - lvW;
      const int xMoins = xVal - 8 - bW;
      const int yb = y + (VALEUR_H - bW) / 2;
      UI::bouton(boite(xMoins, yb, bW, bW), "−1h", D_HEURE_MOINS_H, 0, STYLE_DISCRET, F_PETITE);
      UI::bouton(boite(xPlus, yb, bW, bW), "+1h", D_HEURE_PLUS_H, 0, STYLE_DISCRET, F_PETITE);

      const Boite bVal = boite(xVal, yb, lvW, bW);
      Dessin::chanfrein(bVal.x, bVal.y, bVal.l, bVal.h, 8, TRAIT, NOIR);
      Dessin::texteCentre(F_GRAS, bVal.x + bVal.l / 2, Dessin::baseCentree(F_GRAS, bVal.y, bVal.h), h, NOIR);
      UI::zone(bVal, D_HEURE_SAISIR);
    } else {
      const Boite bDef = boite(ECRAN_L - MARGE - 160, y + (VALEUR_H - 44) / 2, 160, 44);
      Dessin::chanfrein(bDef.x, bDef.y, bDef.l, bDef.h, 8, TRAIT, GRIS);
      Dessin::texteCentre(F_PETITE, bDef.x + bDef.l / 2, Dessin::baseCentree(F_PETITE, bDef.y, bDef.h), "Régler l'heure", NOIR);
      UI::zone(bDef, D_HEURE_SAISIR);
    }

    // Rangée de réglage fin de l'heure
    if (t->heure >= 0) {
      const int lb = (ECRAN_L - 2 * MARGE - 3 * 10) / 4;
      const int yRap = y + VALEUR_H + 2;
      UI::bouton(boite(MARGE, yRap, lb, RAPIDES_H), "−15m", D_HEURE_MOINS_M, 0, STYLE_CONTOUR, F_PETITE);
      UI::bouton(boite(MARGE + lb + 10, yRap, lb, RAPIDES_H), "+15m", D_HEURE_PLUS_M, 0, STYLE_CONTOUR, F_PETITE);
      UI::bouton(boite(MARGE + 2 * (lb + 10), yRap, lb, RAPIDES_H), "Taper", D_HEURE_SAISIR, 0, STYLE_CONTOUR, F_PETITE);
      UI::bouton(boite(MARGE + 3 * (lb + 10), yRap, lb, RAPIDES_H), "Effacer", D_HEURE_EFFACER, 0, STYLE_DISCRET, F_PETITE);
    } else {
      static const char* const HEURES_DEFAUT[4] = {"8:00", "12:00", "18:00", "Taper"};
      const int lb = (ECRAN_L - 2 * MARGE - 3 * 10) / 4;
      const int yRap = y + VALEUR_H + 2;
      for (int i = 0; i < 3; i++) {
        UI::bouton(boite(MARGE + i * (lb + 10), yRap, lb, RAPIDES_H), HEURES_DEFAUT[i], D_HEURE, i + 1, STYLE_CONTOUR, F_PETITE);
      }
      UI::bouton(boite(MARGE + 3 * (lb + 10), yRap, lb, RAPIDES_H), "Taper", D_HEURE_SAISIR, 0, STYLE_PLEIN, F_PETITE);
    }
    y += BLOC_H;

    // Répétition
    char rep[40];
    if (t->repetition == REP_SEMAINE && t->echeance) {
      snprintf(rep, sizeof(rep), TR("chaque %s"), Horloge::nomJour(Horloge::jourSemaine(t->echeance)));
    } else {
      Stockage::texteRepetition(t->repetition, rep, sizeof(rep));
    }
    const int hRep = 48;
    Dessin::texte(F_TEXTE, MARGE, Dessin::baseCentree(F_TEXTE, y, hRep), "Répéter", NOIR);
    Dessin::texteDroite(F_GRAS, ECRAN_L - MARGE - 28, Dessin::baseCentree(F_GRAS, y, hRep), rep, GRIS_FONCE);
    Dessin::icone(ICONE_CHEVRON_D, ECRAN_L - MARGE - 15, y + (hRep - 21) / 2, 3, GRIS);
    UI::zone(boite(0, y, ECRAN_L, hRep), D_REPETER);
    y += hRep + 10;

    // 7 pastilles pour les jours de la semaine (Lundi à Dimanche)
    const int diam = 46;
    const int ecartJour = (ECRAN_L - 2 * MARGE - 7 * diam) / 6;
    for (int j = 0; j < 7; j++) {
      const int jx = MARGE + j * (diam + ecartJour);
      const bool actif = (t->joursSemaine & (1 << j)) != 0;
      if (actif) {
        Dessin::cerclePlein(jx + diam / 2, y + diam / 2, diam / 2, NOIR);
        Dessin::texteCentre(F_GRAS, jx + diam / 2, y + 32, Horloge::initialeJour(j), BLANC);
      } else {
        Dessin::cercle(jx + diam / 2, y + diam / 2, diam / 2, 2, GRIS);
        Dessin::texteCentre(F_GRAS, jx + diam / 2, y + 32, Horloge::initialeJour(j), NOIR);
      }
      UI::zone(boite(jx - 4, y - 4, diam + 8, diam + 8), D_JOUR_0 + j);
    }
    y += diam + 14;
    UI::separateur(y);
    y += 10;

    // Rappel : seulement avec une date et une heure
    if (t->echeance && t->heure >= 0) {
      char lib[48] = "Rappel à l'heure";
      const uint32_t m = Stockage::momentRappel(t);
      if (t->rappelReporte && m > Horloge::maintenant()) {
        struct tm hm;
        Horloge::localeDe(m, hm);
        snprintf(lib, sizeof(lib), TR("Rappel (reporté à %d:%02d)"), hm.tm_hour, hm.tm_min);
      }
      UI::ligneInterrupteur(y, lib, t->rappel, D_RAPPEL);
    } else {
      const int base = Dessin::baseCentree(F_TEXTE, y, 64);
      Dessin::texte(F_TEXTE, MARGE, base, "Rappel", GRIS);
      Dessin::texteDroite(F_PETITE, ECRAN_L - MARGE, base, "choisis d'abord une heure", GRIS);
      UI::separateur(y + 62);
      UI::zone(boite(0, y, ECRAN_L, 64), D_RAPPEL);
    }
    y += 72;

    // Tâche qu'on vient de créer : pas de dates ni de « Supprimer », mais Annuler / Ajouter.
    const int ySuppr = maxi(y + 50, 856);
    if (m_nouvelle) {
      const int l = (ECRAN_L - 2 * MARGE - 16) / 2;
      UI::bouton(boite(MARGE, ySuppr, l, 68), "Annuler", D_ANNULER, 0, STYLE_DISCRET);
      UI::bouton(boite(MARGE + l + 16, ySuppr, l, 68), "Ajouter", D_AJOUTER, 0, STYLE_PLEIN);
      return;
    }

    // Dates de création / modification bien dégagées au-dessus du bouton supprimer
    char cr[40], md[40], ligne[96];
    dateHeure(t->cree, cr, sizeof(cr));
    dateHeure(t->maj, md, sizeof(md));
    snprintf(ligne, sizeof(ligne), TR("Créée le %s"), cr);
    Dessin::texte(F_PETITE, MARGE, y + 16, ligne, GRIS_FONCE);
    snprintf(ligne, sizeof(ligne), TR("Modifiée le %s"), md);
    Dessin::texte(F_PETITE, MARGE, y + 36, ligne, GRIS_FONCE);

    // Bouton de suppression en bas sans chevauchement
    UI::bouton(boite(MARGE, ySuppr, ECRAN_L - 2 * MARGE, 68), "Supprimer la tâche", D_SUPPRIMER);
  }

  void action(int code, int arg) override {
    const Tache* t = Stockage::tache(Stockage::indexTache(m_id));
    if (!t) return;
    const int auj = Horloge::aujourdhui();
    switch (code) {
      case D_TITRE:
        Systeme::ouvrir(appClavier("Modifier la tâche", t->titre, TACHE_TITRE_MAX, SAISIE_LIGNE, rappelTitre, this));
        return;
      case D_DESCRIPTION:
        Systeme::ouvrir(appClavier("Description", t->description, 120, SAISIE_LIGNE, rappelDescription, this));
        return;
      case D_FAITE:
        basculer(t);
        break;
      case D_ECHEANCE: {
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
      case D_HEURE: {
        static const int H_DEFS[4] = {-1, 8 * 60, 12 * 60, 18 * 60};
        Stockage::heureTache(m_id, H_DEFS[borner(arg, 0, 3)]);
        break;
      }
      case D_HEURE_SAISIR: {
        char initH[12] = "";
        if (t->heure >= 0) snprintf(initH, sizeof(initH), "%d:%02d", t->heure / 60, t->heure % 60);
        Systeme::ouvrir(appClavier("Heure (ex: 7:15 ou 14h30)", initH, 8, SAISIE_LIGNE, rappelHeureSaisie, this));
        return;
      }
      case D_HEURE_MOINS_H:
        if (t->heure >= 0) Stockage::heureTache(m_id, ((t->heure - 60) + 1440) % 1440);
        break;
      case D_HEURE_PLUS_H:
        if (t->heure >= 0) Stockage::heureTache(m_id, (t->heure + 60) % 1440);
        break;
      case D_HEURE_MOINS_M:
        if (t->heure >= 0) Stockage::heureTache(m_id, ((t->heure % 15 ? t->heure - t->heure % 15 : t->heure - 15) + 1440) % 1440);
        break;
      case D_HEURE_PLUS_M:
        if (t->heure >= 0) Stockage::heureTache(m_id, ((t->heure / 15 + 1) * 15) % 1440);
        break;
      case D_HEURE_EFFACER:
        Stockage::heureTache(m_id, -1);
        break;
      case D_REPETER:
        Stockage::repetitionTache(m_id, repetitionSuivante(t->repetition));
        break;
      case D_JOUR_0: case D_JOUR_1: case D_JOUR_2: case D_JOUR_3:
      case D_JOUR_4: case D_JOUR_5: case D_JOUR_6: {
        const int j = code - D_JOUR_0;
        uint8_t js = t->joursSemaine ^ (1 << j);
        Stockage::joursSemaineTache(m_id, js);
        break;
      }
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
      case D_ANNULER:   // création abandonnée : la tâche n'a jamais existé pour l'utilisateur
        m_nouvelle = false;
        Stockage::supprimerTache(m_id);
        Systeme::retour();
        return;
      case D_AJOUTER:
        m_nouvelle = false;
        Systeme::retour();
        Systeme::notifier("Tâche ajoutée");
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
  bool m_nouvelle = false;   // ouverte juste après sa création

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
    snprintf(b, n, TR("%s %d à %s"), d, t.tm_year + 1900, h);
  }

  static void rappelTitre(bool ok, const char* texte, void* ctx) {
    AppTache* moi = static_cast<AppTache*>(ctx);
    if (ok && texte && *texte) Stockage::renommerTache(moi->m_id, texte);
  }

  static void rappelDescription(bool ok, const char* texte, void* ctx) {
    AppTache* moi = static_cast<AppTache*>(ctx);
    if (ok) Stockage::descriptionTache(moi->m_id, texte ? texte : "");
  }

  static void rappelHeureSaisie(bool ok, const char* texte, void* ctx) {
    AppTache* moi = static_cast<AppTache*>(ctx);
    if (!ok || !texte || !*texte) return;
    int h = -1, m = 0;
    if (sscanf(texte, "%d:%d", &h, &m) == 2 ||
        sscanf(texte, "%dh%d", &h, &m) == 2 ||
        sscanf(texte, "%dH%d", &h, &m) == 2 ||
        sscanf(texte, "%d", &h) == 1) {
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        Stockage::heureTache(moi->m_id, h * 60 + m);
        Systeme::redessiner();
      } else {
        Systeme::notifier("Heure invalide (0 à 23h)");
      }
    } else {
      Systeme::notifier("Format: 7:15 ou 14h30");
    }
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

App* appTache(const char* id, bool nouvelle) {
  g_tache.choisir(id, nouvelle);
  return &g_tache;
}

// Pour le calendrier
void dessinerLigneTache(int y, int index, int codeCocher, int codeOuvrir) { ligneTache(y, index, codeCocher, codeOuvrir); }
void ajouterTacheAuClavier(int echeance) {
  Systeme::ouvrir(appClavier("Nouvelle tâche", "", TACHE_TITRE_MAX, SAISIE_LIGNE, rappelNouvelle,
                             (void*)(intptr_t)echeance));
}
