#include "dessin.h"
#include "ecran.h"
#include <math.h>
#include <string.h>

namespace Dessin {

static int s_cx0 = 0, s_cy0 = 0, s_cx1 = ECRAN_L, s_cy1 = ECRAN_H;   // découpe [x0,x1) x [y0,y1)

void decoupe(const Boite& b) {
  s_cx0 = maxi(0, b.x);
  s_cy0 = maxi(0, b.y);
  s_cx1 = mini(ECRAN_L, b.x + b.l);
  s_cy1 = mini(ECRAN_H, b.y + b.h);
}

void sansDecoupe() {
  s_cx0 = 0; s_cy0 = 0; s_cx1 = ECRAN_L; s_cy1 = ECRAN_H;
}

void pixel(int x, int y, uint8_t c) {
  if (x < s_cx0 || x >= s_cx1 || y < s_cy0 || y >= s_cy1) return;
  Ecran::toile()[y * ECRAN_L + x] = c & 0x0F;
}

void rectPlein(int x, int y, int l, int h, uint8_t c) {
  int x0 = maxi(x, s_cx0), x1 = mini(x + l, s_cx1);
  int y0 = maxi(y, s_cy0), y1 = mini(y + h, s_cy1);
  if (x1 <= x0 || y1 <= y0) return;
  uint8_t* t = Ecran::toile();
  for (int yy = y0; yy < y1; yy++) memset(t + yy * ECRAN_L + x0, c & 0x0F, x1 - x0);
}

void rect(int x, int y, int l, int h, int ep, uint8_t c) {
  rectPlein(x, y, l, ep, c);
  rectPlein(x, y + h - ep, l, ep, c);
  rectPlein(x, y + ep, ep, h - 2 * ep, c);
  rectPlein(x + l - ep, y + ep, ep, h - 2 * ep, c);
}

void ligneH(int x, int y, int l, uint8_t c, int ep) { rectPlein(x, y, l, ep, c); }
void ligneV(int x, int y, int h, uint8_t c, int ep) { rectPlein(x, y, ep, h, c); }

void ligne(int x0, int y0, int x1, int y1, uint8_t c, int ep) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  const int d = ep / 2;
  while (true) {
    if (ep <= 1) pixel(x0, y0, c);
    else rectPlein(x0 - d, y0 - d, ep, ep, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void pointille(int x, int y, int l, uint8_t c, int pas) {
  for (int i = 0; i < l; i += pas * 2) rectPlein(x + i, y, mini(pas, l - i), 2, c);
}

// Retrait horizontal d'une ligne i d'un rectangle à coins coupés (chanfrein k, hauteur h).
static inline int retrait(int i, int k, int h) {
  if (i < k) return k - i;
  if (i >= h - k) return i - (h - k - 1);
  return 0;
}

void chanfreinPlein(int x, int y, int l, int h, int k, uint8_t c) {
  k = mini(k, mini(l, h) / 2);
  for (int i = 0; i < h; i++) {
    const int r = retrait(i, k, h);
    rectPlein(x + r, y + i, l - 2 * r, 1, c);
  }
}

void chanfrein(int x, int y, int l, int h, int k, int ep, uint8_t c) {
  k = mini(k, mini(l, h) / 2);
  const int hi = h - 2 * ep;              // hauteur intérieure
  const int ki = mini(k, maxi(0, hi / 2));
  for (int i = 0; i < h; i++) {
    const int a = retrait(i, k, h);
    if (i < ep || i >= h - ep) {
      rectPlein(x + a, y + i, l - 2 * a, 1, c);
      continue;
    }
    int b = ep + retrait(i - ep, ki, hi);
    if (b > l / 2) b = l / 2;
    rectPlein(x + a, y + i, b - a, 1, c);
    rectPlein(x + l - b, y + i, b - a, 1, c);
  }
}

void cerclePlein(int cx, int cy, int r, uint8_t c) {
  for (int dy = -r; dy <= r; dy++) {
    const int dx = (int)sqrtf((float)(r * r - dy * dy) + 0.25f);
    rectPlein(cx - dx, cy + dy, 2 * dx + 1, 1, c);
  }
}

void cercle(int cx, int cy, int r, int ep, uint8_t c) {
  const int ri = r - ep;
  for (int dy = -r; dy <= r; dy++) {
    const int dxo = (int)sqrtf((float)(r * r - dy * dy) + 0.25f);
    if (abs(dy) > ri || ri <= 0) {
      rectPlein(cx - dxo, cy + dy, 2 * dxo + 1, 1, c);
    } else {
      const int dxi = (int)sqrtf((float)(ri * ri - dy * dy) + 0.25f);
      rectPlein(cx - dxo, cy + dy, dxo - dxi, 1, c);
      rectPlein(cx + dxi + 1, cy + dy, dxo - dxi, 1, c);
    }
  }
}

void barreProgression(const Boite& b, int pourMille, uint8_t fond, uint8_t plein) {
  pourMille = borner(pourMille, 0, 1000);
  rectPlein(b.x, b.y, b.l, b.h, fond);
  rectPlein(b.x, b.y, (int)((long)b.l * pourMille / 1000), b.h, plein);
}

// ---------------------------------------------------------------------------
//  Texte
// ---------------------------------------------------------------------------
uint32_t utf8Lire(const char*& s) {
  const uint8_t c = (uint8_t)*s;
  if (!c) return 0;
  if (c < 0x80) { s++; return c; }
  int n;
  uint32_t cp;
  if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
  else { s++; return 0xFFFD; }
  s++;
  for (int i = 0; i < n; i++) {
    const uint8_t d = (uint8_t)*s;
    if ((d & 0xC0) != 0x80) return 0xFFFD;   // séquence tronquée : on ne consomme pas
    cp = (cp << 6) | (d & 0x3F);
    s++;
  }
  return cp;
}

int utf8Precedent(const char* s, int pos) {
  if (pos <= 0) return 0;
  pos--;
  while (pos > 0 && (((uint8_t)s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

int utf8Encoder(uint32_t cp, char* o) {
  if (cp < 0x80) { o[0] = (char)cp; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    o[0] = (char)(0xE0 | (cp >> 12));
    o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  o[0] = (char)(0xF0 | (cp >> 18));
  o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  o[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static const GFXglyph* glyphe(const GFXfont* p, uint32_t cp) {
  for (uint32_t i = 0; i < (uint32_t)p->interval_count; i++) {
    const UnicodeInterval& iv = p->intervals[i];
    if (cp >= iv.first && cp <= iv.last) return &p->glyph[iv.offset + (cp - iv.first)];
    if (cp < iv.first) break;
  }
  return nullptr;
}

// Glyphe ou remplaçant : lettres accentuées absentes -> lettre de base, sinon « ? ».
static const GFXglyph* glypheOuRemplacant(const GFXfont* p, uint32_t cp) {
  const GFXglyph* g = glyphe(p, cp);
  if (g) return g;
  if (cp == 0x2212) g = glyphe(p, '-');
  else if (cp == 0xA0) g = glyphe(p, ' ');
  else if (cp >= 0xC0 && cp <= 0xFF) {
    static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    g = glyphe(p, (uint8_t)base[cp - 0xC0]);
  }
  if (!g) g = glyphe(p, '?');
  return g;
}

int largeurN(const GFXfont* p, const char* s, int nbOctets) {
  int l = 0;
  const char* fin = s + nbOctets;
  while (s < fin && *s) {
    const uint32_t cp = utf8Lire(s);
    const GFXglyph* g = glypheOuRemplacant(p, cp);
    if (g) l += g->advance_x;
  }
  return l;
}

int largeur(const GFXfont* p, const char* s) { return largeurN(p, s, (int)strlen(s)); }

int hautCapitale(const GFXfont* p) {
  const GFXglyph* g = glyphe(p, 'H');
  if (!g) g = glyphe(p, '0');
  return g ? g->top : p->ascender;
}

int hauteurLigne(const GFXfont* p) { return p->advance_y; }

int baseCentree(const GFXfont* p, int y, int h) { return y + (h + hautCapitale(p)) / 2; }

static int dessinerGlyphe(const GFXfont* p, const GFXglyph* g, int x, int yBase, uint8_t c) {
  if (p->compressed) return g->advance_x;   // non géré : les polices sont générées sans compression
  const int octetsLigne = (g->width + 1) / 2;
  const uint8_t* bm = p->bitmap + g->data_offset;
  uint8_t* t = Ecran::toile();
  const int gx = x + g->left;
  const int gy = yBase - g->top;
  for (int j = 0; j < g->height; j++) {
    const int yy = gy + j;
    if (yy < s_cy0 || yy >= s_cy1) continue;
    uint8_t* lignePix = t + yy * ECRAN_L;
    const uint8_t* src = bm + j * octetsLigne;
    for (int i = 0; i < g->width; i++) {
      const int xx = gx + i;
      if (xx < s_cx0 || xx >= s_cx1) continue;
      uint8_t v = src[i >> 1];
      v = (i & 1) ? (uint8_t)(v >> 4) : (uint8_t)(v & 0x0F);
      if (!v) continue;
      if (v == 15) {
        lignePix[xx] = c;
      } else {
        const int o = lignePix[xx];
        const int diff = (int)c - o;
        lignePix[xx] = (uint8_t)(o + (diff * v + (diff >= 0 ? 7 : -7)) / 15);
      }
    }
  }
  return g->advance_x;
}

int texteN(const GFXfont* p, int x, int y, const char* s, int nbOctets, uint8_t c) {
  const char* fin = s + nbOctets;
  c &= 0x0F;
  while (s < fin && *s) {
    const uint32_t cp = utf8Lire(s);
    if (cp == '\n') continue;
    const GFXglyph* g = glypheOuRemplacant(p, cp);
    if (g) x += dessinerGlyphe(p, g, x, y, c);
  }
  return x;
}

int texte(const GFXfont* p, int x, int y, const char* s, uint8_t c) {
  return texteN(p, x, y, s, (int)strlen(s), c);
}

void texteCentre(const GFXfont* p, int cx, int y, const char* s, uint8_t c) {
  texte(p, cx - largeur(p, s) / 2, y, s, c);
}

void texteDroite(const GFXfont* p, int xDroite, int y, const char* s, uint8_t c) {
  texte(p, xDroite - largeur(p, s), y, s, c);
}

int texteTronque(const GFXfont* p, int x, int y, int largeurMax, const char* s, uint8_t c) {
  const int l = largeur(p, s);
  if (l <= largeurMax) {
    texte(p, x, y, s, c);
    return l;
  }
  static const char ELLIPSE[] = "\xE2\x80\xA6";   // …
  const int le = largeur(p, ELLIPSE);
  int n = 0, lc = 0;
  const char* q = s;
  while (*q) {
    const char* avant = q;
    const uint32_t cp = utf8Lire(q);
    const GFXglyph* g = glypheOuRemplacant(p, cp);
    const int a = g ? g->advance_x : 0;
    if (lc + a + le > largeurMax) { q = avant; break; }
    lc += a;
    n = (int)(q - s);
  }
  int xf = texteN(p, x, y, s, n, c);
  xf = texte(p, xf, y, ELLIPSE, c);
  return xf - x;
}

int couperLignes(const GFXfont* p, const char* s, int largeurMax, LigneTexte* lignes, int maxLignes) {
  int nb = 0;
  int debut = 0;
  const int total = (int)strlen(s);
  while (debut <= total && nb < maxLignes) {
    int pos = debut;
    int l = 0;
    int dernierEspace = -1;
    bool finLigne = false;
    while (pos < total) {
      if (s[pos] == '\n') { finLigne = true; break; }
      const char* q = s + pos;
      const uint32_t cp = utf8Lire(q);
      const GFXglyph* g = glypheOuRemplacant(p, cp);
      const int a = g ? g->advance_x : 0;
      if (l + a > largeurMax && pos > debut) break;
      if (cp == ' ') dernierEspace = pos;
      l += a;
      pos = (int)(q - s);
    }
    if (finLigne) {
      lignes[nb].debut = debut; lignes[nb].fin = pos; nb++;
      debut = pos + 1;
      continue;
    }
    if (pos >= total) {
      lignes[nb].debut = debut; lignes[nb].fin = total; nb++;
      break;
    }
    // Ligne trop longue : coupe sur l'espace qui déborde, sinon au dernier espace, sinon dans le mot.
    if (s[pos] == ' ') {
      lignes[nb].debut = debut; lignes[nb].fin = pos; nb++;
      debut = pos + 1;
    } else if (dernierEspace > debut) {
      lignes[nb].debut = debut; lignes[nb].fin = dernierEspace; nb++;
      debut = dernierEspace + 1;
    } else {
      lignes[nb].debut = debut; lignes[nb].fin = pos; nb++;
      debut = pos;
    }
  }
  return nb;
}

// ---------------------------------------------------------------------------
//  Pixel art
// ---------------------------------------------------------------------------
void motif(const char* const* lignes, int nbLignes, int x, int y, int taille, uint8_t c, int ecart) {
  for (int j = 0; j < nbLignes; j++) {
    const char* l = lignes[j];
    for (int i = 0; l[i]; i++) {
      if (l[i] == '#' || l[i] == '+') {
        rectPlein(x + i * taille + ecart, y + j * taille + ecart,
                  taille - 2 * ecart, taille - 2 * ecart, l[i] == '#' ? c : GRIS);
      }
    }
  }
}

static const char* const IC_TACHES[] = {
  "############", "#..........#", "#.........##", "#........#.#",
  "#.......##.#", "#.#....##..#", "#.##..##...#", "#..####....#",
  "#...##.....#", "#..........#", "#..........#", "############",
};
static const char* const IC_NOTES[] = {
  ".##########.", ".#........#.", ".#.######.#.", ".#........#.",
  ".#.######.#.", ".#........#.", ".#.######.#.", ".#........#.",
  ".#.####...#.", ".#........#.", ".##########.", "............",
};
static const char* const IC_POMODORO[] = {
  "....####....", "..########..", ".##......##.", ".#...#....#.",
  "#....#.....#", "#....#.....#", "#....####..#", "#..........#",
  "#..........#", ".#........#.", ".##......##.", "..########..",
};
static const char* const IC_CALENDRIER[] = {
  "..#.....#...", "############", "#..........#", "############",
  "#..........#", "#.##.##.##.#", "#.##.##.##.#", "#..........#",
  "#.##.##....#", "#.##.##....#", "#..........#", "############",
};
static const char* const IC_CALCULATRICE[] = {
  "############", "#..........#", "#.########.#", "#.########.#",
  "#..........#", "#.##.##.##.#", "#..........#", "#.##.##.##.#",
  "#..........#", "#.##.##.##.#", "#..........#", "############",
};
static const char* const IC_REGLAGES[] = {
  ".....##.....", "..#..##..#..", ".###.##.###.", "..########..",
  "..##....##..", "####....####", "####....####", "..##....##..",
  "..########..", ".###.##.###.", "..#..##..#..", ".....##.....",
};
static const char* const IC_ACCUEIL[] = {
  ".....##.....", "....####....", "...##..##...", "..##....##..",
  ".##......##.", "############", ".#........#.", ".#..###...#.",
  ".#..#.#...#.", ".#..#.#...#.", ".#..#.#...#.", ".##########.",
};
static const char* const IC_COCHE[] = {
  "......#", ".....##", "#...##.", "##.##..", ".###...", "..#....",
};
static const char* const IC_CHEVRON_G[] = {
  "...##", "..##.", ".##..", "##...", ".##..", "..##.", "...##",
};
static const char* const IC_CHEVRON_D[] = {
  "##...", ".##..", "..##.", "...##", "..##.", ".##..", "##...",
};

static const char* const IC_MAJ[] = {
  "....#....", "...#.#...", "..#...#..", ".#.....#.", "###...###", "..#...#..", "..#...#..", "..#####..",
};
static const char* const IC_MAJ_PLEIN[] = {
  "....#....", "...###...", "..#####..", ".#######.", "#########", "..#####..", "..#####..", "..#####..",
};
static const char* const IC_EFFACER[] = {
  "..#.......", ".##.......", "##########", ".##.......", "..#.......", "..........", "..........",
};
static const char* const IC_ENTREE[] = {
  ".........#", ".........#", "..#......#", ".##......#", "##########", ".##.......", "..#.......",
};
static const char* const IC_RECHERCHE[] = {
  "..####......", ".##..##.....", "##....##....", "#......#....",
  "#......#....", "##....##....", ".##..##.....", "..######....",
  "......###...", ".......###..", "........###.", ".........##.",
};
static const char* const IC_BILAN[] = {
  "............", "........##..", "........##..", "........##..",
  "....##..##..", "....##..##..", "....##..##..", "##..##..##..",
  "##..##..##..", "##..##..##..", "............", "############",
};

struct DefIcone {
  const char* const* lignes;
  uint8_t l, h;
};
static const DefIcone ICONES[ICONE_NB] = {
  {IC_TACHES, 12, 12},     {IC_NOTES, 12, 12},        {IC_POMODORO, 12, 12},
  {IC_CALENDRIER, 12, 12}, {IC_CALCULATRICE, 12, 12}, {IC_REGLAGES, 12, 12},
  {IC_ACCUEIL, 12, 12},    {IC_COCHE, 7, 6},          {IC_CHEVRON_G, 5, 7},
  {IC_CHEVRON_D, 5, 7},    {IC_MAJ, 9, 8},            {IC_MAJ_PLEIN, 9, 8},
  {IC_EFFACER, 10, 5},     {IC_ENTREE, 10, 7},        {IC_RECHERCHE, 12, 12},
  {IC_BILAN, 12, 12},
};

void icone(Icone i, int x, int y, int taille, uint8_t c) {
  if (i >= ICONE_NB) return;
  motif(ICONES[i].lignes, ICONES[i].h, x, y, taille, c, 0);
}
int iconeLargeur(Icone i) { return i < ICONE_NB ? ICONES[i].l : 0; }
int iconeHauteur(Icone i) { return i < ICONE_NB ? ICONES[i].h : 0; }

void visagePamk(int cx, int y, int p, Expression e, uint8_t c) {
  const int x = cx - (11 * p) / 2;
  const int g = maxi(1, p / 12);   // petit écart entre les « pixels », comme sur la plaque
  auto bloc = [&](int col, int ligneDemi) {   // ligneDemi : position verticale en demi-pixels
    rectPlein(x + col * p + g, y + (ligneDemi * p) / 2 + g, p - 2 * g, p - 2 * g, c);
  };
  // Yeux (colonnes 0-3 et 7-10)
  for (int oeil = 0; oeil < 2; oeil++) {
    const int ox = oeil * 7;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        bool on = false;
        switch (e) {
          case VISAGE_NORMAL:
            on = (i == 0 || i == 3 || j == 0 || j == 3);
            break;
          case VISAGE_DORT:      // yeux fermés : un trait en bas
          case VISAGE_ETEINT:
            on = (j == 3);
            break;
          case VISAGE_CONTENT:   // yeux en accent circonflexe
            on = (j == 1 && (i == 1 || i == 2)) || (j == 2 && (i == 0 || i == 3));
            break;
          case VISAGE_FATIGUE:   // paupières à moitié fermées
            on = (j == 3) || (j == 2 && (i == 0 || i == 3)) || (j == 1 && i >= 0 && i <= 3);
            break;
        }
        if (on) bloc(ox + i, j * 2);
      }
    }
  }
  // Bouche (rangées 5,5 et 6,5 en unités de pixel, comme le visuel de concept)
  for (int i = 0; i < 9; i++) {
    int ligneDemi;
    bool on = true;
    switch (e) {
      case VISAGE_NORMAL:
      case VISAGE_CONTENT:
        ligneDemi = (i % 2) ? 13 : 11;
        break;
      case VISAGE_DORT:
        ligneDemi = 12;
        on = (i >= 3 && i <= 5);
        break;
      default:   // éteint / fatigué : bouche plate
        ligneDemi = 12;
        on = (i >= 1 && i <= 7);
        break;
    }
    if (on) bloc(1 + i, ligneDemi);
  }
}

// ---------------------------------------------------------------------------
//  Pictogrammes
// ---------------------------------------------------------------------------
void pictoBatterie(int x, int y, int pourcentage, bool charge, uint8_t c) {
  // Corps 34 x 20, téton 4 x 8
  rect(x, y, 34, 20, 3, c);
  rectPlein(x + 34, y + 6, 4, 8, c);
  const int pleine = borner(pourcentage, 0, 100) * 26 / 100;
  if (pleine > 0) rectPlein(x + 4, y + 4, pleine, 12, c);
  if (charge) {   // éclair stylisé (blanc sur le remplissage, sinon de la couleur du contour)
    const uint8_t e = pleine > 16 ? BLANC : c;
    ligne(x + 19, y + 3, x + 14, y + 10, e, 2);
    ligne(x + 14, y + 10, x + 20, y + 10, e, 2);
    ligne(x + 20, y + 10, x + 15, y + 17, e, 2);
  }
}

void pictoWifi(int x, int y, int barres, uint8_t c) {
  for (int i = 0; i < 4; i++) {
    const int h = 7 + i * 5;
    rectPlein(x + i * 8, y + 22 - h, 6, h, i < barres ? c : GRIS_CLAIR);
  }
}

void pictoPlus(int cx, int cy, int t, int ep, uint8_t c) {
  rectPlein(cx - t / 2, cy - ep / 2, t, ep, c);
  rectPlein(cx - ep / 2, cy - t / 2, ep, t, c);
}

void pictoCroix(int cx, int cy, int t, int ep, uint8_t c) {
  const int d = t / 2;
  ligne(cx - d, cy - d, cx + d, cy + d, c, ep);
  ligne(cx - d, cy + d, cx + d, cy - d, c, ep);
}

}  // namespace Dessin
