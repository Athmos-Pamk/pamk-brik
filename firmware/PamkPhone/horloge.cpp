#include "horloge.h"
#include <Wire.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <stdlib.h>

namespace Horloge {

// Conservé pendant le sommeil profond (mémoire RTC de l'ESP32-S3).
static RTC_DATA_ATTR bool r_valide = false;
static RTC_DATA_ATTR uint8_t r_source = 0;          // 0 aucune, 1 RTC, 2 NTP, 3 manuel
static RTC_DATA_ATTR uint32_t r_derniereSynchro = 0;

static bool s_rtc = false;
static volatile bool s_ntpArrive = false;
static bool s_ntpRecu = false;
static char s_fuseau[64] = FUSEAU_DEFAUT;

static const char* const SOURCES[] = {"aucune", "RTC", "NTP", "manuel"};

// ---------------------------------------------------------------------------
//  Calculs de dates (algorithmes « days from civil » de H. Hinnant)
// ---------------------------------------------------------------------------
static long joursCivils(int a, int m, int j) {
  a -= m <= 2;
  const long ere = (a >= 0 ? a : a - 399) / 400;
  const long ade = a - ere * 400;
  const long jda = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + j - 1;
  const long jde = ade * 365 + ade / 4 - ade / 100 + jda;
  return ere * 146097 + jde - 719468;
}

static void civilDepuisJours(long z, int& a, int& m, int& j) {
  z += 719468;
  const long ere = (z >= 0 ? z : z - 146096) / 146097;
  const long jde = z - ere * 146097;
  const long ade = (jde - jde / 1460 + jde / 36524 - jde / 146096) / 365;
  const long jda = jde - (365 * ade + ade / 4 - ade / 100);
  const long mp = (5 * jda + 2) / 153;
  j = (int)(jda - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  a = (int)(ade + ere * 400 + (m <= 2));
}

int composer(int annee, int mois, int jour) { return annee * 10000 + mois * 100 + jour; }

void decomposer(int date, int& annee, int& mois, int& jour) {
  annee = date / 10000;
  mois = (date / 100) % 100;
  jour = date % 100;
}

int joursDansMois(int annee, int mois) {
  static const uint8_t J[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mois < 1 || mois > 12) return 30;
  if (mois == 2) {
    const bool bis = (annee % 4 == 0 && annee % 100 != 0) || annee % 400 == 0;
    return bis ? 29 : 28;
  }
  return J[mois - 1];
}

bool dateValide(int date) {
  int a, m, j;
  decomposer(date, a, m, j);
  return a >= 2000 && a <= 2099 && m >= 1 && m <= 12 && j >= 1 && j <= joursDansMois(a, m);
}

int joursDepuis1970(int date) {
  int a, m, j;
  decomposer(date, a, m, j);
  return (int)joursCivils(a, m, j);
}

int dateDepuisJours(int jours) {
  int a, m, j;
  civilDepuisJours(jours, a, m, j);
  return composer(a, m, j);
}

int ajouterJours(int date, int n) { return dateDepuisJours(joursDepuis1970(date) + n); }
int ecartJours(int de, int a) { return joursDepuis1970(a) - joursDepuis1970(de); }

int jourSemaine(int date) {
  const int z = joursDepuis1970(date);   // 1970-01-01 était un jeudi (3 si lundi = 0)
  return ((z % 7) + 7 + 3) % 7;
}

int dateDe(const struct tm& t) { return composer(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday); }

int aujourdhui() {
  struct tm t;
  locale(t);
  return dateDe(t);
}

// ---------------------------------------------------------------------------
//  RTC PCF8563
// ---------------------------------------------------------------------------
static uint8_t versBcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int depuisBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

// Signature écrite dans les 4 registres d'alarme (0x09-0x0C), alarmes désactivées (bit 7
// AE = 1) : elle prouve que l'heure de la RTC a été écrite par ce firmware, EN UTC, après
// une synchro NTP ou un réglage manuel. Sans elle, l'heure vient d'ailleurs : la démo
// d'usine LilyGO (SensorLib hwClockWrite, SensorRTC.h l. 451-462) l'écrit en heure LOCALE
// de son fuseau, d'où le décalage de plusieurs heures vu sur la carte de Thomas.
// Valeurs choisies sans bit non implémenté : 47 min, 23 h, jour 31, jour de semaine 5.
static const uint8_t SIGNATURE[4] = {0xC7, 0xA3, 0xB1, 0x85};
static bool s_rtcSignee = false;

static bool rtcLire(uint32_t& utc) {
  Wire.beginTransmission(ADRESSE_RTC);
  Wire.write((uint8_t)0x02);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)ADRESSE_RTC, 11) != 11) return false;   // 0x02..0x0C
  uint8_t b[11];
  for (int i = 0; i < 11; i++) b[i] = (uint8_t)Wire.read();
  s_rtcSignee = memcmp(b + 7, SIGNATURE, 4) == 0;
  if (!s_rtcSignee) return false;  // heure écrite par un autre programme (fuseau inconnu)
  if (b[0] & 0x80) return false;   // bit VL : la RTC a perdu l'alimentation, heure douteuse
  const int s = depuisBcd(b[0] & 0x7F), mi = depuisBcd(b[1] & 0x7F), h = depuisBcd(b[2] & 0x3F);
  const int j = depuisBcd(b[3] & 0x3F), mo = depuisBcd(b[5] & 0x1F), a = 2000 + depuisBcd(b[6]);
  if (s > 59 || mi > 59 || h > 23 || mo < 1 || mo > 12 || j < 1 || j > joursDansMois(a, mo) || a < 2024) {
    return false;
  }
  utc = (uint32_t)joursCivils(a, mo, j) * 86400UL + (uint32_t)(h * 3600 + mi * 60 + s);
  return true;
}

