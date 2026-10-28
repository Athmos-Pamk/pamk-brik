// Clavier AZERTY à l'écran : lettres, accents (é è ê ë à â ç ù û î ï ô œ...), chiffres, symboles.
// Taper à la fin du texte ne fait que foncer des pixels : l'écran se met à jour sans flash.
// Curseur : toucher le texte, ou volume + / volume − (un caractère à droite / à gauche).
#include "app.h"
#include "systeme.h"
#include "ui.h"

namespace {

enum ActionTouche : uint8_t { T_CAR, T_MAJ, T_EFF, T_ESPACE, T_ENTREE, T_LETTRES, T_ACCENTS, T_CHIFFRES, T_SYMBOLES };

struct DefTouche {
  const char* car;
  uint8_t largeur;   // en demi-touches (une rangée = 20)
  uint8_t action;
};

struct Rangee {
  const DefTouche* touches;
  uint8_t nb;
};

#define TC(c) {c, 2, T_CAR}
#define MAJ {"", 3, T_MAJ}
#define EFF {"", 3, T_EFF}
#define ESPACE {"espace", 6, T_ESPACE}
#define ENTREE {"", 2, T_ENTREE}

const DefTouche L1[] = {TC("a"), TC("z"), TC("e"), TC("r"), TC("t"), TC("y"), TC("u"), TC("i"), TC("o"), TC("p")};
const DefTouche L2[] = {TC("q"), TC("s"), TC("d"), TC("f"), TC("g"), TC("h"), TC("j"), TC("k"), TC("l"), TC("m")};
const DefTouche L3[] = {MAJ, TC("w"), TC("x"), TC("c"), TC("v"), TC("b"), TC("n"), TC("'"), EFF};
const DefTouche L4[] = {{"?123", 3, T_CHIFFRES}, TC(","), TC("é"), ESPACE, TC("."), {"àç", 3, T_ACCENTS}, ENTREE};

const DefTouche A1[] = {TC("é"), TC("è"), TC("ê"), TC("ë"), TC("à"), TC("â"), TC("ä"), TC("ù"), TC("û"), TC("ü")};
const DefTouche A2[] = {TC("ç"), TC("î"), TC("ï"), TC("ô"), TC("ö"), TC("œ"), TC("æ"), TC("ÿ"), TC("ñ"), TC("ß")};
const DefTouche A3[] = {MAJ, TC("€"), TC("°"), TC("…"), TC("–"), TC("’"), TC("«"), TC("»"), EFF};
const DefTouche A4[] = {{"?123", 3, T_CHIFFRES}, TC(","), TC("-"), ESPACE, TC("."), {"abc", 3, T_LETTRES}, ENTREE};

const DefTouche C1[] = {TC("1"), TC("2"), TC("3"), TC("4"), TC("5"), TC("6"), TC("7"), TC("8"), TC("9"), TC("0")};
const DefTouche C2[] = {TC("@"), TC("#"), TC("€"), TC("%"), TC("&"), TC("-"), TC("+"), TC("("), TC(")"), TC("/")};
const DefTouche C3[] = {{"=\\<", 3, T_SYMBOLES}, TC("*"), TC("\""), TC("'"), TC(":"), TC(";"), TC("!"), TC("?"), EFF};
const DefTouche C4[] = {{"abc", 3, T_LETTRES}, TC(","), TC("_"), ESPACE, TC("."), {"àç", 3, T_ACCENTS}, ENTREE};

const DefTouche S1[] = {TC("["), TC("]"), TC("{"), TC("}"), TC("<"), TC(">"), TC("="), TC("_"), TC("\\"), TC("|")};
const DefTouche S2[] = {TC("~"), TC("^"), TC("$"), TC("£"), TC("°"), TC("§"), TC("²"), TC("µ"), TC("×"), TC("÷")};
const DefTouche S3[] = {{"?123", 3, T_CHIFFRES}, TC("`"), TC("«"), TC("»"), TC("¿"), TC("¡"), TC("•"), TC("·"), EFF};
const DefTouche S4[] = {{"abc", 3, T_LETTRES}, TC(","), TC("…"), ESPACE, TC("."), {"àç", 3, T_ACCENTS}, ENTREE};

#define RG(t) {t, (uint8_t)(sizeof(t) / sizeof(t[0]))}
const Rangee COUCHES[4][4] = {
  {RG(L1), RG(L2), RG(L3), RG(L4)},
  {RG(A1), RG(A2), RG(A3), RG(A4)},
  {RG(C1), RG(C2), RG(C3), RG(C4)},
  {RG(S1), RG(S2), RG(S3), RG(S4)},
};
#undef TC
#undef MAJ
#undef EFF
#undef ESPACE
#undef ENTREE
#undef RG
enum Couche : uint8_t { COUCHE_LETTRES, COUCHE_ACCENTS, COUCHE_CHIFFRES, COUCHE_SYMBOLES };

enum Code { C_OK = 1, C_VOIR = 2, C_TOUCHE = 100 };

const Boite ZONE_TEXTE = {MARGE, CONTENU_Y + 12, ECRAN_L - 2 * MARGE, CLAVIER_Y - CONTENU_Y - 24};
const int PADDING = 16;
const int INTERLIGNE = 38;
const int LARGEUR_TEXTE = ECRAN_L - 2 * MARGE - 2 * PADDING;   // 460
const int LIGNES_VISIBLES = (CLAVIER_Y - CONTENU_Y - 24 - 2 * PADDING) / INTERLIGNE;   // 10
const int LIGNES_MAX = NOTE_TEXTE_MAX + 1;

// Majuscule d'une lettre (ASCII, Latin-1, œ, ÿ) ; sinon inchangé.
uint32_t majuscule(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 32;
  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
  if (cp == 0xFF) return 0x178;
  if (cp == 0x153) return 0x152;
  return cp;
}

class AppClavier : public App {
 public:
  IdApp id() const override { return APP_CLAVIER; }
  bool reprendreApresVeille() const override { return false; }
  uint32_t delaiVeilleMs(uint32_t defaut) const override {
    return m_modifie ? (defaut > 600000UL ? defaut : 600000UL) : defaut;   // texte en cours : 10 min
  }

