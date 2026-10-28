// Recherche (28/10/2026) : un ou plusieurs mots dans les titres des tâches et le texte des
// notes, sans tenir compte des majuscules ni des accents (« ecole » trouve « École »). Tous les
// mots doivent y être. La dernière recherche est gardée (même pendant le sommeil profond) et
// ses résultats sont recalculés en revenant (tâche cochée, note modifiée entre-temps…).
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "stockage.h"

namespace {

enum Code { C_SAISIR, C_EFFACER, C_OUVRIR };

const int REQUETE_MAX = 63;                               // octets UTF-8
const int MOTS_MAX = 8;
const int CHAMP_Y = CONTENU_Y + 12;                       // 144 : champ de recherche
const int CHAMP_H = 76;
const int RESUME_Y = CHAMP_Y + CHAMP_H + 32;              // 252 : « 3 résultats »
const int LISTE_Y = CHAMP_Y + CHAMP_H + 44;               // 264
const int PAR_PAGE = (PIED_Y - LISTE_Y) / LIGNE_H;        // 6

RTC_DATA_ATTR char r_requete[REQUETE_MAX + 1] = "";      // gardée pendant le sommeil profond

char g_plie[REQUETE_MAX + 1];       // requête repliée, coupée en mots
const char* g_mots[MOTS_MAX];
int g_nbMots = 0;
char* g_tampon = nullptr;           // texte d'une note replié (PSRAM, NOTE_TEXTE_MAX + 1 octets)
int g_ordre[TACHES_MAX];
int16_t g_resTaches[TACHES_MAX];    // index des tâches trouvées (ordre de la liste des tâches)
int16_t g_resNotes[NOTES_MAX];      // index des notes trouvées (la plus récente d'abord)

// Repli d'un caractère pour comparer (avance s) : minuscule sans accent (é -> e, œ -> oe,
// ß -> ss), apostrophes et tirets typographiques -> ASCII, le reste tel quel. Écrit 1 à 4
// octets dans « sortie » et retourne leur nombre (jamais plus que le caractère d'origine).
int plierCar(const char*& s, char* sortie) {
  // 0xC0..0xDF et 0xE0..0xFF : '*' = « ae », '=' = inchangé (× ÷), '?' = ß ou ÿ
  static const char BASE[] = "aaaaaa*ceeeeiiiidnooooo=ouuuuyt?";
  const char* debut = s;
  const uint32_t cp = Dessin::utf8Lire(s);
  if (cp >= 'A' && cp <= 'Z') {
    sortie[0] = (char)(cp + 32);
    return 1;
  }
  if (cp < 0x80) {
    sortie[0] = (char)cp;
    return 1;
  }
  char a = 0, b = 0;
  if (cp >= 0xC0 && cp <= 0xFF) {
    const char c = BASE[cp & 0x1F];
    if (c == '*') { a = 'a'; b = 'e'; }
    else if (c == '?') { if (cp == 0xDF) { a = 's'; b = 's'; } else { a = 'y'; } }
    else if (c != '=') a = c;
  } else if (cp == 0x152 || cp == 0x153) {
    a = 'o';
    b = 'e';
  } else if (cp == 0x178) {
    a = 'y';
  } else if (cp == 0x2018 || cp == 0x2019) {
    a = '\'';
  } else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) {
    a = '-';
  } else if (cp == 0xA0) {
    a = ' ';
  }
  if (a) {
    sortie[0] = a;
    if (!b) return 1;
    sortie[1] = b;
    return 2;
  }
  const int n = (int)(s - debut);   // autre caractère : recopié tel quel
  memcpy(sortie, debut, n);
  return n;
}

// Replie les nbOctets premiers octets de s (au plus) dans sortie. Retourne la longueur.
int plier(const char* s, int nbOctets, char* sortie, int taille) {
  const char* fin = s + nbOctets;
  int o = 0;
  char c[4];
  while (s < fin && *s) {
    const int n = plierCar(s, c);
    if (o + n >= taille) break;
    memcpy(sortie + o, c, n);
    o += n;
  }
  sortie[o] = 0;
  return o;
}

