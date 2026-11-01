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
static const uint8_t M_FONCER = 1;                           // « 01 » dans le masque (2 bits par pixel)
static const uint8_t M_ECLAIRCIR = 2;                        // « 10 »
// Pixels écrits par le chemin rapide (affichage rapide, inversion au toucher) : la dalle est au
// noir ou au blanc, et s_affiche garde la valeur voulue v (0 à 15) : A_NOIR | v ou A_BLANC | v.
// Ces valeurs (0x20 à 0x3F) diffèrent toujours de la toile (0 à 15), d'INCONNU et de FORCE :
// la comparaison par mots de 32 bits les voit changées, et elles ne comptent pas en inconnus.
static const uint8_t A_NOIR = 0x20;
static const uint8_t A_BLANC = 0x30;
// Pixel rapide au noir qui doit revenir à sa valeur v : éclairci directement (comme en mode
// doux) si l'écart v est sous ECART_DIRECT_MAX, nettoyé au-delà. Bords gris des lettres : écart
// toujours < 8 (arrondi à 8), donc jamais nettoyés. Fond blanc d'une zone inversée au toucher
// (poussé au noir) : écart >= 8, car l'éclaircissement direct (102 µs au plus, voir plus haut)
// ne ramène pas un noir au vrai blanc.
static const uint8_t ECART_DIRECT_MAX = 8;

// Affichage rapide (clavier) et retour au toucher (28/10/2026) : RAPIDE_TRAMES trames de
// RAPIDE_DUREE (0,1 µs) où chaque pixel qui change est poussé d'un coup au noir ou au blanc.
// 4 x 50 µs, c'est la moitié « blanche » d'un cycle de epd_clear(), celle du nettoyage
// (pousserMasque) : assez pour ramener un noir au blanc du fond, alors que les 102 µs de
// epd_draw_image n'y arrivaient pas (voir plus haut). Vers le noir, c'est deux fois un noir de
// epd_draw_image : noir franc. Moins laisserait des lettres effacées grisâtres ; plus coûterait
// quelques ms par trame (toutes les lignes de la dalle sont parcourues) sans rien de mieux à voir.
static const int RAPIDE_TRAMES = 4;
static const uint32_t RAPIDE_DUREE = DUREE_CYCLE_US * 10;    // 50 µs
static const int RAPIDE_INCONNU_MAX_PCT = 5;                 // écran inconnu au-delà : affichage normal
// Retour au toucher : une zone plus grande (horloge de l'accueil) n'est inversée que sur un
// cadre de INVERSION_CADRE_PX pixels : moins de pixels à remettre d'aplomb ensuite.
static const long INVERSION_MAX_PX = 60000;
static const int INVERSION_CADRE_PX = 6;
// Image complète automatique selon l'usure (Param.nettoyageAuto == 3). Chaque mise à jour
// ajoute à l'usure ses pixels pilotés, multipliés par un poids : sans nettoyage, les traces
// s'accumulent plus vite. Au-delà de USURE_SEUIL, l'image complète est faite à la prochaine
// mise en veille (nettoyageConseille(), 29/10/2026) : on ne regarde plus l'écran, son flash ne
// gêne personne. Pendant l'utilisation, seulement au-delà de USURE_SECOURS (téléphone qui ne
// se met jamais en veille), sur un changement de page. L'usure repart de zéro après toute
// image complète.
static const uint32_t USURE_SEUIL = 6UL * ECRAN_L * ECRAN_H;      // ~3,1 millions : six écrans entiers
static const uint32_t USURE_SECOURS = 4UL * USURE_SEUIL;
static const uint32_t USURE_POIDS_PIXELS = 1;  // « pixels » : nettoyés à chaque fois (inversion au
                                               // toucher aussi : aller-retour noir/blanc)
static const uint32_t USURE_POIDS_DOUX = 2;    // « doux » : jamais nettoyés
static const uint32_t USURE_POIDS_RAPIDE = 4;  // rapide : poussés à fond, sans nettoyage
// Gros changement : au moins GROS_CHANGEMENT_PX pixels (~12 % de l'écran : un bandeau ou une
// ligne de réglage remise d'aplomb restent en dessous), sur au moins la moitié des colonnes.
static const long GROS_CHANGEMENT_PX = 60000;
static const int GROS_CHANGEMENT_COLONNES = ECRAN_L / 2;

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
static bool s_completAuto = false;     // image complète selon l'usure
static uint32_t s_usure = 0;           // pixels pilotés (pondérés) depuis la dernière image complète
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

