#include "tactile.h"
#include "ecran.h"
#include <Wire.h>

namespace Tactile {

// Registres GT911 (mêmes adresses que SensorLib/REG/GT911Constants.h)
static const uint16_t REG_COMMANDE = 0x8040;
static const uint16_t REG_MODE_INT = 0x804D;
static const uint16_t REG_PRODUIT  = 0x8140;
static const uint16_t REG_ETAT     = 0x814E;
static const uint16_t REG_POINT1   = 0x814F;

static uint8_t s_adr = 0;
static bool s_present = false;
static bool s_intHaut = true;
static bool s_debug = false;

static bool s_pose = false;        // doigt posé (suivi)
static bool s_ignorer = false;     // contact à ignorer jusqu'au relâchement
static bool s_longEmis = false;
static int s_x0 = 0, s_y0 = 0, s_x = 0, s_y = 0;
static uint32_t s_t0 = 0, s_tDonnee = 0, s_tLecture = 0;

static bool ecrire8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(s_adr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool lire(uint16_t reg, uint8_t* buf, int n) {
  Wire.beginTransmission(s_adr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)s_adr, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool repond(uint8_t adr) {
  Wire.beginTransmission(adr);
  return Wire.endTransmission() == 0;
}

bool demarrer(bool apresSommeil) {
  // Comme les exemples LilyGO : INT à l'état haut réveille un GT911 endormi.
  pinMode(BROCHE_TACTILE_INT, OUTPUT);
  digitalWrite(BROCHE_TACTILE_INT, HIGH);
  delay(5);
  pinMode(BROCHE_TACTILE_INT, INPUT);
  // LilyGO : « après un réveil, attendre ~1 s avant que le tactile réponde ».
  const uint32_t limite = millis() + (apresSommeil ? 1500 : 400);
  s_adr = 0;
  do {
    if (repond(0x5D)) s_adr = 0x5D;
    else if (repond(0x14)) s_adr = 0x14;
    if (!s_adr) delay(50);
  } while (!s_adr && (int32_t)(limite - millis()) > 0);
  s_present = s_adr != 0;
  if (!s_present) {
    JOURNAL("[tactile] GT911 introuvable (0x5D / 0x14)");
    return false;
  }
  uint8_t id[4] = {0, 0, 0, 0};
  lire(REG_PRODUIT, id, 4);
  uint8_t mode = 0;
  lire(REG_MODE_INT, &mode, 1);
  // 0 = front montant, 1 = front descendant, 2 = niveau bas, 3 = niveau haut
  s_intHaut = ((mode & 0x03) == 0 || (mode & 0x03) == 3);
  ecrire8(REG_ETAT, 0);
  JOURNAL("[tactile] GT911 en 0x%02X, produit « %c%c%c », INT mode %u", s_adr,
          id[0] ? id[0] : '?', id[1] ? id[1] : '?', id[2] ? id[2] : '?', (unsigned)(mode & 0x03));
  return true;
}

bool present() { return s_present; }
uint8_t adresse() { return s_adr; }
bool doigtPose() { return s_pose; }
bool intActifHaut() { return s_intHaut; }
void definirDebug(bool actif) { s_debug = actif; }
bool debug() { return s_debug; }

// Coordonnées brutes -> natives 960x540 (réglages LilyGO) -> portrait.
static void convertir(int rx, int ry, int& px, int& py) {
  int nx = rx, ny = ry;
  if (TACTILE_ECHANGER_XY) { nx = ry; ny = rx; }
  if (TACTILE_MIROIR_X) nx = NATIF_L - nx;
  if (TACTILE_MIROIR_Y) ny = NATIF_H - ny;
  nx = borner(nx, 0, NATIF_L - 1);
  ny = borner(ny, 0, NATIF_H - 1);
  if (Ecran::orientation() == 1) {
    px = ny;
    py = NATIF_L - 1 - nx;
  } else {
    px = NATIF_H - 1 - ny;
    py = nx;
  }
}

// Lit l'état. Retourne true si une nouvelle trame est arrivée ; doigt/x/y mis à jour.
static bool lireTrame(bool& doigt, int& x, int& y) {
  uint8_t etat = 0;
  if (!lire(REG_ETAT, &etat, 1)) return false;
  if (!(etat & 0x80)) return false;            // pas de nouvelle trame
  const int n = etat & 0x0F;
  doigt = false;
  if (n > 0 && n <= 5) {
    uint8_t b[7];
    if (lire(REG_POINT1, b, 7)) {
      const int rx = b[1] | (b[2] << 8);
      const int ry = b[3] | (b[4] << 8);
      convertir(rx, ry, x, y);
      doigt = true;
      if (s_debug) JOURNAL("[tactile] brut %d,%d -> portrait %d,%d (%d doigt(s))", rx, ry, x, y, n);
    }
  }
  ecrire8(REG_ETAT, 0);
  return true;
}

Geste suivre() {
  Geste g;
  g.type = GESTE_AUCUN;
  g.x = g.y = g.dx = g.dy = 0;
  if (!s_present) return g;
  const uint32_t m = millis();
  if (m - s_tLecture < (uint32_t)TACTILE_PERIODE_MS) return g;
  s_tLecture = m;

  bool doigt = s_pose;
  int x = s_x, y = s_y;
  if (lireTrame(doigt, x, y)) {
    s_tDonnee = m;
  } else if (s_pose && m - s_tDonnee > 160) {
    doigt = false;   // plus de trame depuis un moment : le doigt est parti
  }

  if (!s_pose && doigt) {                       // contact
    s_pose = true;
    s_longEmis = false;
    s_x0 = s_x = x;
    s_y0 = s_y = y;
    s_t0 = m;
  } else if (s_pose && doigt) {                 // maintien
    s_x = x;
    s_y = y;
    if (!s_ignorer && !s_longEmis && m - s_t0 >= (uint32_t)TACTILE_APPUI_LONG_MS &&
        abs(s_x - s_x0) < TACTILE_TOLERANCE_PX && abs(s_y - s_y0) < TACTILE_TOLERANCE_PX) {
      s_longEmis = true;
      g.type = GESTE_APPUI_LONG;
      g.x = s_x0;
      g.y = s_y0;
    }
  } else if (s_pose && !doigt) {                // relâchement
    s_pose = false;
    if (s_ignorer) {
      s_ignorer = false;
    } else if (!s_longEmis) {
      const int dx = s_x - s_x0, dy = s_y - s_y0;
      if (abs(dx) >= TACTILE_GLISSER_PX || abs(dy) >= TACTILE_GLISSER_PX) {
        g.type = GESTE_GLISSER;
      } else if (abs(dx) < TACTILE_TOLERANCE_PX && abs(dy) < TACTILE_TOLERANCE_PX) {
        g.type = GESTE_TOUCHER;
      }
      g.x = s_x0;
      g.y = s_y0;
      g.dx = dx;
      g.dy = dy;
    }
  }
  return g;
}

void purger() {
  if (!s_present) return;
  ecrire8(REG_ETAT, 0);            // oublie la trame lue pendant le rafraîchissement (cf. LilyGO #143)
  if (s_pose) s_ignorer = true;    // un doigt resté posé ne déclenche rien
  s_tDonnee = millis();
}

// Le GT911 garde le drapeau « nouvelle trame » (0x814E, bit 7) jusqu'à ce qu'on l'efface :
// même un toucher très bref entre deux lectures est vu. Si le doigt est encore posé, ce
// contact ne fait que réveiller : il est ignoré jusqu'au relâchement.
bool reveilParContact() {
  if (!s_present) return false;
  uint8_t etat = 0;
  if (!lire(REG_ETAT, &etat, 1) || !(etat & 0x80)) return false;
  ecrire8(REG_ETAT, 0);
  const int n = etat & 0x0F;
  if (n > 0 && n <= 5) {
    s_pose = true;
    s_ignorer = true;
    s_longEmis = false;
    s_t0 = s_tDonnee = millis();
  }
  return true;
}

void dormir() {
  if (!s_present) return;
  // Séquence du GT911 (SensorLib TouchDrvGT911::sleep) : INT bas puis commande 0x05.
  pinMode(BROCHE_TACTILE_INT, OUTPUT);
  digitalWrite(BROCHE_TACTILE_INT, LOW);
  delay(2);
  ecrire8(REG_COMMANDE, 0x05);
  delay(5);
}

}  // namespace Tactile
