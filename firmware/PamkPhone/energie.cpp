#include "energie.h"
#include "ecran.h"
#include "tactile.h"
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

int pourcentage() {
  if (!s_valide) return 0;
  const int n = sizeof(COURBE) / sizeof(COURBE[0]);
  if (s_mv >= COURBE[0].mv) return 100;
  if (s_mv <= COURBE[n - 1].mv) return 0;
  for (int i = 1; i < n; i++) {
    if (s_mv >= COURBE[i].mv) {
      const PointCourbe& h = COURBE[i - 1];
      const PointCourbe& b = COURBE[i];
      return b.pct + (s_mv - b.mv) * (h.pct - b.pct) / (h.mv - b.mv);
    }
  }
  return 0;
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
