#include "energie.h"
#include "ecran.h"
#include "tactile.h"
#include "horloge.h"
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>

namespace Energie {

static int s_mv = 0;
static bool s_valide = false;
static uint32_t s_tMesure = 0;
static uint32_t s_tActivite = 0;
static esp_sleep_wakeup_cause_t s_cause = ESP_SLEEP_WAKEUP_UNDEFINED;

// Courbe de décharge typique d'une LiPo 1 cellule à faible courant (mV -> %).
struct PointCourbe {
  int mv;
  int pct;
};
static const PointCourbe COURBE[] = {
  {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
  {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
  {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3500, 2},
  {3350, 0},
};

// Charge en pour mille selon la courbe (pourcentage() = pourMilleDe() / 10, même arrondi).
static int pourMilleDe(int mv) {
  const int n = sizeof(COURBE) / sizeof(COURBE[0]);
  if (mv >= COURBE[0].mv) return 1000;
  if (mv <= COURBE[n - 1].mv) return 0;
  for (int i = 1; i < n; i++) {
    if (mv >= COURBE[i].mv) {
      const PointCourbe& h = COURBE[i - 1];
      const PointCourbe& b = COURBE[i];
      return b.pct * 10 + (mv - b.mv) * (h.pct - b.pct) * 10 / (h.mv - b.mv);
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  Autonomie estimée (28/10/2026)
//  Pendant la décharge (sur batterie seulement), un échantillon (heure UTC, charge en pour
//  mille) toutes les 10 min au plus, gardé en mémoire RTC pour survivre au sommeil profond.
//  Quand les 32 places sont prises, on garde un échantillon sur deux et l'écart double :
//  la fenêtre couvre toute la décharge depuis la dernière charge. La pente est calculée
//  par les moindres carrés (la tension varie de quelques % d'une mesure à l'autre selon
//  l'activité de la dalle) ; l'heure doit être valide (RTC, NTP ou réglage manuel).
// ---------------------------------------------------------------------------
struct Echantillon {
  uint32_t utc;
  uint16_t pm;      // charge en pour mille
};
static const int ECH_MAX = 32;
static const int ECH_MIN = 4;
static const uint32_t ECH_PAS_S = 600;               // 10 min entre deux échantillons au départ
static const uint32_t RECUL_MIN_S = 2UL * 3600UL;    // estimation après 2 h de décharge…
static const int BAISSE_MIN_PM = 30;                 // … et 3 % de baisse
static const int REMONTEE_PM = 80;                   // +8 % : rechargée sans qu'on l'ait vu
static const uint32_t ATTENTE_APRES_CHARGE_S = 1800; // la tension retombe après la charge

static RTC_DATA_ATTR Echantillon r_ech[ECH_MAX];
static RTC_DATA_ATTR uint8_t r_nbEch = 0;
static RTC_DATA_ATTR uint32_t r_pasS = ECH_PAS_S;
static RTC_DATA_ATTR uint32_t r_finCharge = 0;       // UTC de la dernière mesure en charge

// Appelé après chaque mesure réussie.
static void noterEchantillon() {
  if (!Horloge::valide()) return;
  const uint32_t t = Horloge::maintenant();
  if (enCharge()) {   // en charge ou sur l'ordinateur : la décharge repartira de zéro
    r_nbEch = 0;
    r_finCharge = t;
    return;
  }
  const int pm = pourMilleDe(s_mv);
  if (r_nbEch > 0) {
    // Heure revenue en arrière (réglage), ou nette remontée au-dessus du plus bas
    // (chargeur non détecté, même lent) : on repart de zéro.
    int plusBas = r_ech[0].pm;
    for (int i = 1; i < r_nbEch; i++) plusBas = mini(plusBas, r_ech[i].pm);
    if (t < r_ech[r_nbEch - 1].utc || pm > plusBas + REMONTEE_PM) r_nbEch = 0;
  }
  // Batterie encore pleine (ou alimentée par un chargeur), ou tension qui retombe juste
  // après la charge : la décharge à suivre n'a pas encore commencé.
  if (s_mv >= BATTERIE_PLEINE_MV || (r_finCharge != 0 && t - r_finCharge < ATTENTE_APRES_CHARGE_S)) {
    r_nbEch = 0;
    return;
  }
  if (r_nbEch == 0) r_pasS = ECH_PAS_S;
  else if (t - r_ech[r_nbEch - 1].utc < r_pasS) return;
  if (r_nbEch >= ECH_MAX) {   // plein : un échantillon sur deux, écart doublé
    for (int i = 1; i < ECH_MAX / 2; i++) r_ech[i] = r_ech[2 * i];
    r_nbEch = ECH_MAX / 2;
    r_pasS *= 2;
  }
  r_ech[r_nbEch].utc = t;
  r_ech[r_nbEch].pm = (uint16_t)pm;
  r_nbEch++;
}

void demarrer() {
  s_cause = esp_sleep_get_wakeup_cause();
  analogSetPinAttenuation(BROCHE_BATTERIE, ADC_11db);   // ~0..3,1 V : batterie/2 <= 2,1 V
  s_tActivite = millis();
}

void mesurer() {
  if (WiFi.getMode() != WIFI_OFF) return;   // ADC2 réservé au Wi-Fi quand la radio tourne
  uint32_t somme = 0;
  int n = 0;
  for (int i = 0; i < 8; i++) {
    const uint32_t v = analogReadMilliVolts(BROCHE_BATTERIE);
    if (v > 0) {
      somme += v;
      n++;
    }
  }
  if (n == 0) return;
  const int mv = (int)((float)somme / n * BATTERIE_RAPPORT) + BATTERIE_CORRECTION_MV;
  if (mv < 2500 || mv > 4700) return;   // lecture absurde : on garde l'ancienne
  if (!s_valide || abs(mv - s_mv) > 150) s_mv = mv;   // premier point ou saut (branchement)
  else s_mv = (s_mv * 3 + mv) / 4;                      // lissage
  s_valide = true;
  s_tMesure = millis();
  noterEchantillon();
}

void mesurerMaintenant() {
  if (WiFi.getMode() != WIFI_OFF) return;
  Ecran::executerAlimente(mesurer);
}

void boucle() {
  // Mesure toutes les 2 min (ou toutes les 10 s tant qu'aucune mesure n'a réussi).
  // Les rafraîchissements d'écran mesurent aussi au passage (rappel d'Ecran).
  const uint32_t intervalle = s_valide ? MESURE_BATTERIE_MS : 10000UL;
  if (millis() - s_tMesure >= intervalle) {
    s_tMesure = millis();   // évite de réessayer à chaque tour si le Wi-Fi bloque la mesure
    mesurerMaintenant();
  }
}

int millivolts() { return s_valide ? s_mv : 0; }
bool mesureValide() { return s_valide; }

int pourcentage() { return s_valide ? pourMilleDe(s_mv) / 10 : 0; }

bool texteAutonomie(char* sortie, size_t taille) {
  if (!sortie || taille == 0) return false;
  sortie[0] = 0;
  if (!s_valide || enCharge() || !Horloge::valide() || r_nbEch < ECH_MIN) return false;
  const uint32_t t0 = r_ech[0].utc;
  const uint32_t recul = r_ech[r_nbEch - 1].utc - t0;
  if (recul < RECUL_MIN_S) return false;
  // Droite des moindres carrés : charge (pour mille) en fonction du temps (heures depuis t0)
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < r_nbEch; i++) {
    const double x = (r_ech[i].utc - t0) / 3600.0;
    const double y = r_ech[i].pm;
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
  }
  const double d = r_nbEch * sxx - sx * sx;
  if (d <= 0) return false;
  const double pente = (r_nbEch * sxy - sx * sy) / d;     // pour mille par heure (< 0)
  const double origine = (sy - pente * sx) / r_nbEch;
  if (pente >= 0 || -pente * (recul / 3600.0) < BAISSE_MIN_PM) return false;
  // Charge restante maintenant selon la tendance, puis heures avant 0 %.
  const uint32_t m = Horloge::maintenant();
  double reste = origine + pente * ((m > t0 ? m - t0 : 0) / 3600.0);
  if (reste < 0) reste = 0;
  const double heures = reste / -pente;
  if (heures < 1.0) {
    snprintf(sortie, taille, "moins d'une heure");
  } else if (heures < 36.0) {
    snprintf(sortie, taille, "environ %d h", (int)(heures + 0.5));
  } else if (heures < 14.0 * 24.0) {
    snprintf(sortie, taille, "environ %d jours", (int)(heures / 24.0 + 0.5));
  } else if (heures < 60.0 * 24.0) {
    snprintf(sortie, taille, "environ %d semaines", (int)(heures / (7.0 * 24.0) + 0.5));
  } else {
    snprintf(sortie, taille, "plus de 2 mois");
  }
  return true;
}

bool usbOrdinateur() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

bool enCharge() { return usbOrdinateur() || (s_valide && s_mv >= BATTERIE_CHARGE_MV); }
bool faible() { return s_valide && s_mv < BATTERIE_FAIBLE_MV && !enCharge(); }
bool vide() { return s_valide && s_mv < BATTERIE_VIDE_MV && !enCharge(); }

void activite() { s_tActivite = millis(); }
uint32_t inactifDepuisMs() { return millis() - s_tActivite; }

void veilleLegere(uint32_t dureeMaxMs) {
  esp_sleep_enable_timer_wakeup((uint64_t)dureeMaxMs * 1000ULL);
  // Réveil par un bouton (niveau bas) ou par la broche INT du tactile.
  gpio_wakeup_enable((gpio_num_t)BROCHE_MARCHE, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BROCHE_VOL_PLUS, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BROCHE_VOL_MOINS, GPIO_INTR_LOW_LEVEL);
  if (Tactile::present()) {
    const bool haut = Tactile::intActifHaut();
    // Si INT est déjà au niveau « donnée », on ne s'en sert pas (sinon réveil immédiat sans fin).
    if ((digitalRead(BROCHE_TACTILE_INT) == HIGH) != haut) {
      gpio_wakeup_enable((gpio_num_t)BROCHE_TACTILE_INT, haut ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    } else {
      gpio_wakeup_disable((gpio_num_t)BROCHE_TACTILE_INT);
    }
  }
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
}

void sommeilProfond(uint32_t dansSecondes) {
  Tactile::dormir();
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Wire.end();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // GPIO21 a son propre pull-up 100 k (R8) : il reste haut pendant le sommeil.
  esp_sleep_enable_ext1_wakeup(1ULL << BROCHE_MARCHE, ESP_EXT1_WAKEUP_ANY_LOW);
  if (dansSecondes > 0) esp_sleep_enable_timer_wakeup((uint64_t)dansSecondes * 1000000ULL);
  JOURNAL("[energie] sommeil profond%s", dansSecondes ? " (réveil programmé)" : "");
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
}

esp_sleep_wakeup_cause_t causeReveil() { return s_cause; }

const char* texteCauseReveil() {
  switch (s_cause) {
    case ESP_SLEEP_WAKEUP_EXT1:  return "bouton marche";
    case ESP_SLEEP_WAKEUP_TIMER: return "minuterie";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "mise sous tension / reset";
    default: return "autre";
  }
}

}  // namespace Energie
