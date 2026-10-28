#include "ecran.h"
#include "epd_driver.h"
#include <esp_heap_caps.h>
#include <string.h>
#include <limits.h>

// =============================================================================
//  Rappels sur le pilote LilyGo-EPD47 1.1.0 (src/epd_driver.c), sur lesquels tout repose
//
//  - epd_draw_image(zone, image, mode) (l. 816-855) envoie 15 trames. La table de
//    conversion est remise à « foncer » (BLACK_ON_WHITE, 0x55) ou « éclaircir »
//    (WHITE_ON_BLACK et WHITE_ON_WHITE, 0xAA) à la trame 0 (reset_lut, l. 915-930), puis
//    update_LUT (l. 933-965) arrête un pixel de valeur v :
//      BLACK_ON_WHITE : fonce pendant 15 - v trames (v = 15 : rien ; v = 0 : 15 trames) ;
//      WHITE_ON_WHITE : éclaircit pendant 15 - v trames (même règle, sens inverse) ;
//      WHITE_ON_BLACK : éclaircit pendant v trames (v = 15 : à fond !).
//    Durée de chaque trame : contrast_cycles_4 (BLACK_ON_WHITE et WHITE_ON_WHITE) ou
//    contrast_cycles_4_white (WHITE_ON_BLACK) (l. 110-112 et 1076-1085).
//  - Pour une zone partielle, provide_out (l. 990-1069) complète chaque ligne hors zone
//    avec 0xFF (l. 993), c'est-à-dire la valeur 15, et saute les lignes hors zone
//    (feed_display, l. 1088-1094). 15 = « rien » en BLACK_ON_WHITE et WHITE_ON_WHITE,
//    mais « éclaircir à fond » en WHITE_ON_BLACK : on n'utilise donc JAMAIS WHITE_ON_BLACK.
//  - epd_clear_area_cycles / epd_push_pixels (l. 169-240) foncent puis éclaircissent TOUS
//    les pixels d'un rectangle : c'était le rectangle noir de la V0.1. On ne s'en sert plus
//    que pour l'image complète (epd_clear) et le nettoyage demandé.
//  - Le temps d'une passe dépend du nombre de LIGNES de la dalle touchées (les autres sont
//    sautées, skip_row l. 873-902), pas de la largeur. En portrait, une ligne de la dalle =
//    une colonne x de l'écran : on regroupe donc les changements par colonnes.
//  - Le nettoyage pixel par pixel n'utilise pas epd_draw_image : 15 trames très courtes
//    (contrast_cycles_4, 102 µs en tout) ne ramenaient pas un pixel noir au blanc du fond,
//    d'où des zones un peu grises. pousserMasque() refait les poussées de epd_push_pixels
//    (4 x 50 µs au noir puis 4 x 50 µs au blanc, comme un cycle de epd_clear) mais ligne par
//    ligne, avec un masque : seuls les pixels à nettoyer sont pilotés.
// =============================================================================

// Sorties ligne par ligne de la dalle (ed047tc1.c) et état du bus parallèle (i2s_data_bus.c).
// Sur l'ESP32-S3, la bibliothèque n'a qu'UN tampon de ligne (USER_I2S_REG = 0, l. 22 et 106) :
// on attend la fin de son envoi avant de le remplir.
#include <esp_timer.h>
#include "ed047tc1.h"
extern "C" bool i2s_is_busy();   // i2s_data_bus.c (son en-tête met IRAM_ATTR sur les déclarations)

