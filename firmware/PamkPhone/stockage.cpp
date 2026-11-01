#include "stockage.h"
#include "horloge.h"
#include "stats.h"
#include <FFat.h>
#include <ArduinoJson.h>
#include <esp_random.h>

namespace {

// Les documents JSON vont en PSRAM (les notes peuvent peser plusieurs centaines de Kio).
struct AllocateurPSRAM : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return ps_malloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { return ps_realloc(p, n); }
};
AllocateurPSRAM g_alloc;

struct Trace {
  char id[ID_LONGUEUR + 1];
  uint32_t maj;
  char type;   // 't' tâche, 'n' note
};

const char* const F_TACHES = "/taches.json";
const char* const F_TACHES_TMP = "/taches.tmp";
const char* const F_NOTES = "/notes.json";
const char* const F_NOTES_TMP = "/notes.tmp";

// Répétitions dans le fichier (texte : l'ordre de l'enum peut changer sans casser les données).
const char* const REPETITIONS[] = {"aucune", "jour", "semaine", "mois", "jours_ouvres"};
const char* const TEXTES_REPETITION[] = {"jamais", "chaque jour", "chaque semaine", "chaque mois", "jours ouvrés"};
// Rappel en retard de plus de 12 h (téléphone resté éteint) : on ne l'affiche plus.
const uint32_t RAPPEL_RETARD_MAX_S = 12UL * 3600UL;

Tache* s_taches = nullptr;
int s_nbT = 0;
Note* s_notes = nullptr;
int s_nbN = 0;
Trace* s_traces = nullptr;
int s_nbTr = 0;
bool s_monte = false;

void copier(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t l = strlen(src);
  if (l > n - 1) {
    l = n - 1;
    while (l > 0 && (((uint8_t)src[l]) & 0xC0) == 0x80) l--;   // ne coupe pas un caractère UTF-8
  }
  memcpy(dst, src, l);
  dst[l] = 0;
}

void nouvelId(char* id) {
  snprintf(id, ID_LONGUEUR + 1, "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
}

void ajouterTrace(char type, const char* id, uint32_t maj) {
  if (!s_traces || !id || !*id) return;
  if (s_nbTr >= SUPPRIMES_MAX) {   // on oublie la plus ancienne
    memmove(&s_traces[0], &s_traces[1], sizeof(Trace) * (SUPPRIMES_MAX - 1));
    s_nbTr = SUPPRIMES_MAX - 1;
  }
  Trace& t = s_traces[s_nbTr++];
  copier(t.id, sizeof(t.id), id);
  t.maj = maj;
  t.type = type;
}

bool ouvrirLecture(const char* chemin, const char* tmp, File& f) {
  // Une écriture interrompue entre « supprimer » et « renommer » laisse le .tmp complet.
  if (!FFat.exists(chemin) && FFat.exists(tmp)) FFat.rename(tmp, chemin);
  if (!FFat.exists(chemin)) return false;
  f = FFat.open(chemin, "r");
  return (bool)f;
}

bool ecrireJson(JsonDocument& doc, const char* chemin, const char* tmp) {
  if (!s_monte) return false;
  File f = FFat.open(tmp, "w");
  if (!f) {
    JOURNAL("[stockage] impossible d'écrire %s", tmp);
    return false;
  }
  const size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) {
    FFat.remove(tmp);
    return false;
  }
  if (FFat.exists(chemin)) FFat.remove(chemin);
  if (!FFat.rename(tmp, chemin)) {
    JOURNAL("[stockage] renommage %s -> %s raté", tmp, chemin);
    return false;
  }
  return true;
}

void mettreDeCote(const char* chemin) {
  char nom[40];
  snprintf(nom, sizeof(nom), "%s.abime", chemin);
  if (FFat.exists(nom)) FFat.remove(nom);
  FFat.rename(chemin, nom);
  JOURNAL("[stockage] %s illisible, copie gardée dans %s", chemin, nom);
}

void ajouterTraces(JsonDocument& doc, char type) {
  JsonArray a = doc["supprimees"].to<JsonArray>();
  for (int i = 0; i < s_nbTr; i++) {
    if (s_traces[i].type != type) continue;
    JsonObject o = a.add<JsonObject>();
    o["id"] = (const char*)s_traces[i].id;
    o["maj"] = s_traces[i].maj;
  }
}