  bool configurer(const char* titre, const char* initial, int maxOctets, ModeSaisie mode,
                  RappelSaisie rappel, void* contexte) {
    if (!m_texte) {
      m_texte = (char*)ps_malloc(NOTE_TEXTE_MAX + 1);
      m_affiche = (char*)ps_malloc(NOTE_TEXTE_MAX * 3 + 1);
      m_lignes = (LigneTexte*)ps_malloc(sizeof(LigneTexte) * LIGNES_MAX);
      if (!m_texte || !m_affiche || !m_lignes) return false;
    }
    strncpy(m_titre, titre ? titre : "", sizeof(m_titre) - 1);
    m_titre[sizeof(m_titre) - 1] = 0;
    m_max = borner(maxOctets, 1, NOTE_TEXTE_MAX);
    m_mode = mode;
    m_rappel = rappel;
    m_contexte = contexte;
    const char* src = initial ? initial : "";
    int l = (int)strlen(src);
    if (l > m_max) {
      l = m_max;
      while (l > 0 && (((uint8_t)src[l]) & 0xC0) == 0x80) l--;
    }
    memcpy(m_texte, src, l);
    m_texte[l] = 0;
    m_long = l;
    m_curseur = l;
    m_couche = COUCHE_LETTRES;
    m_maj = (mode != SAISIE_MOT_DE_PASSE && l == 0) ? 1 : 0;   // majuscule en début de texte
    m_voir = false;
    m_modifie = false;
    return true;
  }

  void dessiner() override {
    UI::barreTitre(m_titre, true, "OK", C_OK, F_GRAS);   // titres longs (« Mot de passe de … »)
    dessinerTexte();
    dessinerClavier();
  }