namespace Ecran {

static const int TAILLE_TOILE = ECRAN_L * ECRAN_H;           // 518 400 octets
static const int TAILLE_NATIF = NATIF_L * NATIF_H / 2;       // 259 200 octets (4 bits/pixel)
static const uint8_t INCONNU = 0xFF;                         // pixel dont on ignore l'état
static const uint8_t FORCE = 0xFE;     // pixel non blanc inchangé, mais à nettoyer et redessiner
static const int OCTETS_LIGNE = NATIF_L / 4;                 // 240 : ligne envoyée à la dalle (2 bits/pixel)
static const int PILE_MAX = 1 << 17;                         // pixels en attente dans etendreNettoyage()

static uint8_t* s_toile = nullptr;     // ce qu'on veut afficher (portrait)
static uint8_t* s_affiche = nullptr;   // ce que la dalle montre (portrait)
static uint8_t* s_natif = nullptr;     // image native envoyée au pilote
static uint8_t* s_masque = nullptr;    // pixels à nettoyer, lignes natives prêtes à envoyer
static bool s_ligneMasque[NATIF_H];    // la ligne native a au moins un pixel à nettoyer
static uint32_t* s_pile = nullptr;
static int s_nbPile = 0;
static int s_saut = 0;                 // lignes sautées d'affilée (comme « skipping », epd_driver.c)
static uint32_t s_dureeLigne = 10;     // durée de la ligne qui attend d'être verrouillée
static int64_t s_finImpulsion = 0;     // fin de l'impulsion CKV de la dernière ligne (µs)
static uint8_t s_orient = ORIENTATION_DEFAUT;
static uint8_t s_mode = MAJ_PIXELS;
static uint16_t s_completTous = 0;     // 0 = jamais
static uint16_t s_partiels = 0;
static void (*s_rappel)() = nullptr;
static uint32_t s_nb = 0;
static uint32_t s_duree = 0;
static char s_texteMode[48] = "aucun";

// Besoins par colonne portrait (= ligne native)
enum : uint8_t { B_NETTOYER = 1, B_CIBLE = 2, B_ECLAIRCIR = 4, B_ASSOMBRIR = 8 };
static uint8_t s_colBesoin[ECRAN_L];
static int16_t s_colY0[ECRAN_L];
static int16_t s_colY1[ECRAN_L];

// Valeurs envoyées au pilote pour chaque passe
enum Filtre : uint8_t {
  F_NORMAL,      // image entière (rafraîchissement complet)
  F_NETTOYER,    // 0 sur les pixels à nettoyer, 15 ailleurs (BLACK_ON_WHITE puis WHITE_ON_WHITE)
  F_CIBLE,       // nouvelle valeur sur les pixels à dessiner depuis le blanc, 15 ailleurs
  F_ECLAIRCIR,   // mode doux : 15 - écart sur les pixels à éclaircir (WHITE_ON_WHITE)
  F_ASSOMBRIR,   // mode doux : 15 - écart sur les pixels à foncer (BLACK_ON_WHITE)
};

struct Groupe {
  int x0, x1, y0, y1;
  uint8_t besoin;
};

bool demarrer() {
  s_toile = (uint8_t*)heap_caps_malloc(TAILLE_TOILE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_affiche = (uint8_t*)heap_caps_malloc(TAILLE_TOILE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_natif = (uint8_t*)heap_caps_malloc(TAILLE_NATIF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_masque = (uint8_t*)heap_caps_malloc(NATIF_H * OCTETS_LIGNE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_pile = (uint32_t*)heap_caps_malloc(PILE_MAX * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_toile || !s_affiche || !s_natif || !s_masque || !s_pile) {
    JOURNAL("[ecran] ERREUR : PSRAM insuffisante (OPI PSRAM activée ?)");
    return false;
  }
  memset(s_toile, BLANC, TAILLE_TOILE);
  memset(s_affiche, INCONNU, TAILLE_TOILE);
  memset(s_natif, 0xFF, TAILLE_NATIF);
  epd_init();   // tampons internes du pilote (table de conversion 64 Kio, file d'attente)
  return true;
}

uint8_t* toile() { return s_toile; }

void effacer(uint8_t couleur) { memset(s_toile, couleur & 0x0F, TAILLE_TOILE); }

// ---------------------------------------------------------------------------
//  Passage portrait <-> natif
//  Orientation 1 : x_natif = 959 - y, y_natif = x
//  Orientation 0 : x_natif = y,       y_natif = 539 - x
// ---------------------------------------------------------------------------
static Rect_t versNatif(int x, int y, int l, int h) {
  Rect_t r;
  if (s_orient == 1) {
    r.x = NATIF_L - y - h;
    r.y = x;
  } else {
    r.x = y;
    r.y = NATIF_H - x - l;
  }
  r.width = h;
  r.height = l;
  // Le pilote range 2 pixels par octet : on aligne x et la largeur sur un nombre pair.
  if (r.x & 1) { r.x -= 1; r.width += 1; }
  if (r.width & 1) r.width += 1;
  if (r.x + r.width > NATIF_L) r.width = NATIF_L - r.x;
  return r;
}

static Boite versPortrait(const Rect_t& r) {
  Boite b;
  if (s_orient == 1) {
    b.y = NATIF_L - (int)r.x - (int)r.width;
    b.x = (int)r.y;
  } else {
    b.y = (int)r.x;
    b.x = NATIF_H - (int)r.y - (int)r.height;
  }
  b.h = (int)r.width;
  b.l = (int)r.height;
  return b;
}

// Le pixel a-t-il besoin d'être nettoyé avant de recevoir sa nouvelle valeur ?
static inline bool aNettoyer(uint8_t ancien) {
  if (ancien == INCONNU) return true;                 // état réel inconnu : on repart d'un blanc propre
  if (ancien == FORCE) return true;                   // collé à un changement (etendreNettoyage)
  return s_mode == MAJ_PIXELS && ancien != BLANC;     // mode pixels : tout ce qui n'était pas blanc
}

static inline uint8_t valeur(int idx, Filtre f) {
  const uint8_t n = s_toile[idx];
  if (f == F_NORMAL) return n;
  const uint8_t a = s_affiche[idx];
  if (n == a) return BLANC;                           // inchangé : valeur neutre (15 = rien)
  switch (f) {
    case F_NETTOYER:
      return aNettoyer(a) ? 0 : BLANC;
    case F_CIBLE:
      // depuis le blanc : pixels nettoyés, ou anciens pixels blancs
      if (n == BLANC) return BLANC;
      return (aNettoyer(a) || a == BLANC) ? n : BLANC;
    case F_ECLAIRCIR:
      return (!aNettoyer(a) && n > a) ? (uint8_t)(BLANC - (n - a)) : BLANC;
    case F_ASSOMBRIR:
      return (!aNettoyer(a) && a != BLANC && n < a) ? (uint8_t)(BLANC - (a - n)) : BLANC;
    default:
      return BLANC;
  }
}

// Remplit s_natif avec l'image du rectangle natif r (r.x et r.width pairs).
static void construire(const Rect_t& r, Filtre f) {
  const int octetsLigne = (int)r.width / 2;
  for (int j = 0; j < (int)r.height; j++) {
    const int ny = (int)r.y + j;
    int idx, pas;
    if (s_orient == 1) {
      idx = (NATIF_L - 1 - (int)r.x) * ECRAN_L + ny;
      pas = -ECRAN_L;
    } else {
      idx = (int)r.x * ECRAN_L + (NATIF_H - 1 - ny);
      pas = ECRAN_L;
    }
    uint8_t* dst = s_natif + j * octetsLigne;
    for (int i = 0; i < octetsLigne; i++) {
      const uint8_t a = valeur(idx, f);   // x pair  -> quartet bas  (cf. epd_draw_pixel, l. 328-347)
      idx += pas;
      const uint8_t b = valeur(idx, f);   // x impair -> quartet haut
      idx += pas;
      dst[i] = (uint8_t)((a & 0x0F) | (b << 4));
    }
  }
}

static void copierVersAffiche(const Boite& b) {
  const int x0 = maxi(0, b.x), x1 = mini(ECRAN_L, b.x + b.l);
  const int y0 = maxi(0, b.y), y1 = mini(ECRAN_H, b.y + b.h);
  if (x1 <= x0) return;
  for (int y = y0; y < y1; y++) {
    memcpy(s_affiche + y * ECRAN_L + x0, s_toile + y * ECRAN_L + x0, x1 - x0);
  }
}

// Classe chaque pixel changé et note, par colonne, ce qu'il faut faire.
// Retourne le nombre de pixels changés ; inconnus = pixels dont l'état était inconnu.
static long analyser(long& inconnus) {
  memset(s_colBesoin, 0, sizeof(s_colBesoin));
  long changes = 0;
  inconnus = 0;
  for (int y = 0; y < ECRAN_H; y++) {
    const uint32_t* t = (const uint32_t*)(s_toile + y * ECRAN_L);    // 540 = 135 mots
    const uint32_t* a = (const uint32_t*)(s_affiche + y * ECRAN_L);
    const uint8_t* tl = s_toile + y * ECRAN_L;
    const uint8_t* al = s_affiche + y * ECRAN_L;
    for (int w = 0; w < ECRAN_L / 4; w++) {
      if (t[w] == a[w]) continue;
      for (int x = w * 4; x < w * 4 + 4; x++) {
        const uint8_t n = tl[x], o = al[x];
        if (n == o) continue;
        changes++;
        uint8_t besoin;
        if (o == INCONNU) {
          inconnus++;
          besoin = (uint8_t)(B_NETTOYER | (n != BLANC ? B_CIBLE : 0));
        } else if (s_mode == MAJ_PIXELS) {
          besoin = (uint8_t)((o != BLANC ? B_NETTOYER : 0) | (n != BLANC ? B_CIBLE : 0));
        } else if (o == BLANC) {
          besoin = B_CIBLE;   // doux : depuis le blanc, c'est un dessin normal
        } else {
          besoin = n > o ? B_ECLAIRCIR : B_ASSOMBRIR;
        }
        if (!s_colBesoin[x]) {
          s_colY0[x] = (int16_t)y;
        }
        s_colY1[x] = (int16_t)y;
        s_colBesoin[x] |= besoin;
      }
    }
  }
  return changes;
}

// Pixel non blanc qui ne change pas (et pas encore marqué : FORCE et INCONNU diffèrent
// toujours de la toile, qui va de 0 à 15).
static inline bool aEtendre(int i) {
  const uint8_t a = s_affiche[i];
  return a != BLANC && a == s_toile[i];
}

static inline void marquer(int i) {
  s_affiche[i] = FORCE;
  if (s_nbPile < PILE_MAX) s_pile[s_nbPile++] = (uint32_t)i;   // pile pleine : nettoyé mais pas étendu
}

static void examinerVoisins(int i) {
  const int x = i % ECRAN_L;
  if (x > 0 && aEtendre(i - 1)) marquer(i - 1);
  if (x < ECRAN_L - 1 && aEtendre(i + 1)) marquer(i + 1);
  if (i >= ECRAN_L && aEtendre(i - ECRAN_L)) marquer(i - ECRAN_L);
  if (i < TAILLE_TOILE - ECRAN_L && aEtendre(i + ECRAN_L)) marquer(i + ECRAN_L);
}

// Mode pixels : les pixels non blancs qui ne changent pas mais qui touchent (de proche en
// proche) un pixel qui change sont nettoyés et redessinés avec lui. Avant, la partie commune
// à l'ancienne et à la nouvelle image (morceau de lettre, bord de forme) restait noire
// pendant l'étape blanche. Une forme identique et isolée, elle, ne bouge pas.
// Retourne le nombre de pixels marqués.
static long etendreNettoyage() {
  long marques = 0;
  s_nbPile = 0;
  for (int y = 0; y < ECRAN_H; y++) {
    const uint32_t* t = (const uint32_t*)(s_toile + y * ECRAN_L);
    const uint32_t* a = (const uint32_t*)(s_affiche + y * ECRAN_L);
    for (int w = 0; w < ECRAN_L / 4; w++) {
      if (t[w] == a[w]) continue;
      for (int x = w * 4; x < w * 4 + 4; x++) {
        const int i = y * ECRAN_L + x;
        const uint8_t o = s_affiche[i];
        if (o == s_toile[i] || o == FORCE) continue;   // inchangé, ou déjà traité par la pile
        examinerVoisins(i);
        while (s_nbPile > 0) {
          marques++;
          examinerVoisins((int)s_pile[--s_nbPile]);
        }
      }
    }
  }
  return marques;
}

// Regroupe les colonnes à traiter. Deux groupes séparés de moins de MAJ_ECART_FUSION_PX
// colonnes sont fusionnés (une passe de plus coûte plus cher que quelques lignes de dalle).
static int grouper(Groupe* g) {
  int nb = 0;
  for (int x = 0; x < ECRAN_L; x++) {
    if (!s_colBesoin[x]) continue;
    if (nb > 0 && x - g[nb - 1].x1 <= MAJ_ECART_FUSION_PX) {
      Groupe& c = g[nb - 1];
      c.x1 = x;
      c.y0 = mini(c.y0, s_colY0[x]);
      c.y1 = maxi(c.y1, s_colY1[x]);
      c.besoin |= s_colBesoin[x];
    } else if (nb < 16) {
      Groupe& c = g[nb++];
      c.x0 = c.x1 = x;
      c.y0 = s_colY0[x];
      c.y1 = s_colY1[x];
      c.besoin = s_colBesoin[x];
    } else {
      Groupe& c = g[nb - 1];   // ne devrait pas arriver (écart minimal 110 px)
      c.x1 = x;
      c.y0 = mini(c.y0, s_colY0[x]);
      c.y1 = maxi(c.y1, s_colY1[x]);
      c.besoin |= s_colBesoin[x];
    }
  }
  while (nb > MAJ_GROUPES_MAX) {   // trop de groupes : on fusionne les plus proches
    int meilleur = 0, ecart = INT_MAX;
    for (int i = 0; i + 1 < nb; i++) {
      const int e = g[i + 1].x0 - g[i].x1;
      if (e < ecart) { ecart = e; meilleur = i; }
    }
    Groupe& a = g[meilleur];
    const Groupe& b = g[meilleur + 1];
    a.x1 = b.x1;
    a.y0 = mini(a.y0, b.y0);
    a.y1 = maxi(a.y1, b.y1);
    a.besoin |= b.besoin;
    for (int i = meilleur + 1; i + 1 < nb; i++) g[i] = g[i + 1];
    nb--;
  }
  return nb;
}

static void dessinerComplet() {
  const Rect_t plein = epd_full_screen();
  construire(plein, F_NORMAL);
  epd_clear();                                   // 4 cycles noir/blanc sur tout l'écran (l. 250-253)
  epd_draw_grayscale_image(plein, s_natif);
  memcpy(s_affiche, s_toile, TAILLE_TOILE);
  s_partiels = 0;
  snprintf(s_texteMode, sizeof(s_texteMode), "complet");
}

// Remplit s_masque pour les groupes à nettoyer : « 01 » (foncer) sur chaque pixel à nettoyer,
// 2 bits par pixel, pixel x : octet x / 4, bits 2 * (x % 4). C'est l'ordre de
// calc_epd_input_4bpp sur l'ESP32-S3 (USER_I2S_REG n'est pas défini dans epd_driver.c :
// octets dans l'ordre naturel). Surtout PAS l'échange des moitiés de 16 bits de
// epd_push_pixels (reorder_line_buffer, pour l'ancien bus I2S) : il décalait le nettoyage
// de 8 pixels (résidus). Ça ne se voit pas avec epd_clear, qui couvre toute la ligne.
// Retourne vrai s'il y a au moins un pixel à nettoyer.
static bool preparerMasque(const Groupe* g, const Rect_t* r, int nb) {
  memset(s_ligneMasque, 0, sizeof(s_ligneMasque));
  bool unPixel = false;
  for (int i = 0; i < nb; i++) {
    if (!(g[i].besoin & B_NETTOYER)) continue;
    construire(r[i], F_NETTOYER);   // 0 = à nettoyer, 15 ailleurs
    const int octets = (int)r[i].width / 2;
    for (int j = 0; j < (int)r[i].height; j++) {
      const int ny = (int)r[i].y + j;
      uint8_t* ligne = s_masque + ny * OCTETS_LIGNE;
      memset(ligne, 0, OCTETS_LIGNE);
      const uint8_t* src = s_natif + j * octets;
      bool vu = false;
      for (int k = 0; k < octets; k++) {
        const uint8_t v = src[k];
        if (v == 0xFF) continue;
        const int nx = (int)r[i].x + 2 * k;   // r.x est pair
        if ((v & 0x0F) == 0) { ligne[nx >> 2] |= (uint8_t)(1 << ((nx & 3) * 2)); vu = true; }
        if ((v >> 4) == 0) { ligne[(nx + 1) >> 2] |= (uint8_t)(1 << (((nx + 1) & 3) * 2)); vu = true; }
      }
      if (!vu) continue;
      s_ligneMasque[ny] = true;
      unPixel = true;
    }
  }
  return unPixel;
}

// Avant de remplir le tampon de ligne ou d'en verrouiller une nouvelle : l'envoi précédent
// doit être fini (un seul tampon) et l'impulsion CKV de la ligne précédente aussi (sinon
// cette ligne recevrait la fin de la suivante).
static void attendreLigne() {
  while (i2s_is_busy()) {}
  while (esp_timer_get_time() < s_finImpulsion) {}
}

static void sortirLigne(uint32_t duree) {
  epd_output_row(duree);   // pulse_ckv_ticks(duree, 50) : haut puis bas, en 0,1 µs
  s_finImpulsion = esp_timer_get_time() + (duree + 50) / 10 + 1;
}

// Comme skip_row (epd_driver.c) : la première ligne sautée verrouille la dernière ligne
// pilotée (avec sa durée complète), la deuxième verrouille des zéros (= rien), les
// suivantes avancent seulement.
static void sauterLigne() {
  if (s_saut < 2) {
    attendreLigne();
    memset(epd_get_current_buffer(), 0, OCTETS_LIGNE);
    sortirLigne(s_saut == 0 ? s_dureeLigne : 10);
  } else {
    epd_skip();
  }
  s_saut++;
}

// Une trame : pousse au noir (blanc = false) ou au blanc les pixels du masque pendant
// duree (0,1 µs, comme epd_push_pixels). Les lignes sans pixel à nettoyer sont sautées.
static void pousserMasque(bool blanc, uint32_t duree) {
  epd_start_frame();
  s_saut = 0;
  s_dureeLigne = 10;   // rien de piloté avant la première ligne : verrouillage bref
  for (int y = 0; y < NATIF_H; y++) {
    if (!s_ligneMasque[y]) {
      sauterLigne();
      continue;
    }
    attendreLigne();
    uint32_t* dst = (uint32_t*)epd_get_current_buffer();
    const uint32_t* src = (const uint32_t*)(s_masque + y * OCTETS_LIGNE);
    // « 01 » (foncer) devient « 10 » (éclaircir) : un seul bit par paire (le bas), donc le
    // décalage reste dans la paire et ne passe jamais d'un octet à l'autre.
    for (int w = 0; w < OCTETS_LIGNE / 4; w++) dst[w] = blanc ? (src[w] << 1) : src[w];
    s_saut = 0;
    s_dureeLigne = duree;
    sortirLigne(duree);
  }
  // Dernière ligne verrouillée puis zéros : la bibliothèque retrouve une dalle qui ne pilote rien.
  while (s_saut < 2) sauterLigne();
  attendreLigne();
  epd_end_frame();
}

// Une étape (filtre + mode du pilote) sur tous les groupes qui en ont besoin.
static int etape(const Groupe* g, const Rect_t* r, int nb, uint8_t besoin, Filtre f, DrawMode_t m) {
  int passes = 0;
  for (int i = 0; i < nb; i++) {
    if (!(g[i].besoin & besoin)) continue;
    construire(r[i], f);   // s_natif est partagé : on le reconstruit pour chaque groupe
    epd_draw_image(r[i], s_natif, m);
    passes++;
  }
  return passes;
}

// Mise à jour pixel par pixel. Chaque étape est faite sur TOUS les groupes avant de passer
// à la suivante : tout l'écran s'efface puis se dessine en même temps. (Avant, un groupe
// était traité en entier avant le suivant, et le contenu des autres groupes restait
// affiché environ 0,5 s de trop.) Retourne le nombre de passes.
static int mettreAJourGroupes(const Groupe* g, int nb) {
  Rect_t r[16];
  for (int i = 0; i < nb; i++) r[i] = versNatif(g[i].x0, g[i].y0, g[i].x1 - g[i].x0 + 1, g[i].y1 - g[i].y0 + 1);
  int passes = 0;
  // Nettoyage des seuls pixels concernés, sur tout l'écran à la fois : au noir puis au blanc,
  // avec la même énergie qu'un cycle de epd_clear(). Les autres pixels ne sont pas pilotés.
  if (!MAJ_NETTOYAGE_FORT) {   // secours : ancien nettoyage, plus faible (zones grises)
    for (int c = 0; c < MAJ_CYCLES_PIXELS; c++) {
      passes += etape(g, r, nb, B_NETTOYER, F_NETTOYER, BLACK_ON_WHITE);
      passes += etape(g, r, nb, B_NETTOYER, F_NETTOYER, WHITE_ON_WHITE);
    }
  } else if (preparerMasque(g, r, nb)) {
    for (int c = 0; c < MAJ_CYCLES_PIXELS; c++) {
      for (int k = 0; k < MAJ_POUSSEES; k++) pousserMasque(false, DUREE_CYCLE_US * 10);
      for (int k = 0; k < MAJ_POUSSEES; k++) pousserMasque(true, DUREE_CYCLE_US * 10);
      passes += 2;
    }
  }
  passes += etape(g, r, nb, B_ECLAIRCIR, F_ECLAIRCIR, WHITE_ON_WHITE);
  passes += etape(g, r, nb, B_ASSOMBRIR, F_ASSOMBRIR, BLACK_ON_WHITE);
  passes += etape(g, r, nb, B_CIBLE, F_CIBLE, BLACK_ON_WHITE);   // = epd_draw_grayscale_image
  for (int i = 0; i < nb; i++) copierVersAffiche(versPortrait(r[i]));
  return passes;
}

void rafraichir(bool complet) {
  if (!s_toile) return;
  const uint32_t t0 = millis();
  long inconnus = 0;
  const long changes = analyser(inconnus);
  if (changes == 0 && !complet) return;

  const bool faireComplet = complet ||
                            inconnus * 100 > (long)TAILLE_TOILE * PART_INCONNU_COMPLET_PCT ||
                            (s_completTous > 0 && s_partiels >= s_completTous);
  if (!faireComplet && s_mode == MAJ_PIXELS && etendreNettoyage() > 0) {
    analyser(inconnus);   // ajoute les colonnes des pixels marqués
  }
  epd_poweron();
  if (faireComplet) {
    dessinerComplet();
  } else {
    Groupe groupes[16];
    const int nb = grouper(groupes);
    const int passes = mettreAJourGroupes(groupes, nb);
    if (s_partiels < 0xFFFF) s_partiels++;
    snprintf(s_texteMode, sizeof(s_texteMode), "%s, %d groupe%s, %d passe%s",
             s_mode == MAJ_DOUX ? "doux" : "pixels", nb, nb > 1 ? "s" : "", passes, passes > 1 ? "s" : "");
  }
  if (s_rappel) s_rappel();
  epd_poweroff_all();   // coupe aussi PWR_EN (LED bleue, pont batterie)
  s_nb++;
  s_duree = millis() - t0;
}

void nettoyerEnProfondeur() {
  if (!s_toile) return;
  const uint32_t t0 = millis();
  const Rect_t plein = epd_full_screen();
  epd_poweron();
  epd_clear_area_cycles(plein, 6, DUREE_CYCLE_US);
  for (int i = 0; i < 6; i++) epd_push_pixels(plein, 50, 0);    // tout foncer
  for (int i = 0; i < 12; i++) epd_push_pixels(plein, 50, 1);   // tout éclaircir
  dessinerComplet();
  snprintf(s_texteMode, sizeof(s_texteMode), "nettoyage");
  if (s_rappel) s_rappel();
  epd_poweroff_all();
  s_nb++;
  s_duree = millis() - t0;
}

void supposerAffiche() {
  if (s_toile) memcpy(s_affiche, s_toile, TAILLE_TOILE);
}

void invaliderZone(const Boite& b) {
  if (!s_affiche) return;
  const int x0 = maxi(0, b.x), x1 = mini(ECRAN_L, b.x + b.l);
  const int y0 = maxi(0, b.y), y1 = mini(ECRAN_H, b.y + b.h);
  if (x1 <= x0) return;
  for (int y = y0; y < y1; y++) memset(s_affiche + y * ECRAN_L + x0, INCONNU, x1 - x0);
}

void oublierAffichage() {
  if (s_affiche) memset(s_affiche, INCONNU, TAILLE_TOILE);
}

void definirOrientation(uint8_t o) {
  o = o ? 1 : 0;
  if (o != s_orient) {
    s_orient = o;
    oublierAffichage();
  }
}
uint8_t orientation() { return s_orient; }
void definirModeMaj(uint8_t mode) { s_mode = (mode == MAJ_DOUX) ? MAJ_DOUX : MAJ_PIXELS; }
void definirCompletTous(uint16_t n) { s_completTous = n; }
void definirRappelAlimente(void (*rappel)()) { s_rappel = rappel; }

void executerAlimente(void (*fonction)()) {
  epd_poweron();
  delay(10);   // comme l'exemple LilyGO « demo » avant de lire la batterie
  if (fonction) fonction();
  epd_poweroff_all();
}

uint32_t nbRafraichissements() { return s_nb; }
uint32_t dureeDerniereMs() { return s_duree; }
const char* dernierMode() { return s_texteMode; }

}  // namespace Ecran