void lireTraces(JsonDocument& doc, char type) {
  for (JsonObject o : doc["supprimees"].as<JsonArray>()) {
    ajouterTrace(type, o["id"] | "", o["maj"] | 0UL);
  }
}

// --- Heure, répétition, rappel (28/10/2026) ---------------------------------
// Heure d'une tâche : "14:30" dans le fichier, minutes depuis minuit en mémoire.
void texteMinutes(int minutes, char* b, size_t n) {
  snprintf(b, n, "%02d:%02d", minutes / 60, minutes % 60);
}

int minutesDepuisTexte(const char* s) {
  if (!s) return -1;
  const char* p = strchr(s, ':');
  if (!p || p == s) return -1;
  const int h = atoi(s), m = atoi(p + 1);
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

uint8_t repetitionDepuisTexte(const char* s) {
  if (!s) return REP_AUCUNE;
  for (uint8_t r = REP_JOUR; r <= REP_JOURS_OUVRES; r++) {
    if (strcmp(s, REPETITIONS[r]) == 0) return r;
  }
  return REP_AUCUNE;
}

// Moment UTC d'une date et d'une heure LOCALES (fuseau de l'horloge, heure d'été comprise).
uint32_t utcLocal(int date, int minutes) {
  int a, m, j;
  Horloge::decomposer(date, a, m, j);
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = a - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = j;
  t.tm_hour = minutes / 60;
  t.tm_min = minutes % 60;
  t.tm_isdst = -1;   // laisse le fuseau décider de l'heure d'été (comme Horloge::reglerLocale)
  const time_t utc = mktime(&t);
  return utc > 0 ? (uint32_t)utc : 0;
}

// Date locale (AAAAMMJJ) d'un instant UTC.
int dateLocale(uint32_t utc) {
  struct tm t;
  Horloge::localeDe(utc, t);
  return Horloge::dateDe(t);
}

// Moment UTC du rappel d'une tâche (0 = aucun) : reporté s'il l'a été, sinon échéance + heure.
uint32_t moment(const Tache& t) {
  if (!t.rappel || t.faite || !t.echeance || t.heure < 0) return 0;
  if (t.rappelReporte) return t.rappelReporte;
  return utcLocal(t.echeance, t.heure);
}

// Après un changement (échéance, heure, rappel, tâche décochée ou répétée) : le report est
// oublié, un moment déjà passé compte comme vu (régler une heure passée ne sonne pas) et un
// moment à venir sera affiché.
void armerRappel(Tache& t) {
  t.rappelReporte = 0;
  const uint32_t m = moment(t);
  t.rappelVu = (m && m <= Horloge::maintenant()) ? m : 0;
}

// Prochaine occurrence d'une tâche répétée, après son échéance ET après aujourd'hui, au même
// rythme : une tâche quotidienne en retard de 3 jours cochée aujourd'hui revient demain, une
// tâche du lundi revient le lundi suivant. Mois : même jour, borné au dernier jour du mois.
int occurrenceSuivante(const Tache& t) {
  const int auj = Horloge::aujourdhui();
  const int e = t.echeance ? t.echeance : auj;
  const int base = e > auj ? e : auj;
  if (t.joursSemaine != 0) {
    int d = Horloge::ajouterJours(base, 1);
    for (int k = 0; k < 14; k++) {
      const int js = Horloge::jourSemaine(d);
      if (t.joursSemaine & (1 << js)) return d;
      d = Horloge::ajouterJours(d, 1);
    }
    return d;
  }
  switch (t.repetition) {
    case REP_JOUR:
      return Horloge::ajouterJours(base, 1);
    case REP_JOURS_OUVRES: {
      int d = Horloge::ajouterJours(base, 1);
      while (Horloge::jourSemaine(d) >= 5) d = Horloge::ajouterJours(d, 1);   // saute samedi et dimanche
      return d;
    }
    case REP_SEMAINE:
      return Horloge::ajouterJours(e, (Horloge::ecartJours(e, base) / 7 + 1) * 7);
    case REP_MOIS: {
      int a, m, j;
      Horloge::decomposer(e, a, m, j);
      const int jour = (t.jourMois >= 1 && t.jourMois <= 31) ? t.jourMois : j;
      int d = e;
      for (int k = 0; k < 1200 && d <= base; k++) {
        if (++m > 12) {
          m = 1;
          a++;
        }
        d = Horloge::composer(a, m, mini(jour, Horloge::joursDansMois(a, m)));
      }
      return d;
    }
    default:
      return e;
  }
}

bool sauverTaches() {
  JsonDocument doc(&g_alloc);
  doc["version"] = 1;
  doc["type"] = "pamk-taches";
  JsonArray a = doc["taches"].to<JsonArray>();
  for (int i = 0; i < s_nbT; i++) {
    const Tache& t = s_taches[i];
    JsonObject o = a.add<JsonObject>();
    o["id"] = (const char*)t.id;
    o["titre"] = (const char*)t.titre;
    o["faite"] = t.faite;
    if (t.echeance) {
      char iso[12];
      Horloge::texteISO(iso, sizeof(iso), t.echeance);
      o["echeance"] = (const char*)iso;
    } else {
      o["echeance"] = nullptr;
    }
    o["cree"] = t.cree;
    o["maj"] = t.maj;
    // 28/10/2026
    if (t.heure >= 0) {
      char h[12];
      texteMinutes(t.heure, h, sizeof(h));
      o["heure"] = (const char*)h;
    } else {
      o["heure"] = nullptr;
    }
    o["repetition"] = REPETITIONS[t.repetition <= REP_JOURS_OUVRES ? t.repetition : 0];
    o["rappel"] = t.rappel;
    // État propre au téléphone : écrit seulement s'il sert.
    if (t.rappelVu) o["rappelVu"] = t.rappelVu;
    if (t.rappelReporte) o["rappelReporte"] = t.rappelReporte;
    if (t.repetition == REP_MOIS && t.jourMois) o["jourMois"] = t.jourMois;
    if (t.description[0]) o["desc"] = (const char*)t.description;
    if (t.joursSemaine) o["jours"] = t.joursSemaine;
  }
  ajouterTraces(doc, 't');
  if (doc.overflowed()) {
    JOURNAL("[stockage] mémoire insuffisante pour les tâches");
    return false;
  }
  return ecrireJson(doc, F_TACHES, F_TACHES_TMP);
}

bool sauverNotes() {
  JsonDocument doc(&g_alloc);
  doc["version"] = 1;
  doc["type"] = "pamk-notes";
  JsonArray a = doc["notes"].to<JsonArray>();
  for (int i = 0; i < s_nbN; i++) {
    const Note& n = s_notes[i];
    JsonObject o = a.add<JsonObject>();
    o["id"] = (const char*)n.id;
    o["texte"] = (const char*)(n.texte ? n.texte : "");
    o["cree"] = n.cree;
    o["maj"] = n.maj;
    if (n.epinglee) o["epingle"] = true;
  }
  ajouterTraces(doc, 'n');
  if (doc.overflowed()) {
    JOURNAL("[stockage] mémoire insuffisante pour les notes");
    return false;
  }
  return ecrireJson(doc, F_NOTES, F_NOTES_TMP);
}

char* dupliquerTexte(const char* s) {
  if (!s) s = "";
  size_t l = strlen(s);
  if (l > (size_t)NOTE_TEXTE_MAX) {
    l = NOTE_TEXTE_MAX;
    while (l > 0 && (((uint8_t)s[l]) & 0xC0) == 0x80) l--;
  }
  char* d = (char*)ps_malloc(l + 1);
  if (!d) return nullptr;
  memcpy(d, s, l);
  d[l] = 0;
  return d;
}

// Notes : la plus récemment modifiée en premier.
void trierNotes() {
  for (int i = 1; i < s_nbN; i++) {
    Note n = s_notes[i];
    int j = i - 1;
    while (j >= 0 && s_notes[j].maj < n.maj) {
      s_notes[j + 1] = s_notes[j];
      j--;
    }
    s_notes[j + 1] = n;
  }
}

void chargerTaches() {
  File f;
  if (!ouvrirLecture(F_TACHES, F_TACHES_TMP, f)) return;
  JsonDocument doc(&g_alloc);
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    JOURNAL("[stockage] taches.json : %s", e.c_str());
    mettreDeCote(F_TACHES);
    return;
  }
  for (JsonObject o : doc["taches"].as<JsonArray>()) {
    if (s_nbT >= TACHES_MAX) break;
    const char* id = o["id"] | "";
    if (!*id) continue;
    Tache& t = s_taches[s_nbT];
    copier(t.id, sizeof(t.id), id);
    copier(t.titre, sizeof(t.titre), o["titre"] | "");
    t.faite = o["faite"] | false;
    t.echeance = Horloge::dateDepuisISO(o["echeance"] | "");
    t.cree = o["cree"] | 0UL;
    t.maj = o["maj"] | 0UL;
    // 28/10/2026 : absents des anciens fichiers -> sans heure, sans répétition, sans rappel.
    t.heure = (int16_t)minutesDepuisTexte(o["heure"] | "");
    t.repetition = repetitionDepuisTexte(o["repetition"] | "");
    t.rappel = (o["rappel"] | false) && t.echeance && t.heure >= 0;
    t.rappelVu = o["rappelVu"] | 0UL;
    t.rappelReporte = o["rappelReporte"] | 0UL;
    t.jourMois = (uint8_t)borner(o["jourMois"] | 0, 0, 31);
    copier(t.description, sizeof(t.description), o["desc"] | "");
    t.joursSemaine = (uint8_t)(o["jours"] | 0);
    s_nbT++;
  }
  lireTraces(doc, 't');
}