  void action(int code, int arg) override {
    (void)arg;
    if (code == C_OK) { valider(); return; }
    if (code == C_VOIR) { m_voir = !m_voir; Systeme::redessiner(); return; }
    if (code >= C_TOUCHE) {
      const int r = (code - C_TOUCHE) / 16, k = (code - C_TOUCHE) % 16;
      if (r < 4 && k < COUCHES[m_couche][r].nb) appuyer(COUCHES[m_couche][r].touches[k]);
    }
  }

  void appuiLong(int code, int arg) override {
    // Appui long sur « effacer » : efface le mot précédent.
    if (code >= C_TOUCHE) {
      const int r = (code - C_TOUCHE) / 16, k = (code - C_TOUCHE) % 16;
      if (r < 4 && k < COUCHES[m_couche][r].nb && COUCHES[m_couche][r].touches[k].action == T_EFF) {
        while (m_curseur > 0 && m_texte[m_curseur - 1] == ' ') effacer();
        while (m_curseur > 0 && m_texte[m_curseur - 1] != ' ' && m_texte[m_curseur - 1] != '\n') effacer();
        Systeme::redessiner();
        return;
      }
    }
    action(code, arg);
  }

  void toucherLibre(int x, int y) override {
    if (m_mode == SAISIE_MOT_DE_PASSE || !dansBoite(ZONE_TEXTE, x, y)) return;
    const int n = decouper();
    const int premiere = premiereLigne(n);
    int li = premiere + (y - (ZONE_TEXTE.y + PADDING)) / INTERLIGNE;
    li = borner(li, 0, n - 1);
    const LigneTexte& l = m_lignes[li];
    const int cible = x - (ZONE_TEXTE.x + PADDING);
    int pos = l.debut, cumul = 0;
    while (pos < l.fin) {
      const char* q = m_texte + pos;
      Dessin::utf8Lire(q);
      const int suivant = (int)(q - m_texte);
      const int a = Dessin::largeurN(F_TEXTE, m_texte + pos, suivant - pos);
      if (cumul + a / 2 > cible) break;
      cumul += a;
      pos = suivant;
    }
    m_curseur = pos;
    Systeme::redessiner();
  }

  bool bouton(Bouton b) override {
    if (m_mode == SAISIE_MOT_DE_PASSE) return true;
    if (b == BOUTON_PLUS && m_curseur < m_long) {
      const char* q = m_texte + m_curseur;
      Dessin::utf8Lire(q);
      m_curseur = (int)(q - m_texte);
    } else if (b == BOUTON_MOINS && m_curseur > 0) {
      m_curseur = Dessin::utf8Precedent(m_texte, m_curseur);
    }
    Systeme::redessiner();
    return true;
  }

  bool retour() override {
    Systeme::retour();
    if (m_rappel) m_rappel(false, m_texte, m_contexte);
    return true;
  }

 private:
  char m_titre[64] = "";
  char* m_texte = nullptr;
  char* m_affiche = nullptr;
  LigneTexte* m_lignes = nullptr;
  int m_long = 0;
  int m_curseur = 0;
  int m_max = 0;
  ModeSaisie m_mode = SAISIE_LIGNE;
  RappelSaisie m_rappel = nullptr;
  void* m_contexte = nullptr;
  uint8_t m_couche = COUCHE_LETTRES;
  uint8_t m_maj = 0;          // 0 non, 1 une lettre, 2 verrouillé
  bool m_voir = false;
  bool m_modifie = false;

  void valider() {
    Systeme::retour();
    if (m_rappel) m_rappel(true, m_texte, m_contexte);
  }

  const char* texteAffiche() {
    if (m_mode != SAISIE_MOT_DE_PASSE || m_voir) return m_texte;
    // Mot de passe masqué : un « • » par caractère
    int o = 0;
    const char* p = m_texte;
    while (*p) {
      Dessin::utf8Lire(p);
      memcpy(m_affiche + o, "\xE2\x80\xA2", 3);
      o += 3;
    }
    m_affiche[o] = 0;
    return m_affiche;
  }