void preparerMots() {
  plier(r_requete, (int)strlen(r_requete), g_plie, sizeof(g_plie));
  g_nbMots = 0;
  char* p = g_plie;
  while (*p && g_nbMots < MOTS_MAX) {
    if (*p == ' ') {
      p++;
      continue;
    }
    g_mots[g_nbMots++] = p;
    while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
  }
}

bool contientTout(const char* textePlie) {
  for (int i = 0; i < g_nbMots; i++) {
    if (!strstr(textePlie, g_mots[i])) return false;
  }
  return true;
}

// Extrait d'une note : la première ligne (hors titre) qui contient un des mots, à partir du
// mot trouvé (« …suite de la ligne »). Vide si les mots ne sont que dans le titre.
void extraitNote(const Note* no, char* sortie, size_t taille) {
  sortie[0] = 0;
  if (!no || !no->texte || !g_tampon || g_nbMots == 0 || taille < 8) return;
  const char* p = no->texte;
  while (*p == '\n' || *p == ' ') p++;
  p = strchr(p, '\n');   // saute la ligne de titre
  while (p && *p) {
    if (*p == '\n') p++;
    const char* fin = strchr(p, '\n');
    const int lg = fin ? (int)(fin - p) : (int)strlen(p);
    plier(p, lg, g_tampon, NOTE_TEXTE_MAX + 1);
    int pos = -1;
    for (int i = 0; i < g_nbMots; i++) {
      const char* q = strstr(g_tampon, g_mots[i]);
      if (q && (pos < 0 || (int)(q - g_tampon) < pos)) pos = (int)(q - g_tampon);
    }
    if (pos >= 0) {
      // Octet d'origine du mot trouvé (le repli change les longueurs), puis début de ce mot.
      const char* q = p;
      int f = 0;
      char c[4];
      while (q < p + lg && f < pos) f += plierCar(q, c);
      const char* debut = q;
      int recul = 0;
      while (debut > p && debut[-1] != ' ' && recul < 24) {
        debut--;
        recul++;
      }
      while (debut < q && (((uint8_t)*debut) & 0xC0) == 0x80) debut++;   // pas au milieu d'un caractère
      size_t o = 0;
      if (debut > p) {
        memcpy(sortie, "\xE2\x80\xA6", 3);   // …
        o = 3;
      }
      int l = mini((int)(p + lg - debut), (int)(taille - 1 - o));
      while (l > 0 && (((uint8_t)debut[l]) & 0xC0) == 0x80) l--;           // ne coupe pas un caractère
      memcpy(sortie + o, debut, l);
      sortie[o + l] = 0;
      return;
    }
    p = fin;
  }
}

class AppRecherche : public App {
 public:
  IdApp id() const override { return APP_RECHERCHE; }
  // Sans recherche en cours, rien à retrouver après la veille : on repart de l'accueil.
  bool reprendreApresVeille() const override { return r_requete[0] != 0; }

  void entrer() override {
    chercher();   // les données ont pu changer depuis la dernière fois
    if (!r_requete[0] && !m_clavierPropose) {   // ouverture sans recherche : directement au clavier
      m_clavierPropose = true;
      saisir();
    }
  }

  void dessiner() override {
    UI::barreTitre("Recherche", true);
    dessinerChamp();
    const int n = m_nbT + m_nbN;
    m_pages.regler(n, PAR_PAGE);
    if (!r_requete[0]) {
      UI::messageVide(LISTE_Y + 60, "Que cherches-tu ?", "Dans les tâches et les notes.");
      return;
    }
    char resume[40];
    if (n == 0) snprintf(resume, sizeof(resume), "Aucun résultat");
    else snprintf(resume, sizeof(resume), "%d résultat%s", n, n > 1 ? "s" : "");
    Dessin::texte(F_PETITE, MARGE, RESUME_Y, resume, GRIS_FONCE);
    if (n == 0) {
      UI::messageVide(LISTE_Y + 60, "Rien trouvé", "Essaie un autre mot.");
      return;
    }
    for (int i = 0; i < PAR_PAGE; i++) {
      const int k = m_pages.page * PAR_PAGE + i;
      if (k >= n) break;
      ligneResultat(LISTE_Y + i * LIGNE_H, k);
    }
    UI::pagination(m_pages);
  }