static bool rtcEcrire(uint32_t utc) {
  const long jours = (long)(utc / 86400UL);
  const long reste = (long)(utc % 86400UL);
  int a, m, j;
  civilDepuisJours(jours, a, m, j);
  const int jourSem = (int)((jours + 4) % 7);   // 0 = dimanche (convention du PCF8563)
  Wire.beginTransmission(ADRESSE_RTC);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);                    // contrôle 1 : horloge en marche
  Wire.write((uint8_t)0x00);                    // contrôle 2 : pas d'alarme ni de minuterie
  Wire.write(versBcd(reste % 60));              // secondes (efface VL)
  Wire.write(versBcd((reste / 60) % 60));
  Wire.write(versBcd(reste / 3600));
  Wire.write(versBcd(j));
  Wire.write(versBcd(jourSem));
  Wire.write(versBcd(m));                       // siècle = 0 (années 2000)
  Wire.write(versBcd(a - 2000));
  for (int i = 0; i < 4; i++) Wire.write(SIGNATURE[i]);   // 0x09-0x0C : « écrit par Pamk Phone, en UTC »
  const bool ok = Wire.endTransmission() == 0;
  if (ok) s_rtcSignee = true;
  return ok;
}

static void reglerSysteme(uint32_t utc) {
  struct timeval tv;
  tv.tv_sec = (time_t)utc;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

static void quandNTP(struct timeval* tv) {
  (void)tv;
  s_ntpArrive = true;
}

void definirFuseau(const char* tz) {
  if (!tz || !*tz) tz = FUSEAU_DEFAUT;
  strncpy(s_fuseau, tz, sizeof(s_fuseau) - 1);
  s_fuseau[sizeof(s_fuseau) - 1] = 0;
  setenv("TZ", s_fuseau, 1);
  tzset();
}

void demarrer() {
  setenv("TZ", s_fuseau, 1);
  tzset();
  Wire.beginTransmission(ADRESSE_RTC);
  s_rtc = Wire.endTransmission() == 0;
  if (s_rtc) {
    uint32_t utc;
    if (rtcLire(utc)) {
      reglerSysteme(utc);
      if (!r_valide) {
        r_valide = true;
        r_source = 1;
      }
    } else if (!s_rtcSignee) {
      // Jamais réglée par ce firmware : on ne s'y fie pas (« Heure à régler » à l'écran)
      // tant qu'une synchro NTP ou un réglage manuel ne l'a pas réécrite en UTC.
      JOURNAL("[horloge] RTC jamais réglée par Pamk Phone (heure d'usine ?) : heure à régler");
    } else {
      JOURNAL("[horloge] RTC présente mais heure invalide (pile vide ?)");
    }
  } else {
    JOURNAL("[horloge] pas de RTC PCF8563 en 0x51 : l'heure ne survivra pas à une coupure");
  }
  if (!r_valide && time(nullptr) > 1735689600L) r_valide = true;   // après le 1/1/2025 : crédible
  sntp_set_time_sync_notification_cb(quandNTP);
}

void relireRTC() {
  uint32_t utc;
  if (s_rtc && rtcLire(utc)) reglerSysteme(utc);
}

void boucle() {
  if (s_ntpArrive) {
    s_ntpArrive = false;
    s_ntpRecu = true;
    const uint32_t utc = maintenant();
    r_valide = true;
    r_source = 2;
    r_derniereSynchro = utc;
    if (s_rtc) rtcEcrire(utc);
    JOURNAL("[horloge] heure NTP reçue, RTC mise à jour");
  }
}

bool rtcPresente() { return s_rtc; }
bool valide() { return r_valide; }
const char* source() { return SOURCES[r_source < 4 ? r_source : 0]; }
bool ntpRecu() { return s_ntpRecu; }
uint32_t derniereSynchro() { return r_derniereSynchro; }

uint32_t maintenant() {
  const time_t t = time(nullptr);
  return t > 0 ? (uint32_t)t : 0;
}

void localeDe(uint32_t utc, struct tm& t) {
  const time_t tt = (time_t)utc;
  localtime_r(&tt, &t);
}

void locale(struct tm& t) { localeDe(maintenant(), t); }

void regler(uint32_t utc, const char* src) {
  reglerSysteme(utc);
  if (s_rtc) rtcEcrire(utc);
  r_valide = true;
  r_source = 3;
  JOURNAL("[horloge] heure réglée (%s)", src ? src : "?");
}

void reglerLocale(int annee, int mois, int jour, int heure, int minute) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = annee - 1900;
  t.tm_mon = mois - 1;
  t.tm_mday = jour;
  t.tm_hour = heure;
  t.tm_min = minute;
  t.tm_isdst = -1;   // laisse le fuseau décider de l'heure d'été
  const time_t utc = mktime(&t);
  if (utc > 0) regler((uint32_t)utc, "manuel");
}

