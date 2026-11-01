// Agenda en lecture seule (28/10/2026) : téléchargement HTTPS de l'adresse iCal secrète,
// lecture du flux octet par octet (rien n'est gardé en entier en mémoire : un agenda Google
// peut peser plusieurs Mo), répétitions simples développées, résultat dans agenda.json.
// L'adresse iCal est un secret : elle n'apparaît jamais dans le journal série.
#include "agenda.h"
#include "horloge.h"
#include "parametres.h"
#include "stockage.h"
#include "langue.h"
#include <FFat.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace {

// ---------------------------------------------------------------------------
//  Racines de confiance pour calendar.google.com (Google Trust Services)
//  Téléchargées le 28/10/2026 depuis https://pki.goog/repo/certs/gtsr1.pem … gtsr4.pem,
//  empreintes SHA-256 recoupées sur crt.sh (sujet et dates identiques), et les deux
//  chaînes servies ce jour-là par calendar.google.com vérifiées avec SEULEMENT ces racines :
//    RSA   : *.google.com <- WR2 <- GTS Root R1
//    ECDSA : *.google.com <- WE2 <- GTS Root R4
//  R2 et R3 sont gardées pour le jour où Google change d'intermédiaire.
//    GTS Root R1 (RSA 4096)  expire le 22/06/2036  SHA-256 D9:47:43:2A:…:57:A7:F4:CF
//    GTS Root R2 (RSA 4096)  expire le 22/06/2036  SHA-256 8D:25:CD:97:…:84:1C:7E:A8
//    GTS Root R3 (ECC P-384) expire le 22/06/2036  SHA-256 34:D8:A7:3E:…:01:0A:6F:48
//    GTS Root R4 (ECC P-384) expire le 22/06/2036  SHA-256 34:9D:FA:40:…:03:1B:3C:7D
// ---------------------------------------------------------------------------
const char RACINES[] PROGMEM =
    // GTS Root R1
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
    "-----END CERTIFICATE-----\n"
    // GTS Root R2
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "6WLAYv7YTVWW4tAR+kg0Eeye7QUd5MjWHYbL\n"
    "-----END CERTIFICATE-----\n"
    // GTS Root R3
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPluILrIPglJ209ZjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjMwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjMwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AAQfTzOHMymKoYTey8chWEGJ6ladK0uFxh1MJ7x/JlFyb+Kf1qPKzEUURout736G\n"
    "jOyxfi//qXGdGIRFBEFVbivqJn+7kAHjSxm65FSWRQmx1WyRRK2EE46ajA2ADDL2\n"
    "4CejQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBTB8Sa6oC2uhYHP0/EqEr24Cmf9vDAKBggqhkjOPQQDAwNpADBmAjEA9uEglRR7\n"
    "VKOQFhG/hMjqb2sXnh5GmCCbn9MN2azTL818+FsuVbu/3ZL3pAzcMeGiAjEA/Jdm\n"
    "ZuVDFhOD3cffL74UOO0BzrEXGhF16b0DjyZ+hOXJYKaV11RZt+cRLInUue4X\n"
    "-----END CERTIFICATE-----\n"
    // GTS Root R4
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
    "VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
    "A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
    "WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
    "IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
    "AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
    "QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
    "HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
    "BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
    "9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
    "p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
    "-----END CERTIFICATE-----\n";

// ---------------------------------------------------------------------------
//  Réglages
// ---------------------------------------------------------------------------
const int EVENEMENTS_MAX = 300;
const int JOURS_AVANT = 1;                 // fenêtre gardée : J-1 …
const int JOURS_APRES = 60;                // … J+60
const uint32_t SYNCHRO_TOUTES_S = 3UL * 3600UL;
const uint32_t PAUSE_APRES_ESSAI_S = 30UL * 60UL;   // pas de nouvel essai automatique avant 30 min
const uint32_t DELAI_TOTAL_MS = 60000;     // téléchargement complet
const uint32_t DELAI_SILENCE_MS = 10000;   // sans aucun octet reçu
const int LIGNE_MAX = 512;                 // ligne dépliée (DESCRIPTION peut être bien plus longue :
                                           // on n'en garde que le début, inutile ici)
const int REGLE_MAX = 160;
const int EXCLUS_MAX = 32;                 // EXDATE par événement
const int REMPLACEMENTS_MAX = 128;         // occurrences modifiées (RECURRENCE-ID)
const int ITERATIONS_MAX = 20000;          // garde-fou par série
const int JOURNEE_JOURS_MAX = 31;          // événement « toute la journée » sur plusieurs jours
const char* const F_AGENDA = "/agenda.json";
const char* const F_AGENDA_TMP = "/agenda.tmp";

// Documents JSON en PSRAM (comme Stockage)
struct AllocateurPSRAM : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return ps_malloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { return ps_realloc(p, n); }
};
AllocateurPSRAM g_alloc;

// Date et heure telles qu'écrites dans le fichier
struct Moment {
  int date;              // AAAAMMJJ, 0 = absent
  int16_t minute;        // minutes depuis minuit, -1 = date seule (toute la journée)
  bool utc;              // suffixe « Z » ; sinon heure locale (TZID supposé = fuseau du téléphone)
};

// Un VEVENT en cours de lecture (ses propriétés arrivent dans n'importe quel ordre)
struct EvLu {
  char titre[72];
  char lieu[48];
  char regle[REGLE_MAX];
  Moment debut, fin, recurrence;
  int32_t dureeMin;      // DURATION, -1 = absente
  uint32_t uid;          // empreinte de l'UID
  bool annule;
  int nbExclus;
  Moment exclus[EXCLUS_MAX];   // EXDATE, déjà en heure locale
};