  void action(int code, int arg) override {
    switch (code) {
      case C_SAISIR:
        saisir();
        break;
      case C_EFFACER:   // nouvelle recherche
        r_requete[0] = 0;
        m_pages.page = 0;
        chercher();
        saisir();
        break;
      case C_OUVRIR:
        ouvrirResultat(arg);
        break;
      case CODE_PAGE_PREC: if (m_pages.precedente()) Systeme::redessiner(); break;
      case CODE_PAGE_SUIV: if (m_pages.suivante()) Systeme::redessiner(); break;
      default: break;
    }
  }

  bool bouton(Bouton b) override {
    if ((b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente()) Systeme::redessiner();
    return true;
  }

  bool retour() override {
    m_clavierPropose = false;   // la prochaine ouverture sans recherche repartira du clavier
    Systeme::retour();
    return true;
  }

 private:
  Pages m_pages = {0, 1};
  int m_nbT = 0;                 // tâches trouvées (affichées d'abord)
  int m_nbN = 0;                 // notes trouvées
  bool m_clavierPropose = false;

  void saisir() {
    Systeme::ouvrir(appClavier("Rechercher", r_requete, REQUETE_MAX, SAISIE_LIGNE, quandSaisi, this));
  }

  static void quandSaisi(bool ok, const char* texte, void* ctx) {
    AppRecherche* moi = static_cast<AppRecherche*>(ctx);
    if (!ok || !texte) return;
    // Sans espaces autour ; le clavier limite déjà à REQUETE_MAX octets.
    while (*texte == ' ') texte++;
    int l = (int)strlen(texte);
    while (l > 0 && texte[l - 1] == ' ') l--;
    if (l > REQUETE_MAX) {
      l = REQUETE_MAX;
      while (l > 0 && (((uint8_t)texte[l]) & 0xC0) == 0x80) l--;
    }
    memcpy(r_requete, texte, l);
    r_requete[l] = 0;
    moi->m_pages.page = 0;
    moi->chercher();
    Systeme::redessiner();
  }

  void chercher() {
    m_nbT = 0;
    m_nbN = 0;
    preparerMots();
    if (g_nbMots == 0) return;
    if (!g_tampon) g_tampon = (char*)ps_malloc(NOTE_TEXTE_MAX + 1);
    char buf[TACHE_TITRE_MAX + 1];
    const int n = Stockage::listeTaches(TACHES_TOUTES, g_ordre, TACHES_MAX);   // à faire d'abord
    for (int k = 0; k < n; k++) {
      const Tache* t = Stockage::tache(g_ordre[k]);
      if (!t) continue;
      plier(t->titre, (int)strlen(t->titre), buf, sizeof(buf));
      if (contientTout(buf)) g_resTaches[m_nbT++] = (int16_t)g_ordre[k];
    }
    for (int i = 0; i < Stockage::nbNotes() && m_nbN < NOTES_MAX; i++) {
      const Note* no = Stockage::note(i);
      if (!no || !no->texte) continue;
      bool trouve;
      if (g_tampon) {
        plier(no->texte, (int)strlen(no->texte), g_tampon, NOTE_TEXTE_MAX + 1);
        trouve = contientTout(g_tampon);
      } else {   // PSRAM pleine : le titre seulement
        char titre[96];
        Stockage::titreNote(no, titre, sizeof(titre));
        plier(titre, (int)strlen(titre), buf, sizeof(buf));
        trouve = contientTout(buf);
      }
      if (trouve) g_resNotes[m_nbN++] = (int16_t)i;
    }
  }

  void dessinerChamp() {
    const Boite c = boite(MARGE, CHAMP_Y, ECRAN_L - 2 * MARGE, CHAMP_H);
    Dessin::chanfrein(c.x, c.y, c.l, c.h, CHANFREIN, TRAIT, NOIR);
    Dessin::icone(ICONE_RECHERCHE, c.x + 18, c.y + (c.h - 36) / 2, 3, NOIR);   // loupe 36 x 36
    const int xt = c.x + 70;
    if (r_requete[0]) {
      const int le = 72;   // croix « effacer » à droite
      const GFXfont* p = policeTexte();
      Dessin::texteTronque(p, xt, Dessin::baseCentree(p, c.y, c.h), c.x + c.l - le - xt, r_requete, NOIR);
      Dessin::pictoCroix(c.x + c.l - le / 2, c.y + c.h / 2, 22, 4, GRIS_FONCE);
      UI::zone(boite(c.x, c.y, c.l - le, c.h), C_SAISIR);
      UI::zone(boite(c.x + c.l - le, c.y, le, c.h), C_EFFACER);
    } else {
      Dessin::texte(F_TEXTE, xt, Dessin::baseCentree(F_TEXTE, c.y, c.h), "Chercher…", GRIS);
      UI::zone(c, C_SAISIR);
    }
  }

  // « Tâche · demain · 9:00 », « Tâche faite »…
  static void descriptionTache(const Tache* t, char* b, size_t n) {
    if (t->faite) {
      snprintf(b, n, "Tâche faite");
      return;
    }
    snprintf(b, n, "Tâche");
    size_t o = strlen(b);
    if (t->echeance) {
      char d[24];
      Horloge::texteDateRelative(d, sizeof(d), t->echeance);
      snprintf(b + o, n - o, " · %s%s", t->echeance < Horloge::aujourdhui() ? "en retard, " : "", d);
      o = strlen(b);
    }
    if (t->heure >= 0 && o < n) snprintf(b + o, n - o, " · %d:%02d", t->heure / 60, t->heure % 60);
  }

  void ligneResultat(int y, int k) {
    const int x = MARGE + 56;
    const int lmax = ECRAN_L - MARGE - x;
    const GFXfont* p = policeTexte();
    char sous[96] = "";
    if (k < m_nbT) {
      const Tache* t = Stockage::tache(g_resTaches[k]);
      if (!t) return;
      Dessin::icone(ICONE_TACHES, MARGE, y + 22, 3, t->faite ? GRIS : GRIS_FONCE);
      const int l = Dessin::texteTronque(p, x, y + 44, lmax, t->titre, t->faite ? GRIS : NOIR);
      if (t->faite) Dessin::rectPlein(x, y + 44 - Dessin::hautCapitale(p) / 2, l, 2, GRIS);   // barré
      descriptionTache(t, sous, sizeof(sous));
    } else {
      const Note* no = Stockage::note(g_resNotes[k - m_nbT]);
      if (!no) return;
      Dessin::icone(ICONE_NOTES, MARGE, y + 22, 3, GRIS_FONCE);
      char titre[96];
      Stockage::titreNote(no, titre, sizeof(titre));
      Dessin::texteTronque(p, x, y + 44, lmax, titre[0] ? titre : "(sans titre)", NOIR);
      extraitNote(no, sous, sizeof(sous));
      if (!sous[0]) snprintf(sous, sizeof(sous), "Note");
    }
    Dessin::texteTronque(F_PETITE, x, y + 76, lmax, sous, GRIS_FONCE);
    UI::separateur(y + LIGNE_H - 2, x, ECRAN_L - MARGE);
    UI::zone(boite(0, y, ECRAN_L, LIGNE_H), C_OUVRIR, k);
  }

  void ouvrirResultat(int k) {
    if (k < 0) return;
    if (k < m_nbT) {
      const Tache* t = Stockage::tache(g_resTaches[k]);
      if (t) Systeme::ouvrir(appTache(t->id));
    } else if (k - m_nbT < m_nbN) {
      const Note* no = Stockage::note(g_resNotes[k - m_nbT]);
      if (no) Systeme::ouvrir(appNote(no->id));
    }
  }
};

AppRecherche g_recherche;

}  // namespace

App* appRecherche() { return &g_recherche; }