// Pixels rapides (A_NOIR | v, A_BLANC | v)
static inline bool marque(uint8_t a) { return (a & 0xE0) == 0x20; }   // 0x20 à 0x3F
static inline uint8_t physique(uint8_t a) {                           // ce que montre vraiment la dalle
  return marque(a) ? ((a & 0x10) ? BLANC : NOIR) : a;
}
static inline uint8_t attendu(uint8_t a) { return a & 0x0F; }          // valeur voulue (pixel marqué)
// État à noter après le chemin rapide : dalle à phys (noir ou blanc), valeur voulue v.
static inline uint8_t etatRapide(uint8_t phys, uint8_t v) {
  return phys == v ? v : (uint8_t)((phys == BLANC ? A_BLANC : A_NOIR) | v);
}

// aNettoyer() pour la valeur brute de s_affiche (marquée ou non) et la nouvelle valeur n.
static inline bool nettoyerPixel(uint8_t brut, uint8_t n) {
  if (!marque(brut)) return aNettoyer(brut);
  if (brut & 0x10) return false;                       // au blanc : dessin depuis le blanc
  if (n >= ECART_DIRECT_MAX) return true;              // au noir, grand écart (= n) : vrai nettoyage
  return attendu(brut) != n && s_mode == MAJ_PIXELS;   // contenu changé : comme les autres pixels
}

// Pixel rapide dont la valeur voulue n'a pas changé : simple correction. Il ne sert pas de point
// de départ à etendreNettoyage() (sinon toute la lettre ou la touche serait nettoyée).
static inline bool aCorriger(uint8_t brut, uint8_t n) { return marque(brut) && attendu(brut) == n; }