struct EvTmp {
  Evenement e;
  uint32_t uid;
  bool serie;            // produit par une règle de répétition
};

struct Remplacement {
  uint32_t uid;
  int date;
  int16_t minute;
};

// Tout l'état de lecture, en PSRAM (environ 45 Kio), gardé d'une synchro à l'autre.
struct Contexte {
  EvTmp ev[EVENEMENTS_MAX];
  int nb;
  Remplacement rem[REMPLACEMENTS_MAX];
  int nbRem;
  EvLu lu;
  char ligne[LIGNE_MAX];
  int lgLigne;
  bool finLigne;         // fin de ligne vue : la suivante dira si celle-ci continue (repli)
  bool dansCalendrier, finCalendrier, dansEvenement;
  int sousNiveau;        // VALARM… dans un VEVENT : ignorés
  int dateMin, dateMax;  // fenêtre, heure locale
};

enum Frequence : uint8_t { F_AUCUNE, F_JOUR, F_SEMAINE, F_MOIS, F_AN };

struct Regle {
  uint8_t freq;
  int intervalle;
  int compte;            // COUNT, 0 = illimité
  Moment jusqua;         // UNTIL, date 0 = aucune
  uint8_t jours;         // BYDAY, bit 0 = lundi
  int8_t rang;           // mensuel « 2TU » : 2, « -1FR » : -1 ; 0 = aucun
  int8_t jourMois;       // BYMONTHDAY (une seule valeur), 0 = aucun
  uint8_t debutSemaine;  // WKST, 0 = lundi
};

Evenement* s_ev = nullptr;
int s_nb = 0;
uint32_t s_synchro = 0;                    // UTC de la dernière synchro réussie
char s_erreur[72] = "";
uint16_t s_essais = 0;
Contexte* s_ctx = nullptr;
RTC_DATA_ATTR uint32_t r_dernierEssai = 0; // UTC, gardé pendant le sommeil profond

// ---------------------------------------------------------------------------
//  Petits outils
// ---------------------------------------------------------------------------
// Longueur de s ramenée au dernier caractère UTF-8 complet.
int finUtf8(const char* s, int k) {
  int i = k;
  while (i > 0 && (((uint8_t)s[i - 1]) & 0xC0) == 0x80) i--;
  if (i == 0) return 0;
  const uint8_t tete = (uint8_t)s[i - 1];
  int lg = 1;
  if ((tete & 0xE0) == 0xC0) lg = 2;
  else if ((tete & 0xF0) == 0xE0) lg = 3;
  else if ((tete & 0xF8) == 0xF0) lg = 4;
  return (i - 1 + lg <= k) ? k : i - 1;
}

void copier(char* d, size_t n, const char* s) {
  if (!s) s = "";
  int l = 0;
  while (s[l] && l < (int)n - 1) { d[l] = s[l]; l++; }
  d[finUtf8(d, l)] = 0;
}

// Texte iCal : « \, » « \; » « \\ » « \n » (saut de ligne -> espace)
void copierTexte(char* d, size_t n, const char* s) {
  int k = 0;
  while (*s && k < (int)n - 1) {
    char c = *s++;
    if (c == '\\' && *s) {
      const char e = *s++;
      c = (e == 'n' || e == 'N') ? ' ' : e;
    }
    d[k++] = c;
  }
  d[finUtf8(d, k)] = 0;
}

bool egal(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

uint32_t hacher(const char* s) {   // FNV-1a
  uint32_t h = 2166136261UL;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619UL; }
  return h;
}

// Jours depuis 1970 -> jour de la semaine, 0 = lundi (le 1/1/1970 était un jeudi).
int jourSemaineJ(int j) { return ((j % 7) + 7 + 3) % 7; }

int jourDe(const char* s) {   // « MO » … « SU » -> 0 … 6, -1 si inconnu
  static const char* const NOMS[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; i++) if (strncasecmp(s, NOMS[i], 2) == 0) return i;
  return -1;
}

