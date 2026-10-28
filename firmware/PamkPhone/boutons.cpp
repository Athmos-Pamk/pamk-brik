#include "boutons.h"
#include <driver/rtc_io.h>

namespace Boutons {

struct Etat {
  int broche;
  bool brut;          // dernière lecture
  bool stable;        // après anti-rebond (true = appuyé)
  bool longEmis;
  bool ignorer;       // ne rien émettre jusqu'au relâchement
  uint32_t tChangement;
  uint32_t tAppui;
};

static Etat s_b[3];
static bool s_inverser = false;
static bool s_debug = false;
static bool s_duoEmis = false;

static EvenementBouton s_file[8];
static uint8_t s_tete = 0, s_queue = 0;

static void pousser(int index, TypeAppui t) {
  Bouton b = (Bouton)index;
  if (s_inverser && b != BOUTON_MARCHE) b = (b == BOUTON_PLUS) ? BOUTON_MOINS : BOUTON_PLUS;
  const uint8_t suivant = (uint8_t)((s_queue + 1) % 8);
  if (suivant == s_tete) return;   // file pleine : on perd l'événement
  s_file[s_queue].bouton = b;
  s_file[s_queue].type = t;
  s_queue = suivant;
  if (s_debug) JOURNAL("[boutons] %s %s", nom(b), t == APPUI_COURT ? "court" : (t == APPUI_LONG ? "long" : "duo"));
}

void demarrer() {
  const int broches[3] = {BROCHE_MARCHE, BROCHE_VOL_PLUS, BROCHE_VOL_MOINS};
  // Après un réveil ext1, GPIO21 est encore routée vers le domaine RTC : on la rend au GPIO normal.
  if (rtc_gpio_is_valid_gpio((gpio_num_t)BROCHE_MARCHE)) rtc_gpio_deinit((gpio_num_t)BROCHE_MARCHE);
  for (int i = 0; i < 3; i++) {
    // GPIO21 a déjà un pull-up 100 k + 4,7 µF sur la carte : le pull-up interne
    // en parallèle raccourcit la remontée au relâchement (~0,2 s au lieu de ~0,6 s).
    pinMode(broches[i], INPUT_PULLUP);
  }
  delay(2);
  const uint32_t m = millis();
  for (int i = 0; i < 3; i++) {
    Etat& e = s_b[i];
    e.broche = broches[i];
    e.brut = digitalRead(e.broche) == LOW;
    e.stable = e.brut;
    e.longEmis = false;
    e.ignorer = e.brut;   // enfoncé au démarrage (bouton qui a servi au réveil)
    e.tChangement = m;
    e.tAppui = m;
  }
  s_duoEmis = false;
}

void scruter() {
  const uint32_t m = millis();
  for (int i = 0; i < 3; i++) {
    Etat& e = s_b[i];
    const bool r = digitalRead(e.broche) == LOW;
    if (r != e.brut) {
      e.brut = r;
      e.tChangement = m;
    }
    if (r != e.stable && m - e.tChangement >= (uint32_t)BOUTON_ANTIREBOND_MS) {
      e.stable = r;
      if (r) {
        e.tAppui = m;
        e.longEmis = false;
      } else {
        if (e.ignorer) e.ignorer = false;
        else if (!e.longEmis) pousser(i, APPUI_COURT);
      }
    }
    const uint32_t seuil = (i == 0) ? (uint32_t)BOUTON_MARCHE_LONG_MS : (uint32_t)BOUTON_LONG_MS;
    if (e.stable && !e.ignorer && !e.longEmis && m - e.tAppui >= seuil) {
      e.longEmis = true;
      pousser(i, APPUI_LONG);
    }
  }
  // Volume + et volume − ensemble
  Etat& p = s_b[1];
  Etat& n = s_b[2];
  if (p.stable && n.stable) {
    const uint32_t ecart = p.tAppui > n.tAppui ? p.tAppui - n.tAppui : n.tAppui - p.tAppui;
    if (!s_duoEmis && ecart <= (uint32_t)BOUTON_DUO_MS && !p.longEmis && !n.longEmis) {
      s_duoEmis = true;
      p.ignorer = true;
      n.ignorer = true;
      pousser(1, APPUI_DUO);
    }
  } else if (!p.stable && !n.stable) {
    s_duoEmis = false;
  }
}

bool lire(EvenementBouton& e) {
  if (s_tete == s_queue) return false;
  e = s_file[s_tete];
  s_tete = (uint8_t)((s_tete + 1) % 8);
  return true;
}

bool unAppuye() { return s_b[0].brut || s_b[1].brut || s_b[2].brut; }
bool marcheAppuye() { return digitalRead(BROCHE_MARCHE) == LOW; }
void inverserVolume(bool inverser) { s_inverser = inverser; }
void definirDebug(bool actif) { s_debug = actif; }

const char* nom(Bouton b) {
  switch (b) {
    case BOUTON_MARCHE: return "marche";
    case BOUTON_PLUS:   return "volume +";
    case BOUTON_MOINS:  return "volume -";
  }
  return "?";
}

}  // namespace Boutons