static inline uint8_t valeur(int idx, Filtre f) {
  const uint8_t n = s_toile[idx];
  if (f == F_NORMAL) return n;
  const uint8_t brut = s_affiche[idx];
  const uint8_t a = physique(brut);
  if (n == a) return BLANC;                           // inchangé : valeur neutre (15 = rien)
  const bool nettoyer = nettoyerPixel(brut, n);
  switch (f) {
    case F_NETTOYER:
      return nettoyer ? 0 : BLANC;
    case F_CIBLE:
      // depuis le blanc : pixels nettoyés, ou anciens pixels blancs
      if (n == BLANC) return BLANC;
      return (nettoyer || a == BLANC) ? n : BLANC;
    case F_ECLAIRCIR:
      return (!nettoyer && n > a) ? (uint8_t)(BLANC - (n - a)) : BLANC;
    case F_ASSOMBRIR:
      return (!nettoyer && a != BLANC && n < a) ? (uint8_t)(BLANC - (a - n)) : BLANC;
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
// Un pixel rapide déjà à sa valeur (noir ou blanc voulu) est simplement noté comme affiché.
static long analyser(long& inconnus) {
  memset(s_colBesoin, 0, sizeof(s_colBesoin));
  long changes = 0;
  inconnus = 0;
  for (int y = 0; y < ECRAN_H; y++) {
    const uint32_t* t = (const uint32_t*)(s_toile + y * ECRAN_L);    // 540 = 135 mots
    const uint32_t* a = (const uint32_t*)(s_affiche + y * ECRAN_L);
    const uint8_t* tl = s_toile + y * ECRAN_L;
    uint8_t* al = s_affiche + y * ECRAN_L;
    for (int w = 0; w < ECRAN_L / 4; w++) {
      if (t[w] == a[w]) continue;
      for (int x = w * 4; x < w * 4 + 4; x++) {
        const uint8_t n = tl[x], o = al[x];
        if (n == o) continue;
        if (marque(o) && physique(o) == n) {
          al[x] = n;
          continue;
        }
        changes++;
        uint8_t besoin;
        if (o == INCONNU) {
          inconnus++;
          besoin = (uint8_t)(B_NETTOYER | (n != BLANC ? B_CIBLE : 0));
        } else if (marque(o)) {
          // Pixel rapide : nettoyage (grand écart depuis le noir, ou contenu changé en mode
          // pixels), sinon dessin depuis le blanc ou éclaircissement direct depuis le noir.
          if (nettoyerPixel(o, n)) besoin = (uint8_t)(B_NETTOYER | (n != BLANC ? B_CIBLE : 0));
          else besoin = (o & 0x10) ? B_CIBLE : B_ECLAIRCIR;
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

// Pixel non blanc qui ne change pas (et pas encore marqué : FORCE, INCONNU et les pixels
// rapides diffèrent toujours de la toile, qui va de 0 à 15).
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

// Changement de page en mode pixels (29/10/2026) : TOUT ce qui n'est pas blanc dans la zone
// qui change passe au blanc, puis la nouvelle page est dessinée. Sans ça, les pixels noirs
// communs aux deux pages restaient seuls pendant la transition, un peu partout. Zone : pour
// chaque colonne qui change, de son premier à son dernier pixel changé (analyser()).
// Retourne le nombre de pixels marqués.
static long effacerZoneChangee() {
  long marques = 0;
  for (int x = 0; x < ECRAN_L; x++) {
    if (!s_colBesoin[x]) continue;
    for (int y = s_colY0[x]; y <= s_colY1[x]; y++) {
      const int i = y * ECRAN_L + x;
      if (aEtendre(i)) {
        s_affiche[i] = FORCE;
        marques++;
      }
    }
  }
  return marques;
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
        if (aCorriger(o, s_toile[i])) continue;        // pixel rapide à corriger, même contenu
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
  s_usure = 0;
  snprintf(s_texteMode, sizeof(s_texteMode), "complet");
}

// Ajoute des pixels pilotés à l'usure (inutile de compter au-delà du secours : pas de débordement).
static void ajouterUsure(long pixels, uint32_t poids) {
  if (pixels > 0 && s_usure < USURE_SECOURS) s_usure += (uint32_t)pixels * poids;
}

// Colonnes portrait touchées par le dernier analyser().
static int compterColonnes() {
  int n = 0;
  for (int x = 0; x < ECRAN_L; x++) {
    if (s_colBesoin[x]) n++;
  }
  return n;
}

// Changement de page : beaucoup de pixels, étalés sur une bonne partie de la largeur.
static inline bool grosChangement(long changes, int colonnes) {
  return changes >= GROS_CHANGEMENT_PX && colonnes >= GROS_CHANGEMENT_COLONNES;
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

// Une trame : pousse les pixels du masque pendant duree (0,1 µs, comme epd_push_pixels).
// blanc = false : masque envoyé tel quel (« 01 » = foncer ; l'affichage rapide y mêle des
// « 10 » = éclaircir) ; blanc = true : les « 01 » deviennent « 10 ». Les lignes sans pixel
// dans le masque sont sautées.
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

static inline void noterMasque(int x, int y, uint8_t code);

// Finition du mode doux (29/10/2026). Les transitions directes du mode doux (epd_draw_image,
// 102 µs au plus) ne mènent pas un pixel jusqu'au vrai noir ou au vrai blanc : sans finition,
// il restait des zones grises. Chaque pixel qui vient de changer et dont la valeur voulue est
// noire ou blanche reçoit donc MAJ_FINITION_TRAMES poussées de plus vers cette valeur, sans
// clignoter (il ne va que vers sa cible). Seuls les pixels qui changent sont poussés, et dans
// le sens de leur changement : un pixel qui passe au noir puis plus tard au blanc reçoit
// autant de « noir » que de « blanc ». (L'ancien blanchiment de tout le fond après chaque
// action poussait toujours les mêmes pixels dans le même sens : charge qui s'accumule dans la
// dalle, traces et usure.) Les gris ne sont pas poussés : ils quitteraient leur valeur.
// À appeler après les étapes de dessin, avant copierVersAffiche(). Retourne vrai s'il y en a.
// noirSeulement (mode pixels) : les pixels effacés ont déjà eu leur blanc (MAJ_POUSSEES x 50 µs) ;
// seuls ceux redessinés en noir reçoivent la finition, pour autant de noir que de blanc.
static bool preparerFinition(const Groupe* g, int nb, bool noirSeulement) {
  memset(s_ligneMasque, 0, sizeof(s_ligneMasque));
  bool unPixel = false;
  for (int i = 0; i < nb; i++) {
    for (int y = g[i].y0; y <= g[i].y1; y++) {
      const uint8_t* tl = s_toile + y * ECRAN_L;
      const uint8_t* al = s_affiche + y * ECRAN_L;
      for (int x = g[i].x0; x <= g[i].x1; x++) {
        const uint8_t n = tl[x];
        if (n != NOIR && (n != BLANC || noirSeulement)) continue;
        if (al[x] == n) continue;                        // pas piloté par cette mise à jour
        noterMasque(x, y, n == NOIR ? M_FONCER : M_ECLAIRCIR);
        unPixel = true;
      }
    }
  }
  return unPixel;
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
  // Nettoyage des seuls pixels concernés, sur tout l'écran à la fois (MAJ_POUSSEES x 50 µs).
  // Les autres pixels ne sont pas pilotés.
  if (!MAJ_NETTOYAGE_FORT) {   // secours : ancien nettoyage, plus faible (zones grises)
    for (int c = 0; c < MAJ_CYCLES_PIXELS; c++) {
      passes += etape(g, r, nb, B_NETTOYER, F_NETTOYER, BLACK_ON_WHITE);
      passes += etape(g, r, nb, B_NETTOYER, F_NETTOYER, WHITE_ON_WHITE);
    }
  } else if (preparerMasque(g, r, nb)) {
    for (int c = 0; c < MAJ_CYCLES_PIXELS; c++) {
      // Effacement : directement au blanc (29/10/2026, plus de flash noir avant le blanc). Le
      // passage au noir d'abord reste possible (MAJ_NETTOYAGE_NOIR) si des traces apparaissent.
      if (MAJ_NETTOYAGE_NOIR) {
        for (int k = 0; k < MAJ_POUSSEES; k++) pousserMasque(false, DUREE_CYCLE_US * 10);
        passes++;
      }
      for (int k = 0; k < MAJ_POUSSEES; k++) pousserMasque(true, DUREE_CYCLE_US * 10);
      passes++;
    }
  }
  passes += etape(g, r, nb, B_ECLAIRCIR, F_ECLAIRCIR, WHITE_ON_WHITE);
  passes += etape(g, r, nb, B_ASSOMBRIR, F_ASSOMBRIR, BLACK_ON_WHITE);
  passes += etape(g, r, nb, B_CIBLE, F_CIBLE, BLACK_ON_WHITE);   // = epd_draw_grayscale_image
  // Finition : en doux, vers le noir et le blanc ; en mode pixels, seulement les pixels
  // redessinés en noir (102 µs de noir + la finition = le blanc de l'effacement : équilibré).
  // Avec le passage au noir d'abord, le nettoyage est déjà équilibré : pas de finition.
  const bool finitionPixels = s_mode == MAJ_PIXELS && !MAJ_NETTOYAGE_NOIR;
  if ((s_mode == MAJ_DOUX || finitionPixels) && MAJ_FINITION_TRAMES > 0 &&
      preparerFinition(g, nb, s_mode == MAJ_PIXELS)) {
    for (int k = 0; k < MAJ_FINITION_TRAMES; k++) pousserMasque(false, DUREE_CYCLE_US * 10);   // masque tel quel
    passes++;
  }
  for (int i = 0; i < nb; i++) copierVersAffiche(versPortrait(r[i]));
  return passes;
}

void rafraichir(bool complet) {
  if (!s_toile) return;
  const uint32_t t0 = millis();
  long inconnus = 0;
  long changes = analyser(inconnus);
  if (changes == 0 && !complet) return;

  // Usure : l'image complète attend la mise en veille (nettoyageConseille()). Pendant
  // l'utilisation, seulement en secours, sur un changement de page.
  const bool parUsure = s_completAuto && s_usure >= USURE_SECOURS && grosChangement(changes, compterColonnes());
  const bool faireComplet = complet ||
                            inconnus * 100 > (long)TAILLE_TOILE * PART_INCONNU_COMPLET_PCT ||
                            (s_completTous > 0 && s_partiels >= s_completTous) ||
                            parUsure;
  if (!faireComplet && s_mode == MAJ_PIXELS) {
    long marques = 0;
    if (grosChangement(changes, compterColonnes())) marques += effacerZoneChangee();
    marques += etendreNettoyage();
    if (marques > 0) changes = analyser(inconnus);   // ajoute les colonnes des pixels marqués
  }
  epd_poweron();
  if (faireComplet) {
    dessinerComplet();              // remet aussi l'usure à zéro
    if (parUsure) snprintf(s_texteMode, sizeof(s_texteMode), "complet (usure)");
  } else {
    Groupe groupes[16];
    const int nb = grouper(groupes);
    const int passes = mettreAJourGroupes(groupes, nb);
    if (s_partiels < 0xFFFF) s_partiels++;
    ajouterUsure(changes, s_mode == MAJ_DOUX ? USURE_POIDS_DOUX : USURE_POIDS_PIXELS);
    snprintf(s_texteMode, sizeof(s_texteMode), "%s, %d groupe%s, %d passe%s",
             s_mode == MAJ_DOUX ? "doux" : "pixels", nb, nb > 1 ? "s" : "", passes, passes > 1 ? "s" : "");
  }
  if (s_rappel) s_rappel();
  epd_poweroff_all();   // coupe aussi PWR_EN (LED bleue, pont batterie)
  s_nb++;
  s_duree = millis() - t0;
}

// ---------------------------------------------------------------------------
//  Affichage rapide et retour au toucher (28/10/2026)
//  Une seule série de RAPIDE_TRAMES trames, avec un masque où se mêlent des « 01 » (pixels à
//  foncer) et des « 10 » (pixels à blanchir) : chaque pixel va directement au noir ou au blanc,
//  sans nettoyage ni passes de gris. s_affiche note le noir ou le blanc réel avec la valeur
//  voulue (A_NOIR | v, A_BLANC | v) : le rafraichir() normal suivant corrige ces pixels sans
//  nettoyage (dessin depuis le blanc, ou éclaircissement direct depuis le noir) et sans
//  etendreNettoyage(), donc sans faire clignoter les lettres tapées ni les touches appuyées.
// ---------------------------------------------------------------------------

// Valeur affichée par le chemin rapide : 0 à 7 -> noir, 8 à 15 -> blanc.
static inline uint8_t arrondi(uint8_t v) { return v < 8 ? NOIR : BLANC; }

// Note le pixel portrait (x, y) dans le masque : M_FONCER ou M_ECLAIRCIR. Une ligne native est
// remise à zéro la première fois qu'on y écrit (s_ligneMasque doit être vidé avant).
static inline void noterMasque(int x, int y, uint8_t code) {
  int nx, ny;
  if (s_orient == 1) {
    nx = NATIF_L - 1 - y;
    ny = x;
  } else {
    nx = y;
    ny = NATIF_H - 1 - x;
  }
  uint8_t* ligne = s_masque + ny * OCTETS_LIGNE;
  if (!s_ligneMasque[ny]) {
    memset(ligne, 0, OCTETS_LIGNE);
    s_ligneMasque[ny] = true;
  }
  ligne[nx >> 2] |= (uint8_t)(code << ((nx & 3) * 2));   // même ordre que preparerMasque()
}

void rafraichirRapide() {
  if (!s_toile) return;
  const uint32_t t0 = millis();
  memset(s_ligneMasque, 0, sizeof(s_ligneMasque));
  memset(s_colBesoin, 0, sizeof(s_colBesoin));
  const long inconnusMax = (long)TAILLE_TOILE * RAPIDE_INCONNU_MAX_PCT / 100;
  long changes = 0, inconnus = 0, pousses = 0;
  int x0 = ECRAN_L, x1 = -1, y0 = -1, y1 = -1;   // rectangle des pixels changés
  for (int y = 0; y < ECRAN_H && inconnus <= inconnusMax; y++) {
    const uint32_t* t = (const uint32_t*)(s_toile + y * ECRAN_L);
    const uint32_t* a = (const uint32_t*)(s_affiche + y * ECRAN_L);
    const uint8_t* tl = s_toile + y * ECRAN_L;
    const uint8_t* al = s_affiche + y * ECRAN_L;
    for (int w = 0; w < ECRAN_L / 4; w++) {
      if (t[w] == a[w]) continue;
      for (int x = w * 4; x < w * 4 + 4; x++) {
        const uint8_t n = tl[x], o = al[x];
        if (n == o) continue;
        const uint8_t p = physique(o);
        const uint8_t cible = arrondi(n);
        if (marque(o) && attendu(o) == n && p == cible) continue;   // déjà fait, attend sa correction
        changes++;
        s_colBesoin[x] = 1;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y0 < 0) y0 = y;
        y1 = y;
        if (o == INCONNU || o == FORCE) inconnus++;   // poussé quand même, mais restera inconnu
        if (p == cible) continue;                     // déjà au noir ou au blanc voulu
        noterMasque(x, y, cible == NOIR ? M_FONCER : M_ECLAIRCIR);
        pousses++;
      }
    }
  }
  if (changes == 0) return;
  // Écran en bonne partie inconnu, ou changement de page : ce n'est plus une frappe.
  if (inconnus > inconnusMax || grosChangement(changes, compterColonnes())) {
    rafraichir();
    return;
  }
  if (pousses > 0) {
    epd_poweron();
    for (int k = 0; k < RAPIDE_TRAMES; k++) pousserMasque(false, RAPIDE_DUREE);
    if (s_rappel) s_rappel();
    epd_poweroff_all();
  }
  // La dalle montre maintenant les valeurs arrondies : on les note avec la valeur voulue. Les
  // pixels inconnus le restent : le rafraichir() suivant les nettoiera.
  for (int y = y0; y <= y1; y++) {
    uint8_t* al = s_affiche + y * ECRAN_L;
    const uint8_t* tl = s_toile + y * ECRAN_L;
    for (int x = x0; x <= x1; x++) {
      const uint8_t o = al[x];
      if (o != tl[x] && o != INCONNU && o != FORCE) al[x] = etatRapide(arrondi(tl[x]), tl[x]);
    }
  }
  ajouterUsure(pousses, USURE_POIDS_RAPIDE);
  s_nb++;
  s_duree = millis() - t0;
  snprintf(s_texteMode, sizeof(s_texteMode), "rapide, %ld pixel%s", pousses, pousses > 1 ? "s" : "");
}

// Inverse les pixels xa à xb - 1 de la ligne portrait y (noir <-> blanc, gris arrondis).
// Chaque pixel garde sa valeur d'avant comme valeur voulue : à la remise d'aplomb, un pixel
// passé au blanc est redessiné depuis le blanc, un fond blanc passé au noir est nettoyé
// (écart >= ECART_DIRECT_MAX), dans les deux modes : vrai blanc, jamais de rectangle gris.
// Retourne le nombre de pixels notés dans le masque.
static long inverserSegment(int y, int xa, int xb) {
  uint8_t* al = s_affiche + y * ECRAN_L;
  const uint8_t* tl = s_toile + y * ECRAN_L;
  for (int x = xa; x < xb; x++) {
    const uint8_t o = al[x];
    const bool connu = (o != INCONNU && o != FORCE);
    const uint8_t vu = connu ? physique(o) : tl[x];   // ce que montre la dalle (inconnu : la toile)
    const uint8_t cible = vu < 8 ? BLANC : NOIR;
    noterMasque(x, y, cible == NOIR ? M_FONCER : M_ECLAIRCIR);
    if (connu) al[x] = etatRapide(cible, marque(o) ? attendu(o) : o);
  }
  return xb > xa ? xb - xa : 0;
}

// s_toile n'est pas touchée : la page redessinée ensuite (rafraichir ou rafraichirRapide) voit
// la zone inversée dans s_affiche et la remet d'aplomb. Pas de mesure de batterie ici (s_rappel) :
// c'est le chemin le plus court possible, et l'affichage qui suit s'en charge.
void inverserZone(const Boite& b) {
  if (!s_affiche) return;
  const int x0 = maxi(0, b.x), x1 = mini(ECRAN_L, b.x + b.l);
  const int y0 = maxi(0, b.y), y1 = mini(ECRAN_H, b.y + b.h);
  if (x1 <= x0 || y1 <= y0) return;
  const bool cadre = (long)(x1 - x0) * (y1 - y0) > INVERSION_MAX_PX;
  const int e = INVERSION_CADRE_PX;
  memset(s_ligneMasque, 0, sizeof(s_ligneMasque));
  long pousses = 0;
  for (int y = y0; y < y1; y++) {
    if (!cadre || y < y0 + e || y >= y1 - e) {
      pousses += inverserSegment(y, x0, x1);
    } else {   // cadre : seulement les bords gauche et droit sur ces lignes
      pousses += inverserSegment(y, x0, mini(x0 + e, x1));
      pousses += inverserSegment(y, maxi(x0 + e, x1 - e), x1);
    }
  }
  epd_poweron();
  for (int k = 0; k < RAPIDE_TRAMES; k++) pousserMasque(false, RAPIDE_DUREE);
  epd_poweroff_all();
  ajouterUsure(pousses, USURE_POIDS_PIXELS);
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

bool nettoyageConseille() { return s_completAuto && s_usure >= USURE_SEUIL; }

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
void definirCompletAuto(bool actif) { s_completAuto = actif; }   // l'usure est comptée même coupée
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