bool chiffres(const char* s, int n) {
  for (int i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

int nombre(const char* s, int n) {
  int v = 0;
  for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
  return v;
}

// « 20261028 », « 20261028T143000 », « 20261028T133000Z »
bool lireMoment(const char* params, const char* v, Moment& m) {
  m.date = 0;
  m.minute = -1;
  m.utc = false;
  if (!chiffres(v, 8)) return false;
  const int a = nombre(v, 4), mo = nombre(v + 4, 2), j = nombre(v + 6, 2);
  if (a < 1900 || a > 2199 || mo < 1 || mo > 12 || j < 1 || j > Horloge::joursDansMois(a, mo)) return false;
  const bool dateSeule = strstr(params, "VALUE=DATE") && !strstr(params, "VALUE=DATE-TIME");
  if (v[8] == 'T' && !dateSeule && chiffres(v + 9, 4)) {
    const int h = nombre(v + 9, 2), mi = nombre(v + 11, 2);
    if (h > 23 || mi > 59) return false;
    m.minute = (int16_t)(h * 60 + mi);
    const char* fin = v + 13;
    if (chiffres(fin, 2)) fin += 2;   // secondes
    m.utc = *fin == 'Z' || *fin == 'z';
  }
  m.date = Horloge::composer(a, mo, j);
  return true;
}

// Moment du fichier -> heure locale du téléphone
void versLocal(const Moment& m, int& date, int16_t& minute) {
  date = m.date;
  minute = m.minute;
  if (!m.utc || m.minute < 0 || m.date < 19700102 || m.date > 21051231) return;
  const uint32_t utc = (uint32_t)Horloge::joursDepuis1970(m.date) * 86400UL + (uint32_t)m.minute * 60UL;
  struct tm t;
  Horloge::localeDe(utc, t);
  date = Horloge::dateDe(t);
  minute = (int16_t)(t.tm_hour * 60 + t.tm_min);
}

// DURATION : « PT1H30M », « P1D », « P2W » -> minutes (-1 si illisible ou négative)
int32_t lireDuree(const char* v) {
  if (*v == '+') v++;
  if (*v == '-' || *v != 'P') return -1;
  v++;
  int32_t total = 0, n = 0;
  bool temps = false;
  for (; *v; v++) {
    if (*v == 'T') { temps = true; continue; }
    if (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); continue; }
    switch (*v) {
      case 'W': total += n * 7 * 1440; break;
      case 'D': total += n * 1440; break;
      case 'H': total += n * 60; break;
      case 'M': if (temps) total += n; break;
      default: break;   // secondes : négligées
    }
    n = 0;
  }
  return total;
}

int32_t minutesEntre(const Moment& a, const Moment& b) {
  int da = a.date, db = b.date;
  int16_t ma = a.minute, mb = b.minute;
  if (a.utc != b.utc) {   // repères différents : on compare en heure locale
    versLocal(a, da, ma);
    versLocal(b, db, mb);
  }
  if (ma < 0) ma = 0;
  if (mb < 0) mb = 0;
  return (int32_t)(Horloge::joursDepuis1970(db) - Horloge::joursDepuis1970(da)) * 1440 + (mb - ma);
}

bool avant(int date, int16_t debut, const Evenement& b) {
  return date < b.date || (date == b.date && debut < b.debut);
}

// ---------------------------------------------------------------------------
//  Construction de la liste
// ---------------------------------------------------------------------------
void ajouter(Contexte& c, const EvLu& e, int date, int16_t debut, int16_t fin, bool serie) {
  int place = c.nb;
  if (c.nb >= EVENEMENTS_MAX) {
    // Plein : on garde les plus proches, en remplaçant le plus tardif s'il vient après.
    int k = 0;
    for (int i = 1; i < c.nb; i++) if (avant(c.ev[k].e.date, c.ev[k].e.debut, c.ev[i].e)) k = i;
    if (!avant(date, debut, c.ev[k].e)) return;
    place = k;
  } else {
    c.nb++;
  }
  EvTmp& t = c.ev[place];
  t.e.date = date;
  t.e.debut = debut;
  t.e.fin = fin;
  copier(t.e.titre, sizeof(t.e.titre), e.titre[0] ? e.titre : "(sans titre)");
  copier(t.e.lieu, sizeof(t.e.lieu), e.lieu);
  t.uid = e.uid;
  t.serie = serie;
}

// Une occurrence, dans le repère de DTSTART. duree : jours (toute la journée) ou minutes
// (-1 = fin inconnue).
void ajouterOccurrence(Contexte& c, const EvLu& e, int dateRef, int16_t minuteRef, int32_t duree, bool serie) {
  Moment m;
  m.date = dateRef;
  m.minute = minuteRef;
  m.utc = e.debut.utc;
  int date;
  int16_t debut;
  versLocal(m, date, debut);
  for (int k = 0; k < e.nbExclus; k++) {   // EXDATE
    const Moment& x = e.exclus[k];
    if (x.date == date && (x.minute < 0 || debut < 0 || x.minute == debut)) return;
  }
  if (debut < 0) {   // toute la journée, éventuellement sur plusieurs jours
    const int n = borner((int)duree, 1, JOURNEE_JOURS_MAX);
    for (int i = 0; i < n; i++) {
      const int d = i == 0 ? date : Horloge::ajouterJours(date, i);
      if (d > c.dateMax) break;
      if (d >= c.dateMin) ajouter(c, e, d, -1, -1, serie);
    }
    return;
  }
  if (date < c.dateMin || date > c.dateMax) return;
  int16_t fin = -1;   // fin le lendemain ou inconnue : seulement l'heure de début
  if (duree >= 0 && debut + duree < 1440) fin = (int16_t)(debut + duree);
  ajouter(c, e, date, debut, fin, serie);
}

bool lireRegle(const char* texte, const EvLu& e, Regle& r) {
  memset(&r, 0, sizeof(r));
  r.intervalle = 1;
  char t[REGLE_MAX];
  copier(t, sizeof(t), texte);
  bool joursSansRang = false;
  char* reste = nullptr;
  for (char* morceau = strtok_r(t, ";", &reste); morceau; morceau = strtok_r(nullptr, ";", &reste)) {
    char* signe = strchr(morceau, '=');
    if (!signe) continue;
    *signe = 0;
    const char* cle = morceau;
    const char* val = signe + 1;
    if (egal(cle, "FREQ")) {
      if (egal(val, "DAILY")) r.freq = F_JOUR;
      else if (egal(val, "WEEKLY")) r.freq = F_SEMAINE;
      else if (egal(val, "MONTHLY")) r.freq = F_MOIS;
      else if (egal(val, "YEARLY")) r.freq = F_AN;
      else return false;   // HOURLY, MINUTELY… : non géré
    } else if (egal(cle, "INTERVAL")) {
      r.intervalle = borner(atoi(val), 1, 1000);
    } else if (egal(cle, "COUNT")) {
      r.compte = maxi(atoi(val), 1);
    } else if (egal(cle, "UNTIL")) {
      if (!lireMoment("", val, r.jusqua)) return false;
    } else if (egal(cle, "WKST")) {
      const int j = jourDe(val);
      if (j >= 0) r.debutSemaine = (uint8_t)j;
    } else if (egal(cle, "BYDAY")) {
      int nb = 0;
      for (const char* p = val; *p;) {
        int rang = 0, signeRang = 1;
        if (*p == '+' || *p == '-') { signeRang = *p == '-' ? -1 : 1; p++; }
        while (*p >= '0' && *p <= '9') { rang = rang * 10 + (*p - '0'); p++; }
        const int j = jourDe(p);
        if (j < 0) return false;
        r.jours |= (uint8_t)(1 << j);
        if (rang) r.rang = (int8_t)(signeRang * borner(rang, 1, 5));
        else joursSansRang = true;
        nb++;
        p += 2;
        if (*p == ',') p++;
      }
      if (r.rang && nb > 1) return false;     // « 1MO,3MO » : non géré
    } else if (egal(cle, "BYMONTHDAY")) {
      if (strchr(val, ',')) return false;
      r.jourMois = (int8_t)borner(atoi(val), -31, 31);
    } else if (egal(cle, "BYMONTH")) {
      // Toléré seulement s'il répète le mois de DTSTART (annuel simple)
      if (strchr(val, ',') || atoi(val) != (e.debut.date / 100) % 100) return false;
    } else {
      return false;        // BYSETPOS, BYYEARDAY, BYWEEKNO, BYHOUR… : non géré
    }
  }
  switch (r.freq) {
    case F_JOUR:    return !r.rang && !r.jourMois;                 // BYDAY = filtre des jours
    case F_SEMAINE: return !r.rang && !r.jourMois;
    case F_MOIS:    return !joursSansRang && !(r.rang && r.jourMois);
    case F_AN:      return !r.jours && !r.jourMois;
    default:        return false;
  }
}

// Propose les occurrences d'une série, dans l'ordre ; s'arrête à COUNT, UNTIL ou après la fenêtre.
struct Generateur {
  Contexte* c;
  const EvLu* e;
  const Regle* r;
  int32_t duree;
  int jMin, jMax;        // fenêtre dans le repère de DTSTART (en jours depuis 1970), avec marge
  int jusquaJour;
  int16_t jusquaMin;
  int n;

  bool proposer(int j) {   // false : la série est finie
    if (j > jMax) return false;
    if (j > jusquaJour || (j == jusquaJour && e->debut.minute > jusquaMin)) return false;
    n++;
    if (r->compte && n > r->compte) return false;
    if (j >= jMin) ajouterOccurrence(*c, *e, Horloge::dateDepuisJours(j), e->debut.minute, duree, true);
    return true;
  }
};

// n-ième jour js (0 = lundi) du mois ; rang < 0 : depuis la fin. 0 s'il n'existe pas.
int nieme(int a, int m, int rang, int js) {
  const int nbJ = Horloge::joursDansMois(a, m);
  if (rang > 0) {
    const int wd = jourSemaineJ(Horloge::joursDepuis1970(Horloge::composer(a, m, 1)));
    const int d = 1 + (js - wd + 7) % 7 + (rang - 1) * 7;
    return d <= nbJ ? d : 0;
  }
  const int wd = jourSemaineJ(Horloge::joursDepuis1970(Horloge::composer(a, m, nbJ)));
  const int d = nbJ - (wd - js + 7) % 7 + (rang + 1) * 7;
  return d >= 1 ? d : 0;
}

void developper(Contexte& c, const EvLu& e, const Regle& r, int32_t duree) {
  const bool journee = e.debut.minute < 0;
  const int j0 = Horloge::joursDepuis1970(e.debut.date);
  Generateur g;
  g.c = &c;
  g.e = &e;
  g.r = &r;
  g.duree = duree;
  // Un fuseau décale d'un jour au plus ; une occurrence de plusieurs jours peut commencer avant.
  g.jMin = Horloge::joursDepuis1970(c.dateMin) - 1 - (journee ? borner((int)duree, 1, JOURNEE_JOURS_MAX) : 0);
  g.jMax = Horloge::joursDepuis1970(c.dateMax) + 1;
  g.jusquaJour = 0x7FFFFFFF;
  g.jusquaMin = 1439;
  g.n = 0;
  if (r.jusqua.date) {
    int d = r.jusqua.date;
    int16_t mi = r.jusqua.minute;
    if (r.jusqua.utc && !e.debut.utc) versLocal(r.jusqua, d, mi);   // série en heure locale (TZID)
    g.jusquaJour = Horloge::joursDepuis1970(d);
    g.jusquaMin = (mi < 0 || journee) ? 1439 : mi;
  }
  int a0, m0, d0;
  Horloge::decomposer(e.debut.date, a0, m0, d0);
  switch (r.freq) {
    case F_JOUR: {
      int k = 0;
      if (!r.compte && j0 < g.jMin) k = (g.jMin - j0) / r.intervalle;   // saute jusqu'à la fenêtre
      for (int it = 0; it < ITERATIONS_MAX; it++, k++) {
        const int j = j0 + k * r.intervalle;
        if (j > g.jMax) break;
        if (r.jours && !(r.jours & (1 << jourSemaineJ(j)))) continue;
        if (!g.proposer(j)) break;
      }
      break;
    }
    case F_SEMAINE: {
      const uint8_t jours = r.jours ? r.jours : (uint8_t)(1 << jourSemaineJ(j0));
      const int s0 = j0 - (jourSemaineJ(j0) - r.debutSemaine + 7) % 7;   // début de la semaine de DTSTART
      const int pas = 7 * r.intervalle;
      int k = 0;
      if (!r.compte && s0 + 6 < g.jMin) k = (g.jMin - 6 - s0) / pas;
      bool fini = false;
      for (int it = 0; it < ITERATIONS_MAX && !fini; it++, k++) {
        const int s = s0 + k * pas;
        if (s > g.jMax) break;
        for (int o = 0; o < 7; o++) {
          if (!(jours & (1 << ((r.debutSemaine + o) % 7)))) continue;
          const int j = s + o;
          if (j < j0) continue;
          if (!g.proposer(j)) { fini = true; break; }
        }
      }
      break;
    }
    case F_MOIS: {
      int js = 0;
      for (int i = 0; i < 7; i++) if (r.jours & (1 << i)) js = i;
      const int M0 = a0 * 12 + (m0 - 1);
      int k = 0;
      if (!r.compte) {
        int a, m, d;
        Horloge::decomposer(Horloge::dateDepuisJours(g.jMin), a, m, d);
        const int Mmin = a * 12 + (m - 1);
        if (Mmin > M0) k = (Mmin - M0) / r.intervalle;
      }
      for (int it = 0; it < ITERATIONS_MAX; it++, k++) {
        const int M = M0 + k * r.intervalle;
        const int a = M / 12, m = M % 12 + 1;
        if (Horloge::joursDepuis1970(Horloge::composer(a, m, 1)) > g.jMax) break;
        const int nbJ = Horloge::joursDansMois(a, m);
        int jour = 0;
        if (r.rang) {
          jour = nieme(a, m, r.rang, js);
        } else {
          int jm = r.jourMois ? r.jourMois : d0;
          if (jm < 0) jm = nbJ + jm + 1;
          if (jm >= 1 && jm <= nbJ) jour = jm;   // le 31 d'un mois de 30 jours : sauté (RFC 5545)
        }
        if (!jour) continue;
        const int j = Horloge::joursDepuis1970(Horloge::composer(a, m, jour));
        if (j < j0) continue;
        if (!g.proposer(j)) break;
      }
      break;
    }
    case F_AN: {
      int k = 0;
      if (!r.compte) {
        int a, m, d;
        Horloge::decomposer(Horloge::dateDepuisJours(g.jMin), a, m, d);
        if (a - 1 > a0) k = (a - 1 - a0) / r.intervalle;
      }
      for (int it = 0; it < ITERATIONS_MAX; it++, k++) {
        const int a = a0 + k * r.intervalle;
        if (Horloge::joursDepuis1970(Horloge::composer(a, 1, 1)) > g.jMax) break;
        if (d0 > Horloge::joursDansMois(a, m0)) continue;   // 29 février
        const int j = Horloge::joursDepuis1970(Horloge::composer(a, m0, d0));
        if (j < j0) continue;
        if (!g.proposer(j)) break;
      }
      break;
    }
    default:
      break;
  }
}

void finirEvenement(Contexte& c) {
  EvLu& e = c.lu;
  if (!e.debut.date) return;
  const bool journee = e.debut.minute < 0;
  int32_t duree;   // jours (toute la journée) ou minutes (-1 = inconnue)
  if (e.fin.date) {
    duree = journee ? Horloge::joursDepuis1970(e.fin.date) - Horloge::joursDepuis1970(e.debut.date)
                    : minutesEntre(e.debut, e.fin);
  } else if (e.dureeMin >= 0) {
    duree = journee ? e.dureeMin / 1440 : e.dureeMin;
  } else {
    duree = journee ? 1 : -1;
  }
  if (journee && duree < 1) duree = 1;
  if (!journee && duree < 0) duree = -1;

  if (e.recurrence.date) {
    // Occurrence modifiée (ou annulée) d'une série : elle remplace celle prévue par la règle.
    if (c.nbRem < REMPLACEMENTS_MAX) {
      Remplacement& r = c.rem[c.nbRem++];
      r.uid = e.uid;
      versLocal(e.recurrence, r.date, r.minute);
    }
    if (!e.annule) ajouterOccurrence(c, e, e.debut.date, e.debut.minute, duree, false);
    return;
  }
  if (e.annule) return;
  Regle r;
  if (e.regle[0] && lireRegle(e.regle, e, r)) developper(c, e, r, duree);
  else ajouterOccurrence(c, e, e.debut.date, e.debut.minute, duree, false);   // règle non gérée : la première
}

void lireExclusions(const char* params, const char* v, EvLu& e) {
  char t[24];
  while (*v && e.nbExclus < EXCLUS_MAX) {
    const char* fin = strchr(v, ',');
    const size_t n = fin ? (size_t)(fin - v) : strlen(v);
    if (n < sizeof(t)) {
      memcpy(t, v, n);
      t[n] = 0;
      Moment m;
      if (lireMoment(params, t, m)) {
        Moment& x = e.exclus[e.nbExclus++];
        versLocal(m, x.date, x.minute);
        x.utc = false;
      }
    }
    if (!fin) break;
    v = fin + 1;
  }
}

// Une ligne dépliée complète : « NOM;PARAM=…:VALEUR »
void traiterLigne(Contexte& c) {
  char* l = c.ligne;
  l[c.lgLigne] = 0;
  if ((uint8_t)l[0] == 0xEF && (uint8_t)l[1] == 0xBB && (uint8_t)l[2] == 0xBF) l += 3;   // BOM UTF-8
  char* deuxPoints = nullptr;
  char* pointVirgule = nullptr;
  bool guillemets = false;
  for (char* p = l; *p; p++) {
    if (*p == '"') guillemets = !guillemets;
    else if (!guillemets && *p == ';' && !pointVirgule) pointVirgule = p;
    else if (!guillemets && *p == ':') { deuxPoints = p; break; }
  }
  if (!deuxPoints) return;
  *deuxPoints = 0;
  const char* valeur = deuxPoints + 1;
  const char* params = "";
  if (pointVirgule) {
    *pointVirgule = 0;
    params = pointVirgule + 1;
  }
  const char* nom = l;

  if (egal(nom, "BEGIN")) {
    if (egal(valeur, "VCALENDAR")) {
      c.dansCalendrier = true;
    } else if (c.dansEvenement) {
      c.sousNiveau++;
    } else if (egal(valeur, "VEVENT")) {
      c.dansEvenement = true;
      c.sousNiveau = 0;
      memset(&c.lu, 0, sizeof(c.lu));
      c.lu.dureeMin = -1;
    }
    return;
  }
  if (egal(nom, "END")) {
    if (egal(valeur, "VCALENDAR")) {
      c.finCalendrier = true;
    } else if (c.dansEvenement) {
      if (c.sousNiveau > 0) {
        c.sousNiveau--;
      } else if (egal(valeur, "VEVENT")) {
        c.dansEvenement = false;
        finirEvenement(c);
      }
    }
    return;
  }
  if (!c.dansEvenement || c.sousNiveau > 0) return;
  EvLu& e = c.lu;
  if (egal(nom, "SUMMARY")) copierTexte(e.titre, sizeof(e.titre), valeur);
  else if (egal(nom, "LOCATION")) copierTexte(e.lieu, sizeof(e.lieu), valeur);
  else if (egal(nom, "DTSTART")) lireMoment(params, valeur, e.debut);
  else if (egal(nom, "DTEND")) lireMoment(params, valeur, e.fin);
  else if (egal(nom, "DURATION")) e.dureeMin = lireDuree(valeur);
  else if (egal(nom, "RRULE")) copier(e.regle, sizeof(e.regle), valeur);
  else if (egal(nom, "EXDATE")) lireExclusions(params, valeur, e);
  else if (egal(nom, "RECURRENCE-ID")) lireMoment(params, valeur, e.recurrence);
  else if (egal(nom, "UID")) e.uid = hacher(valeur);
  else if (egal(nom, "STATUS")) e.annule = egal(valeur, "CANCELLED");
}

// Un octet du fichier (après le décodage « chunked »). Lignes repliées : une ligne qui commence
// par une espace ou une tabulation continue la précédente (RFC 5545, 3.1).
void lireOctet(Contexte& c, uint8_t b) {
  if (b == '\r') return;
  if (c.finLigne) {
    c.finLigne = false;
    if (b == ' ' || b == '\t') return;
    traiterLigne(c);
    c.lgLigne = 0;
  }
  if (b == '\n') {
    c.finLigne = true;
    return;
  }
  if (c.lgLigne < LIGNE_MAX - 1) c.ligne[c.lgLigne++] = (char)b;
}

void viderLigne(Contexte& c) {
  if (c.finLigne || c.lgLigne > 0) traiterLigne(c);
  c.lgLigne = 0;
  c.finLigne = false;
}

// Décodage « Transfer-Encoding: chunked » (Google l'utilise), octet par octet.
enum EtatMorceau : uint8_t { M_TAILLE, M_EXTENSION, M_DONNEES, M_APRES_DONNEES, M_TERMINE };

struct Morceaux {
  bool actif;
  uint8_t etat;
  uint32_t reste;
  bool vu;               // au moins un chiffre de taille lu
};

// false : dernier morceau reçu (fin du fichier)
bool finTaille(Morceaux& d) {
  if (!d.vu) return true;   // ligne vide parasite
  if (d.reste == 0) {
    d.etat = M_TERMINE;
    return false;
  }
  d.etat = M_DONNEES;
  return true;
}

bool octet(Contexte& c, Morceaux& d, uint8_t b) {
  if (!d.actif) {
    lireOctet(c, b);
    return true;
  }
  switch (d.etat) {
    case M_TAILLE: {
      int v = -1;
      if (b >= '0' && b <= '9') v = b - '0';
      else if (b >= 'a' && b <= 'f') v = b - 'a' + 10;
      else if (b >= 'A' && b <= 'F') v = b - 'A' + 10;
      if (v >= 0) {
        d.reste = d.reste * 16 + (uint32_t)v;
        d.vu = true;
      } else if (b == ';') {
        d.etat = M_EXTENSION;
      } else if (b == '\n') {
        return finTaille(d);
      }
      return true;
    }
    case M_EXTENSION:
      return b == '\n' ? finTaille(d) : true;
    case M_DONNEES:
      lireOctet(c, b);
      if (--d.reste == 0) d.etat = M_APRES_DONNEES;
      return true;
    case M_APRES_DONNEES:   // « \r\n » après les données du morceau
      if (b == '\n') {
        d.etat = M_TAILLE;
        d.reste = 0;
        d.vu = false;
      }
      return true;
    default:
      return false;
  }
}

void finaliser(Contexte& c) {
  // Occurrences remplacées (RECURRENCE-ID) : on retire celles prévues par la règle.
  int k = 0;
  for (int i = 0; i < c.nb; i++) {
    bool retirer = false;
    if (c.ev[i].serie) {
      for (int r = 0; r < c.nbRem && !retirer; r++) {
        const Remplacement& x = c.rem[r];
        retirer = x.uid == c.ev[i].uid && x.date == c.ev[i].e.date && x.minute == c.ev[i].e.debut;
      }
    }
    if (!retirer) {
      if (k != i) c.ev[k] = c.ev[i];
      k++;
    }
  }
  c.nb = k;
  // Tri par date puis heure (toute la journée d'abord)
  for (int i = 1; i < c.nb; i++) {
    const EvTmp x = c.ev[i];
    int j = i - 1;
    while (j >= 0 && avant(x.e.date, x.e.debut, c.ev[j].e)) {
      c.ev[j + 1] = c.ev[j];
      j--;
    }
    c.ev[j + 1] = x;
  }
}

bool echec(const char* message) {
  copier(s_erreur, sizeof(s_erreur), message);
  JOURNAL("[agenda] échec : %s", message);
  return false;
}

bool sauver() {
  if (!Stockage::monte()) return false;
  JsonDocument doc(&g_alloc);
  doc["version"] = 1;
  doc["synchro"] = s_synchro;
  JsonArray a = doc["evenements"].to<JsonArray>();
  for (int i = 0; i < s_nb; i++) {
    const Evenement& ev = s_ev[i];
    JsonObject o = a.add<JsonObject>();
    o["d"] = ev.date;
    o["h"] = ev.debut;
    o["f"] = ev.fin;
    o["t"] = (const char*)ev.titre;
    if (ev.lieu[0]) o["l"] = (const char*)ev.lieu;
  }
  if (doc.overflowed()) {
    JOURNAL("[agenda] mémoire insuffisante pour agenda.json");
    return false;
  }
  File f = FFat.open(F_AGENDA_TMP, "w");
  if (!f) return false;
  const size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) {
    FFat.remove(F_AGENDA_TMP);
    return false;
  }
  if (FFat.exists(F_AGENDA)) FFat.remove(F_AGENDA);
  return FFat.rename(F_AGENDA_TMP, F_AGENDA);
}