void chargerNotes() {
  File f;
  if (!ouvrirLecture(F_NOTES, F_NOTES_TMP, f)) return;
  JsonDocument doc(&g_alloc);
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    JOURNAL("[stockage] notes.json : %s", e.c_str());
    mettreDeCote(F_NOTES);
    return;
  }
  for (JsonObject o : doc["notes"].as<JsonArray>()) {
    if (s_nbN >= NOTES_MAX) break;
    const char* id = o["id"] | "";
    if (!*id) continue;
    Note& n = s_notes[s_nbN];
    copier(n.id, sizeof(n.id), id);
    n.texte = dupliquerTexte(o["texte"] | "");
    if (!n.texte) break;
    n.cree = o["cree"] | 0UL;
    n.maj = o["maj"] | 0UL;
    n.epinglee = o["epingle"] | false;
    s_nbN++;
  }
  lireTraces(doc, 'n');
  trierNotes();
}

// Ordre d'affichage des tâches à faire : échéance la plus proche, sans échéance à la fin ;
// le même jour, celles qui ont une heure d'abord (dans l'ordre), puis les autres par création.
bool avantAFaire(const Tache& a, const Tache& b) {
  const long ea = a.echeance ? a.echeance : 99999999L;
  const long eb = b.echeance ? b.echeance : 99999999L;
  if (ea != eb) return ea < eb;
  const bool ha = a.heure >= 0, hb = b.heure >= 0;
  if (ha != hb) return ha;
  if (ha && a.heure != b.heure) return a.heure < b.heure;
  return a.cree < b.cree;
}