void lancerNTP(const char* serveur) {
  configTzTime(s_fuseau, (serveur && *serveur) ? serveur : NTP_DEFAUT, NTP_SECOURS);
}

// ---------------------------------------------------------------------------
//  Textes
// ---------------------------------------------------------------------------
static const char* const JOURS[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
static const char* const JOURS_COURTS[7] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
static const char* const MOIS[12] = {"janvier", "février", "mars", "avril", "mai", "juin",
                                     "juillet", "août", "septembre", "octobre", "novembre", "décembre"};
static const char* const MOIS_COURTS[12] = {"janv.", "févr.", "mars", "avr.", "mai", "juin",
                                            "juil.", "août", "sept.", "oct.", "nov.", "déc."};

const char* nomJour(int l0) { return JOURS[((l0 % 7) + 7) % 7]; }
const char* nomJourCourt(int l0) { return JOURS_COURTS[((l0 % 7) + 7) % 7]; }
const char* nomMois(int m) { return MOIS[borner(m, 1, 12) - 1]; }
const char* nomMoisCourt(int m) { return MOIS_COURTS[borner(m, 1, 12) - 1]; }

void texteHeure(char* b, size_t n, const struct tm& t) { snprintf(b, n, "%02d:%02d", t.tm_hour, t.tm_min); }

void texteDateLongue(char* b, size_t n, int date, bool annee) {
  int a, m, j;
  decomposer(date, a, m, j);
  const char* jour1 = (j == 1) ? "1er" : nullptr;
  if (annee) {
    if (jour1) snprintf(b, n, "%s %s %s %d", nomJour(jourSemaine(date)), jour1, nomMois(m), a);
    else snprintf(b, n, "%s %d %s %d", nomJour(jourSemaine(date)), j, nomMois(m), a);
  } else {
    if (jour1) snprintf(b, n, "%s %s %s", nomJour(jourSemaine(date)), jour1, nomMois(m));
    else snprintf(b, n, "%s %d %s", nomJour(jourSemaine(date)), j, nomMois(m));
  }
}

void texteDateCourte(char* b, size_t n, int date) {
  int a, m, j;
  decomposer(date, a, m, j);
  snprintf(b, n, "%s %d %s", nomJourCourt(jourSemaine(date)), j, nomMoisCourt(m));
}

void texteDateRelative(char* b, size_t n, int date) {
  const int e = ecartJours(aujourdhui(), date);
  if (e == 0) snprintf(b, n, "aujourd'hui");
  else if (e == 1) snprintf(b, n, "demain");
  else if (e == -1) snprintf(b, n, "hier");
  else if (e == 2) snprintf(b, n, "après-demain");
  else texteDateCourte(b, n, date);
}

void texteISO(char* b, size_t n, int date) {
  int a, m, j;
  decomposer(date, a, m, j);
  snprintf(b, n, "%04d-%02d-%02d", a, m, j);
}

int dateDepuisISO(const char* s) {
  if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-') return 0;
  const int a = atoi(s), m = atoi(s + 5), j = atoi(s + 8);
  const int d = composer(a, m, j);
  return dateValide(d) ? d : 0;
}

}  // namespace Horloge