void charger() {
  if (!Stockage::monte()) return;
  // Une écriture interrompue entre « supprimer » et « renommer » laisse le .tmp complet.
  if (!FFat.exists(F_AGENDA) && FFat.exists(F_AGENDA_TMP)) FFat.rename(F_AGENDA_TMP, F_AGENDA);
  if (!FFat.exists(F_AGENDA)) return;
  File f = FFat.open(F_AGENDA, "r");
  if (!f) return;
  JsonDocument doc(&g_alloc);
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    JOURNAL("[agenda] agenda.json : %s", err.c_str());
    return;
  }
  s_synchro = doc["synchro"] | 0UL;
  for (JsonObject o : doc["evenements"].as<JsonArray>()) {
    if (s_nb >= EVENEMENTS_MAX) break;
    Evenement& ev = s_ev[s_nb];
    ev.date = o["d"] | 0;
    ev.debut = (int16_t)(o["h"] | -1);
    ev.fin = (int16_t)(o["f"] | -1);
    copier(ev.titre, sizeof(ev.titre), o["t"] | "");
    copier(ev.lieu, sizeof(ev.lieu), o["l"] | "");
    if (ev.date) s_nb++;
  }
}

// Téléchargement et lecture. false + s_erreur si quelque chose cloche.
bool telecharger(const char* url, Contexte& c) {
  const uint32_t t0 = millis();
  WiFiClientSecure client;
  client.setCACert(RACINES);
  client.setHandshakeTimeout(15);   // secondes
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("PamkBrik/" PAMK_VERSION);
  const char* cles[] = {"Transfer-Encoding"};
  http.collectHeaders(cles, 1);
  if (!http.begin(client, String(url))) return echec("adresse invalide");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char m[72];
    if (code < 0) {
      char tls[80] = "";
      client.lastError(tls, sizeof(tls));
      JOURNAL("[agenda] connexion : %s %s", HTTPClient::errorToString(code).c_str(), tls);
      snprintf(m, sizeof(m), TR("connexion impossible (%d)"), code);
    } else if (code == 400 || code == 401 || code == 403 || code == 404) {
      snprintf(m, sizeof(m), TR("adresse refusée (%d) : vérifie-la"), code);
    } else {
      snprintf(m, sizeof(m), TR("le serveur répond %d"), code);
    }
    http.end();
    return echec(m);
  }
  String te = http.header("Transfer-Encoding");
  te.toLowerCase();
  Morceaux d;
  d.actif = te.indexOf("chunked") >= 0;
  d.etat = M_TAILLE;
  d.reste = 0;
  d.vu = false;
  int32_t reste = http.getSize();   // -1 : inconnue (lu jusqu'à la fermeture)
  WiFiClient* flux = http.getStreamPtr();
  if (!flux) {
    http.end();
    return echec("connexion perdue");
  }
  static uint8_t tampon[1024];       // hors de la pile de la boucle principale
  uint32_t total = 0;
  uint32_t tDernier = millis();
  const char* erreur = nullptr;
  bool fini = false;
  while (!fini) {
    const int dispo = flux->available();
    if (dispo > 0) {
      const int n = flux->read(tampon, dispo < (int)sizeof(tampon) ? (size_t)dispo : sizeof(tampon));
      if (n < 0) break;
      tDernier = millis();
      total += (uint32_t)n;
      for (int i = 0; i < n; i++) {
        if (!octet(c, d, tampon[i])) { fini = true; break; }
      }
      if (!d.actif && reste > 0) {
        reste -= n;
        if (reste <= 0) fini = true;
      }
    } else if (!flux->connected()) {
      break;   // le serveur a fermé : fin du fichier
    } else if (millis() - tDernier > DELAI_SILENCE_MS) {
      erreur = "le serveur ne répond plus";
      break;
    } else {
      delay(5);
    }
    if (millis() - t0 > DELAI_TOTAL_MS) {
      erreur = "agenda trop long à télécharger";
      break;
    }
  }
  http.end();
  if (erreur) return echec(erreur);
  viderLigne(c);
  JOURNAL("[agenda] %lu octets lus en %lu ms", (unsigned long)total, (unsigned long)(millis() - t0));
  if (!c.dansCalendrier) return echec("ce n'est pas un agenda iCal");
  if (!c.finCalendrier) return echec("agenda incomplet (coupure ?)");
  return true;
}

}  // namespace