bool avant(const Tache& a, const Tache& b) {
  if (a.faite != b.faite) return !a.faite;          // à faire d'abord
  if (!a.faite) return avantAFaire(a, b);
  return a.maj > b.maj;                              // faites : la plus récente d'abord
}

// Insertion triée bornée : la liste garde les « max » premières selon « ordre » (et pas les
// « max » premières du tableau). Retourne le nouveau nombre d'éléments.
int insererTrie(int* indices, int n, int max, int i, bool (*ordre)(const Tache&, const Tache&)) {
  if (max <= 0) return n;
  int j = n;
  if (n >= max) {   // liste pleine : n'entre que si elle passe avant la dernière
    if (!ordre(s_taches[i], s_taches[indices[max - 1]])) return n;
    j = max - 1;
  }
  while (j > 0 && ordre(s_taches[i], s_taches[indices[j - 1]])) {
    indices[j] = indices[j - 1];
    j--;
  }
  indices[j] = i;
  return n < max ? n + 1 : max;
}

int trouverNote(const char* id) {
  for (int i = 0; i < s_nbN; i++) if (strcmp(s_notes[i].id, id) == 0) return i;
  return -1;
}

}  // namespace

namespace Stockage {

bool demarrer() {
  s_taches = (Tache*)ps_calloc(TACHES_MAX, sizeof(Tache));
  s_notes = (Note*)ps_calloc(NOTES_MAX, sizeof(Note));
  s_traces = (Trace*)ps_calloc(SUPPRIMES_MAX, sizeof(Trace));
  if (!s_taches || !s_notes || !s_traces) {
    JOURNAL("[stockage] ERREUR : PSRAM insuffisante");
    return false;
  }
  // Partition « ffat » du schéma « 16M Flash (3MB APP/9.9MB FATFS) ».
  s_monte = FFat.begin(true);
  if (!s_monte) {
    JOURNAL("[stockage] ERREUR : FFat impossible à monter (schéma de partition ?)");
    return false;
  }
  chargerTaches();
  chargerNotes();
  JOURNAL("[stockage] %d tâche(s), %d note(s), %lu/%lu octets utilisés", s_nbT, s_nbN,
          (unsigned long)FFat.usedBytes(), (unsigned long)FFat.totalBytes());
  return true;
}

bool monte() { return s_monte; }

// --- Tâches ---------------------------------------------------------------
int nbTaches() { return s_nbT; }
const Tache* tache(int i) { return (i >= 0 && i < s_nbT) ? &s_taches[i] : nullptr; }

int indexTache(const char* id) {
  if (!id) return -1;
  for (int i = 0; i < s_nbT; i++) if (strcmp(s_taches[i].id, id) == 0) return i;
  return -1;
}

const Tache* ajouterTache(const char* titre, int echeance) {
  if (!s_taches || s_nbT >= TACHES_MAX || !titre || !*titre) return nullptr;
  Tache& t = s_taches[s_nbT];
  memset(&t, 0, sizeof(t));
  nouvelId(t.id);
  copier(t.titre, sizeof(t.titre), titre);
  t.echeance = echeance;
  t.faite = false;
  t.heure = -1;                       // le reste (répétition, rappel) est à zéro = aucun
  if (echeance) t.jourMois = (uint8_t)(echeance % 100);
  t.cree = t.maj = Horloge::maintenant();
  s_nbT++;
  sauverTaches();
  return &t;
}

bool renommerTache(const char* id, const char* titre) {
  const int i = indexTache(id);
  if (i < 0 || !titre || !*titre) return false;
  copier(s_taches[i].titre, sizeof(s_taches[i].titre), titre);
  s_taches[i].maj = Horloge::maintenant();
  return sauverTaches();
}

bool cocherTache(const char* id, bool faite) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  if (faite && !t.faite) {
    Stats::tacheFaite();
    if (t.repetition != REP_AUCUNE) {
      // Tâche répétée (comme Rappels) : elle reste à faire, son échéance passe à la
      // prochaine occurrence et son rappel est réarmé.
      t.echeance = occurrenceSuivante(t);
      armerRappel(t);
      t.maj = Horloge::maintenant();
      return sauverTaches();
    }
  } else if (!faite && t.faite) {
    // Les statistiques ne reprennent que les tâches cochées le jour même.
    if (dateLocale(t.maj) == Horloge::aujourdhui()) Stats::tacheDefaite();
    t.faite = false;
    armerRappel(t);   // un rappel passé pendant qu'elle était faite ne sonne pas
  }
  t.faite = faite;
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool echeanceTache(const char* id, int echeance) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  t.echeance = echeance;
  if (!echeance) {   // sans date : ni heure, ni rappel, ni répétition (comme Rappels)
    t.heure = -1;
    t.rappel = false;
    t.repetition = REP_AUCUNE;
  } else {
    t.jourMois = (uint8_t)(echeance % 100);
  }
  armerRappel(t);
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool descriptionTache(const char* id, const char* desc) {
  const int i = indexTache(id);
  if (i < 0) return false;
  copier(s_taches[i].description, sizeof(s_taches[i].description), desc ? desc : "");
  s_taches[i].maj = Horloge::maintenant();
  return sauverTaches();
}

bool joursSemaineTache(const char* id, uint8_t jours) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  t.joursSemaine = jours & 0x7F;
  if (t.joursSemaine != 0) {
    t.repetition = REP_SEMAINE;
    if (!t.echeance && Horloge::valide()) {
      int d = Horloge::aujourdhui();
      for (int k = 0; k < 7; k++) {
        int js = Horloge::jourSemaine(d);
        if (t.joursSemaine & (1 << js)) break;
        d = Horloge::ajouterJours(d, 1);
      }
      t.echeance = d;
    }
  }
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool supprimerTache(const char* id) {
  const int i = indexTache(id);
  if (i < 0) return false;
  ajouterTrace('t', s_taches[i].id, Horloge::maintenant());
  for (int j = i; j + 1 < s_nbT; j++) s_taches[j] = s_taches[j + 1];
  s_nbT--;
  return sauverTaches();
}

int listeTaches(FiltreTaches f, int* indices, int max) {
  int n = 0;
  for (int i = 0; i < s_nbT; i++) {
    const Tache& t = s_taches[i];
    if (f == TACHES_A_FAIRE && t.faite) continue;
    if (f == TACHES_FAITES && !t.faite) continue;
    n = insererTrie(indices, n, max, i, avant);
  }
  return n;
}

int tachesDuJour(int date, int* indices, int max) {
  int n = 0;
  for (int i = 0; i < s_nbT; i++) {
    if (s_taches[i].echeance != date) continue;
    n = insererTrie(indices, n, max, i, avant);
  }
  return n;
}

int nbTachesAFaire() {
  int n = 0;
  for (int i = 0; i < s_nbT; i++) if (!s_taches[i].faite) n++;
  return n;
}

int nbTachesFaites() { return s_nbT - nbTachesAFaire(); }

int nbTachesEnRetard() {
  const int auj = Horloge::aujourdhui();
  int n = 0;
  for (int i = 0; i < s_nbT; i++) {
    if (!s_taches[i].faite && s_taches[i].echeance && s_taches[i].echeance < auj) n++;
  }
  return n;
}

void joursAvecTaches(int annee, int mois, uint32_t& aFaire, uint32_t& faites) {
  aFaire = 0;
  faites = 0;
  for (int i = 0; i < s_nbT; i++) {
    const int e = s_taches[i].echeance;
    if (!e || e / 10000 != annee || (e / 100) % 100 != mois) continue;
    const int j = e % 100;
    if (s_taches[i].faite) faites |= (1UL << j);
    else aFaire |= (1UL << j);
  }
  faites &= ~aFaire;
}

// --- Heure, répétition, rappel (28/10/2026) ---------------------------------
bool heureTache(const char* id, int minutes) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  if (minutes < 0) {
    t.heure = -1;
    t.rappel = false;
  } else {
    t.heure = (int16_t)(minutes % 1440);
    if (!t.echeance && Horloge::valide()) {   // une heure sans date vaut pour aujourd'hui
      t.echeance = Horloge::aujourdhui();
      t.jourMois = (uint8_t)(t.echeance % 100);
    }
  }
  armerRappel(t);
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool repetitionTache(const char* id, uint8_t rep) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  t.repetition = rep <= REP_JOURS_OUVRES ? rep : (uint8_t)REP_AUCUNE;
  if (t.repetition != REP_AUCUNE && !t.echeance && Horloge::valide()) {
    t.echeance = Horloge::aujourdhui();
    armerRappel(t);
  }
  if (t.echeance) t.jourMois = (uint8_t)(t.echeance % 100);
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool rappelTache(const char* id, bool actif) {
  const int i = indexTache(id);
  if (i < 0) return false;
  Tache& t = s_taches[i];
  if (actif && (!t.echeance || t.heure < 0)) return false;
  t.rappel = actif;
  armerRappel(t);
  t.maj = Horloge::maintenant();
  return sauverTaches();
}

bool reporterRappel(const char* id, int minutes) {
  const int i = indexTache(id);
  if (i < 0 || minutes <= 0) return false;
  Tache& t = s_taches[i];
  if (!t.rappel || t.faite || !t.echeance || t.heure < 0) return false;
  // État propre au téléphone (comme rappelVu) : « maj » ne bouge pas.
  t.rappelReporte = Horloge::maintenant() + (uint32_t)minutes * 60UL;
  return sauverTaches();
}

uint32_t momentRappel(const Tache* t) { return t ? moment(*t) : 0; }

int tachesAujourdhui(int* indices, int max) {
  const int auj = Horloge::aujourdhui();
  int n = 0;
  for (int i = 0; i < s_nbT; i++) {
    const Tache& t = s_taches[i];
    if (t.faite || !t.echeance || t.echeance > auj) continue;
    n = insererTrie(indices, n, max, i, avantAFaire);   // en retard (la plus ancienne) puis du jour
  }
  return n;
}

uint32_t prochainRappel() {
  const uint32_t mtn = Horloge::maintenant();
  uint32_t p = 0;
  for (int i = 0; i < s_nbT; i++) {
    const uint32_t m = moment(s_taches[i]);
    if (!m || m <= s_taches[i].rappelVu) continue;          // aucun, ou déjà affiché
    if (m + RAPPEL_RETARD_MAX_S < mtn) continue;            // trop tard : ne sera plus affiché
    if (!p || m < p) p = m;
  }
  return p;
}

bool rappelArrive(uint32_t maintenant, char* idSortie, size_t taille) {
  int choisi = -1;
  uint32_t mChoisi = 0;
  bool aSauver = false;
  for (int i = 0; i < s_nbT; i++) {
    Tache& t = s_taches[i];
    const uint32_t m = moment(t);
    if (!m || m <= t.rappelVu || m > maintenant) continue;
    if (maintenant - m > RAPPEL_RETARD_MAX_S) {   // téléphone resté éteint : trop tard, on l'oublie
      t.rappelVu = m;
      aSauver = true;
      continue;
    }
    if (choisi < 0 || m < mChoisi) {
      choisi = i;
      mChoisi = m;
    }
  }
  if (choisi >= 0) {
    s_taches[choisi].rappelVu = mChoisi;   // affiché une seule fois
    if (idSortie && taille) copier(idSortie, taille, s_taches[choisi].id);
    aSauver = true;
  }
  if (aSauver) sauverTaches();   // gardé en flash : pas de doublon après le sommeil profond
  return choisi >= 0;
}

void texteRepetition(uint8_t rep, char* sortie, size_t taille) {
  if (!sortie || !taille) return;
  copier(sortie, taille, TEXTES_REPETITION[rep <= REP_JOURS_OUVRES ? rep : 0]);
}

// --- Notes -----------------------------------------------------------------
int nbNotes() { return s_nbN; }
const Note* note(int i) { return (i >= 0 && i < s_nbN) ? &s_notes[i] : nullptr; }
int indexNote(const char* id) { return id ? trouverNote(id) : -1; }

const Note* ajouterNote(const char* texte) {
  if (!s_notes || s_nbN >= NOTES_MAX) return nullptr;
  char* t = dupliquerTexte(texte);
  if (!t) return nullptr;
  memmove(&s_notes[1], &s_notes[0], sizeof(Note) * s_nbN);   // en tête de liste
  Note& n = s_notes[0];
  memset(&n, 0, sizeof(n));
  nouvelId(n.id);
  n.texte = t;
  n.cree = n.maj = Horloge::maintenant();
  s_nbN++;
  sauverNotes();
  return &s_notes[0];
}

bool modifierNote(const char* id, const char* texte) {
  const int i = trouverNote(id);
  if (i < 0) return false;
  char* t = dupliquerTexte(texte);
  if (!t) return false;
  free(s_notes[i].texte);
  s_notes[i].texte = t;
  s_notes[i].maj = Horloge::maintenant();
  trierNotes();
  return sauverNotes();
}

bool supprimerNote(const char* id) {
  const int i = trouverNote(id);
  if (i < 0) return false;
  ajouterTrace('n', s_notes[i].id, Horloge::maintenant());
  free(s_notes[i].texte);
  memmove(&s_notes[i], &s_notes[i + 1], sizeof(Note) * (s_nbN - i - 1));
  s_nbN--;
  return sauverNotes();
}

void titreNote(const Note* n, char* sortie, size_t taille) {
  sortie[0] = 0;
  if (!n || !n->texte) return;
  const char* p = n->texte;
  while (*p == '\n' || *p == ' ') p++;
  const char* fin = strchr(p, '\n');
  size_t l = fin ? (size_t)(fin - p) : strlen(p);
  if (l > taille - 1) {
    l = taille - 1;
    while (l > 0 && (((uint8_t)p[l]) & 0xC0) == 0x80) l--;
  }
  memcpy(sortie, p, l);
  sortie[l] = 0;
}

// État propre au téléphone (comme rappelVu) : « maj » ne bouge pas, l'ordre de la liste non plus.
bool epinglerNote(const char* id, bool oui) {
  const int i = trouverNote(id);
  if (i < 0) return false;
  if (s_notes[i].epinglee == oui) return true;
  s_notes[i].epinglee = oui;
  return sauverNotes();
}

int notesEpinglees(int* indices, int max) {
  int n = 0;
  for (int i = 0; i < s_nbN && n < max; i++) {
    if (s_notes[i].epinglee) indices[n++] = i;
  }
  return n;
}

// --- Divers ----------------------------------------------------------------
int nbTraces() { return s_nbTr; }

void toutEffacer() {
  for (int i = 0; i < s_nbN; i++) free(s_notes[i].texte);
  s_nbT = 0;
  s_nbN = 0;
  s_nbTr = 0;
  if (s_monte) {
    const char* fichiers[] = {F_TACHES, F_TACHES_TMP, F_NOTES, F_NOTES_TMP};
    for (const char* f : fichiers) if (FFat.exists(f)) FFat.remove(f);
  }
}

uint32_t octetsTotal() { return s_monte ? (uint32_t)FFat.totalBytes() : 0; }
uint32_t octetsUtilises() { return s_monte ? (uint32_t)FFat.usedBytes() : 0; }

void exporter(Print& sortie) {
  JsonDocument doc(&g_alloc);
  doc["type"] = "pamk-phone-export";
  doc["version"] = 1;
  JsonArray ta = doc["taches"].to<JsonArray>();
  for (int i = 0; i < s_nbT; i++) {
    JsonObject o = ta.add<JsonObject>();
    o["id"] = (const char*)s_taches[i].id;
    o["titre"] = (const char*)s_taches[i].titre;
    o["faite"] = s_taches[i].faite;
    o["echeance"] = s_taches[i].echeance;
    if (s_taches[i].heure >= 0) {
      char h[12];
      texteMinutes(s_taches[i].heure, h, sizeof(h));
      o["heure"] = (const char*)h;
    }
    if (s_taches[i].repetition != REP_AUCUNE) {
      o["repetition"] = REPETITIONS[s_taches[i].repetition <= REP_JOURS_OUVRES ? s_taches[i].repetition : 0];
    }
    if (s_taches[i].rappel) o["rappel"] = true;
    if (s_taches[i].description[0]) o["desc"] = (const char*)s_taches[i].description;
    if (s_taches[i].joursSemaine) o["jours"] = s_taches[i].joursSemaine;   // bits 0..6 = lundi..dimanche
    o["maj"] = s_taches[i].maj;
  }
  JsonArray na = doc["notes"].to<JsonArray>();
  for (int i = 0; i < s_nbN; i++) {
    JsonObject o = na.add<JsonObject>();
    o["id"] = (const char*)s_notes[i].id;
    o["texte"] = (const char*)s_notes[i].texte;
    o["maj"] = s_notes[i].maj;
    if (s_notes[i].epinglee) o["epingle"] = true;
  }
  serializeJsonPretty(doc, sortie);
  sortie.println();
}

void listerFichiers(Print& sortie) {
  if (!s_monte) {
    sortie.println("FFat non monté");
    return;
  }
  File racine = FFat.open("/");
  if (!racine) return;
  File f = racine.openNextFile();
  while (f) {
    sortie.printf("  %-24s %8lu octets\r\n", f.name(), (unsigned long)f.size());
    f = racine.openNextFile();
  }
  sortie.printf("  utilisé %lu / %lu octets\r\n", (unsigned long)FFat.usedBytes(), (unsigned long)FFat.totalBytes());
}

}  // namespace Stockage