  int decouper() {
    const int n = Dessin::couperLignes(F_TEXTE, texteAffiche(), LARGEUR_TEXTE, m_lignes, LIGNES_MAX);
    return n < 1 ? 1 : n;
  }

  int ligneDuCurseur(int n) {
    for (int i = 0; i < n; i++) {
      if (m_curseur >= m_lignes[i].debut && m_curseur <= m_lignes[i].fin) {
        // Curseur juste après un retour à la ligne : début de la ligne suivante
        if (m_curseur == m_lignes[i].fin && i + 1 < n && m_lignes[i + 1].debut == m_curseur) continue;
        return i;
      }
    }
    return n - 1;
  }

  int premiereLigne(int n) {
    const int lc = (m_mode == SAISIE_MOT_DE_PASSE) ? n - 1 : ligneDuCurseur(n);
    return maxi(0, lc - LIGNES_VISIBLES + 1);
  }

  void dessinerTexte() {
    const Boite& z = ZONE_TEXTE;
    Dessin::chanfrein(z.x, z.y, z.l, z.h, CHANFREIN, 2, GRIS);
    const int x0 = z.x + PADDING;
    const int base0 = z.y + PADDING + 30;
    if (m_long == 0) {
      Dessin::texte(F_TEXTE, x0, base0, m_mode == SAISIE_MOT_DE_PASSE ? "Mot de passe" : "Écris ici…", GRIS);
    } else {
      const char* aff = texteAffiche();
      const int n = decouper();
      const int premiere = premiereLigne(n);
      if (premiere > 0) Dessin::texteDroite(F_PETITE, z.x + z.l - PADDING, z.y + 26, "…", GRIS);
      for (int i = 0; i < LIGNES_VISIBLES && premiere + i < n; i++) {
        const LigneTexte& l = m_lignes[premiere + i];
        Dessin::texteN(F_TEXTE, x0, base0 + i * INTERLIGNE, aff + l.debut, l.fin - l.debut, NOIR);
      }
      // Curseur seulement s'il n'est pas à la fin (taper à la fin reste sans flash).
      if (m_mode != SAISIE_MOT_DE_PASSE && m_curseur < m_long) {
        const int lc = ligneDuCurseur(n);
        if (lc >= premiere && lc < premiere + LIGNES_VISIBLES) {
          const LigneTexte& l = m_lignes[lc];
          const int x = x0 + Dessin::largeurN(F_TEXTE, m_texte + l.debut, m_curseur - l.debut);
          Dessin::rectPlein(x - 1, base0 + (lc - premiere) * INTERLIGNE - 28, 3, 36, NOIR);
        }
      }
    }
    if (m_mode == SAISIE_MOT_DE_PASSE) {
      UI::bouton(boite(z.x + z.l - 150, z.y + z.h - 68, 134, 52), m_voir ? "Masquer" : "Afficher", C_VOIR, 0,
                 STYLE_DISCRET, F_PETITE);
    }
  }