namespace Agenda {

void demarrer() {
  if (!s_ev) s_ev = (Evenement*)ps_calloc(EVENEMENTS_MAX, sizeof(Evenement));
  if (!s_ev) {
    JOURNAL("[agenda] ERREUR : PSRAM insuffisante");
    return;
  }
  s_nb = 0;
  if (!configure()) return;
  charger();
  JOURNAL("[agenda] %d événement(s) en mémoire", s_nb);
}

bool configure() { return Param.icalUrl[0] != 0; }

bool synchroniser() {
  s_essais++;
  r_dernierEssai = Horloge::maintenant();
  if (!configure()) return echec("aucune adresse d'agenda");
  if (WiFi.status() != WL_CONNECTED) return echec("pas de Wi-Fi");
  if (!Horloge::valide()) return echec("heure inconnue");
  if (!s_ev) return echec("mémoire insuffisante");
  if (!s_ctx) s_ctx = (Contexte*)ps_malloc(sizeof(Contexte));
  if (!s_ctx) return echec("mémoire insuffisante");

  char url[sizeof(Param.icalUrl) + 8];
  if (strncasecmp(Param.icalUrl, "webcal://", 9) == 0) snprintf(url, sizeof(url), "https://%s", Param.icalUrl + 9);
  else if (strncasecmp(Param.icalUrl, "https://", 8) == 0) copier(url, sizeof(url), Param.icalUrl);
  else return echec("l'adresse doit commencer par https://");

  Contexte& c = *s_ctx;
  c.nb = 0;
  c.nbRem = 0;
  c.lgLigne = 0;
  c.finLigne = false;
  c.dansCalendrier = c.finCalendrier = c.dansEvenement = false;
  c.sousNiveau = 0;
  const int auj = Horloge::aujourdhui();
  c.dateMin = Horloge::ajouterJours(auj, -JOURS_AVANT);
  c.dateMax = Horloge::ajouterJours(auj, JOURS_APRES);

  // La radio sans économie d'énergie pendant le téléchargement (bien plus rapide)
  const wifi_ps_type_t sommeil = WiFi.getSleep();
  WiFi.setSleep(false);
  const bool ok = telecharger(url, c);
  WiFi.setSleep(sommeil);
  if (!ok) return false;

  finaliser(c);
  for (int i = 0; i < c.nb; i++) s_ev[i] = c.ev[i].e;
  s_nb = c.nb;
  s_synchro = Horloge::maintenant();
  s_erreur[0] = 0;
  if (!sauver()) JOURNAL("[agenda] agenda.json non enregistré");
  JOURNAL("[agenda] %d événement(s) gardé(s) (J-%d à J+%d)", s_nb, JOURS_AVANT, JOURS_APRES);
  return true;
}

bool aSynchroniser() {
  if (!configure() || !Horloge::valide()) return false;
  const uint32_t m = Horloge::maintenant();
  if (r_dernierEssai && m >= r_dernierEssai && m - r_dernierEssai < PAUSE_APRES_ESSAI_S) return false;
  return s_synchro == 0 || m < s_synchro || m - s_synchro > SYNCHRO_TOUTES_S;
}

uint32_t derniereSynchro() { return s_synchro; }
const char* derniereErreur() { return tr(s_erreur); }   // glissée dans « Agenda : %s »
uint16_t numeroEssai() { return s_essais; }
int nbEvenements() { return s_nb; }

int evenementsDuJour(int date, Evenement* sortie, int max) {
  int n = 0;
  for (int i = 0; i < s_nb && n < max; i++) {
    if (s_ev[i].date == date) sortie[n++] = s_ev[i];
    else if (s_ev[i].date > date) break;   // liste triée
  }
  return n;
}

bool prochainEvenement(Evenement& e) {
  if (!s_nb || !Horloge::valide()) return false;
  struct tm t;
  Horloge::locale(t);
  const int auj = Horloge::dateDe(t);
  const int maintenant = t.tm_hour * 60 + t.tm_min;
  int journee = -1;   // événement « toute la journée » d'aujourd'hui (en cours)
  for (int i = 0; i < s_nb; i++) {
    const Evenement& ev = s_ev[i];
    if (ev.date < auj) continue;
    if (ev.date == auj) {
      if (ev.debut < 0) {
        if (journee < 0) journee = i;
        continue;
      }
      const int fin = ev.fin >= 0 ? ev.fin : ev.debut + 60;
      if (fin <= maintenant) continue;   // déjà fini
      e = ev;                            // en cours ou plus tard aujourd'hui
      return true;
    }
    e = journee >= 0 ? s_ev[journee] : ev;   // rien d'autre aujourd'hui
    return true;
  }
  if (journee < 0) return false;
  e = s_ev[journee];
  return true;
}

uint32_t joursAvecEvenements(int annee, int mois) {
  uint32_t bits = 0;
  for (int i = 0; i < s_nb; i++) {
    const int d = s_ev[i].date;
    if (d / 10000 == annee && (d / 100) % 100 == mois) bits |= (1UL << (d % 100));
  }
  return bits;
}

void effacer() {
  s_nb = 0;
  s_synchro = 0;
  s_erreur[0] = 0;
  r_dernierEssai = 0;
  if (Stockage::monte()) {
    if (FFat.exists(F_AGENDA)) FFat.remove(F_AGENDA);
    if (FFat.exists(F_AGENDA_TMP)) FFat.remove(F_AGENDA_TMP);
  }
}

}  // namespace Agenda
