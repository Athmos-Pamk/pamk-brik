// Statistiques de la semaine : un enregistrement par jour (date locale AAAAMMJJ), 35 jours
// glissants, dans stats.json sur FFat. Le fichier est réécrit à chaque événement (fin, passage
// ou arrêt d'une phase de travail, tâche cochée) : quelques écritures par jour, la flash ne
// s'en aperçoit pas. Tant que l'heure est inconnue (Horloge::valide() faux), rien n'est compté.
// Sans FFat monté, tout marche quand même, en mémoire seulement (perdu au redémarrage).
#include "stats.h"
#include "horloge.h"
#include "stockage.h"
#include <FFat.h>
#include <ArduinoJson.h>

namespace {

const int JOURS_MAX = 35;
const int MINUTES_JOUR_MAX = 24 * 60;
const char* const F_STATS = "/stats.json";
const char* const F_STATS_TMP = "/stats.tmp";
const char* const F_STATS_ABIME = "/stats.json.abime";

JourStats s_jours[JOURS_MAX];   // du plus ancien au plus récent
int s_nb = 0;
bool s_pret = false;            // demarrer() est passé (sinon on ne compte rien)

JourStats* trouver(int date) {
  for (int i = 0; i < s_nb; i++) if (s_jours[i].date == date) return &s_jours[i];
  return nullptr;
}

// Le jour à cette date, créé à sa place s'il manque. Tableau plein : le plus ancien est oublié.
JourStats* trouverOuCreer(int date) {
  JourStats* j = trouver(date);
  if (j) return j;
  if (s_nb >= JOURS_MAX) {
    if (date < s_jours[0].date) return nullptr;   // plus vieux que tout ce qu'on garde
    memmove(&s_jours[0], &s_jours[1], sizeof(JourStats) * (JOURS_MAX - 1));
    s_nb = JOURS_MAX - 1;
  }
  int i = s_nb;
  while (i > 0 && s_jours[i - 1].date > date) {   // insertion triée
    s_jours[i] = s_jours[i - 1];
    i--;
  }
  memset(&s_jours[i], 0, sizeof(JourStats));
  s_jours[i].date = date;
  s_nb++;
  return &s_jours[i];
}

// Oublie les jours sortis de la fenêtre des 35 derniers jours.
void oublierAnciens(int auj) {
  const int limite = Horloge::ajouterJours(auj, -(JOURS_MAX - 1));
  int k = 0;
  while (k < s_nb && s_jours[k].date < limite) k++;
  if (k == 0) return;
  memmove(&s_jours[0], &s_jours[k], sizeof(JourStats) * (s_nb - k));
  s_nb -= k;
}

// Aujourd'hui (créé si besoin), ou nullptr si l'heure n'est pas connue.
JourStats* jourDuJour() {
  if (!s_pret || !Horloge::valide()) return nullptr;
  const int auj = Horloge::aujourdhui();
  if (!Horloge::dateValide(auj)) return nullptr;
  oublierAnciens(auj);
  return trouverOuCreer(auj);
}

// Même principe que Stockage : fichier temporaire, puis renommage.
bool sauver() {
  if (!Stockage::monte()) return false;
  JsonDocument doc;
  doc["version"] = 1;
  doc["type"] = "pamk-stats";
  JsonArray a = doc["jours"].to<JsonArray>();
  for (int i = 0; i < s_nb; i++) {
    const JourStats& j = s_jours[i];
    if (!j.minutes && !j.pomodoros && !j.taches) continue;   // jour vide : rien à garder
    JsonObject o = a.add<JsonObject>();
    char iso[12];
    Horloge::texteISO(iso, sizeof(iso), j.date);
    o["date"] = (const char*)iso;
    o["minutes"] = (int)j.minutes;
    o["pomodoros"] = (int)j.pomodoros;
    o["taches"] = (int)j.taches;
  }
  if (doc.overflowed()) {
    JOURNAL("[stats] mémoire insuffisante");
    return false;
  }
  File f = FFat.open(F_STATS_TMP, "w");
  if (!f) {
    JOURNAL("[stats] impossible d'écrire %s", F_STATS_TMP);
    return false;
  }
  const size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) {
    FFat.remove(F_STATS_TMP);
    return false;
  }
  if (FFat.exists(F_STATS)) FFat.remove(F_STATS);
  if (!FFat.rename(F_STATS_TMP, F_STATS)) {
    JOURNAL("[stats] renommage %s -> %s raté", F_STATS_TMP, F_STATS);
    return false;
  }
  return true;
}

void charger() {
  // Une écriture interrompue entre « supprimer » et « renommer » laisse le .tmp complet.
  if (!FFat.exists(F_STATS) && FFat.exists(F_STATS_TMP)) FFat.rename(F_STATS_TMP, F_STATS);
  if (!FFat.exists(F_STATS)) return;
  File f = FFat.open(F_STATS, "r");
  if (!f) return;
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    JOURNAL("[stats] stats.json : %s, copie gardée dans %s", e.c_str(), F_STATS_ABIME);
    if (FFat.exists(F_STATS_ABIME)) FFat.remove(F_STATS_ABIME);
    FFat.rename(F_STATS, F_STATS_ABIME);
    return;
  }
  for (JsonObject o : doc["jours"].as<JsonArray>()) {
    const int date = Horloge::dateDepuisISO(o["date"] | "");
    if (!date) continue;
    JourStats* j = trouverOuCreer(date);
    if (!j) continue;
    j->minutes = (uint16_t)borner((int)j->minutes + (int)(o["minutes"] | 0), 0, MINUTES_JOUR_MAX);
    j->pomodoros = (uint8_t)borner((int)j->pomodoros + (int)(o["pomodoros"] | 0), 0, 255);
    j->taches = (uint8_t)borner((int)j->taches + (int)(o["taches"] | 0), 0, 255);
  }
}

}  // namespace

namespace Stats {

void demarrer() {
  s_nb = 0;
  if (Stockage::monte()) charger();
  s_pret = true;
  JOURNAL("[stats] %d jour(s) en mémoire", s_nb);
}

void ajouterMinutes(int minutes) {
  if (minutes <= 0) return;
  JourStats* j = jourDuJour();
  if (!j) return;
  j->minutes = (uint16_t)mini((int)j->minutes + minutes, MINUTES_JOUR_MAX);
  sauver();
}

void pomodoroTermine() {
  JourStats* j = jourDuJour();
  if (!j || j->pomodoros >= 255) return;
  j->pomodoros++;
  sauver();
}

void tacheFaite() {
  JourStats* j = jourDuJour();
  if (!j || j->taches >= 255) return;
  j->taches++;
  sauver();
}

void tacheDefaite() {
  JourStats* j = jourDuJour();
  if (!j || j->taches == 0) return;
  j->taches--;
  sauver();
}

int derniersJours(JourStats* sortie, int n) {
  if (!sortie || n <= 0 || !Horloge::valide()) return 0;
  const int auj = Horloge::aujourdhui();
  if (!Horloge::dateValide(auj)) return 0;
  for (int k = 0; k < n; k++) {
    JourStats& j = sortie[k];
    const int date = Horloge::ajouterJours(auj, k - (n - 1));
    const JourStats* s = trouver(date);
    if (s) {
      j = *s;
    } else {
      memset(&j, 0, sizeof(JourStats));
      j.date = date;
    }
  }
  return n;
}

void effacer() {
  s_nb = 0;
  if (!Stockage::monte()) return;
  if (FFat.exists(F_STATS)) FFat.remove(F_STATS);
  if (FFat.exists(F_STATS_TMP)) FFat.remove(F_STATS_TMP);
}

}  // namespace Stats