  void dessinerClavier() {
    for (int r = 0; r < 4; r++) {
      const Rangee& rg = COUCHES[m_couche][r];
      const int y = CLAVIER_Y + r * (CLAVIER_TOUCHE_H + CLAVIER_ECART);
      int cumul = 0;
      for (int k = 0; k < rg.nb; k++) {
        const DefTouche& t = rg.touches[k];
        const int x0 = 6 + cumul * (ECRAN_L - 6) / 20;
        const int x1 = 6 + (cumul + t.largeur) * (ECRAN_L - 6) / 20 - CLAVIER_ECART;
        cumul += t.largeur;
        const Boite b = boite(x0, y, x1 - x0, CLAVIER_TOUCHE_H);
        const int cx = b.x + b.l / 2, cy = b.y + b.h / 2;
        if (t.action == T_CAR) {
          Dessin::chanfrein(b.x, b.y, b.l, b.h, 8, 2, GRIS_FONCE);
          Dessin::texteCentre(F_GRAS, cx, Dessin::baseCentree(F_GRAS, b.y, b.h), t.car, NOIR);
        } else {
          Dessin::chanfreinPlein(b.x, b.y, b.l, b.h, 8, GRIS_CLAIR);
          switch (t.action) {
            case T_MAJ:
              Dessin::icone(m_maj == 2 ? ICONE_MAJ_PLEIN : ICONE_MAJ, cx - 18, cy - 16, 4, NOIR);
              if (m_maj == 1) Dessin::rectPlein(cx - 18, cy + 20, 36, 4, NOIR);
              break;
            case T_EFF:
              Dessin::icone(ICONE_EFFACER, cx - 20, cy - 10, 4, NOIR);
              break;
            case T_ENTREE:
              if (m_mode == SAISIE_TEXTE) Dessin::icone(ICONE_ENTREE, cx - 20, cy - 14, 4, NOIR);
              else Dessin::texteCentre(F_GRAS, cx, Dessin::baseCentree(F_GRAS, b.y, b.h), "OK", NOIR);
              break;
            case T_ESPACE:
              Dessin::texteCentre(F_PETITE, cx, Dessin::baseCentree(F_PETITE, b.y, b.h), t.car, GRIS_FONCE);
              break;
            default:
              Dessin::texteCentre(F_GRAS, cx, Dessin::baseCentree(F_GRAS, b.y, b.h), t.car, NOIR);
              break;
          }
        }
        UI::zone(b, C_TOUCHE + r * 16 + k);
      }
    }
  }

  void inserer(const char* s) {
    const int l = (int)strlen(s);
    if (m_long + l > m_max) {
      Systeme::notifier("Texte trop long");
      return;
    }
    memmove(m_texte + m_curseur + l, m_texte + m_curseur, m_long - m_curseur + 1);
    memcpy(m_texte + m_curseur, s, l);
    m_long += l;
    m_curseur += l;
    m_modifie = true;
  }

  void effacer() {
    if (m_curseur == 0) return;
    const int p = Dessin::utf8Precedent(m_texte, m_curseur);
    memmove(m_texte + p, m_texte + m_curseur, m_long - m_curseur + 1);
    m_long -= m_curseur - p;
    m_curseur = p;
    m_modifie = true;
  }

  void appuyer(const DefTouche& t) {
    switch (t.action) {
      case T_CAR: {
        const char* q = t.car;
        uint32_t cp = Dessin::utf8Lire(q);
        if (m_maj) cp = majuscule(cp);
        char buf[5];
        const int n = Dessin::utf8Encoder(cp, buf);
        buf[n] = 0;
        inserer(buf);
        if (*q) inserer(q);   // touches de plusieurs caractères (aucune pour l'instant)
        if (m_maj == 1) m_maj = 0;
        break;
      }
      case T_MAJ: m_maj = (uint8_t)((m_maj + 1) % 3); break;
      case T_EFF: effacer(); break;
      case T_ESPACE: inserer(" "); break;
      case T_ENTREE:
        if (m_mode == SAISIE_TEXTE) inserer("\n");
        else { valider(); return; }
        break;
      case T_LETTRES: m_couche = COUCHE_LETTRES; break;
      case T_ACCENTS: m_couche = COUCHE_ACCENTS; break;
      case T_CHIFFRES: m_couche = COUCHE_CHIFFRES; break;
      case T_SYMBOLES: m_couche = COUCHE_SYMBOLES; break;
      default: break;
    }
    Systeme::redessiner();
  }
};

AppClavier g_clavier;

}  // namespace

App* appClavier(const char* titre, const char* initial, int maxOctets, ModeSaisie mode,
                RappelSaisie rappel, void* contexte) {
  if (!g_clavier.configurer(titre, initial, maxOctets, mode, rappel, contexte)) {
    Systeme::notifier("Mémoire insuffisante pour le clavier");
    return nullptr;
  }
  return &g_clavier;
}
