#include "stockage.h"
#include "horloge.h"
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
    s_nbN++;
  }
  lireTraces(doc, 'n');
  trierNotes();
}

// Ordre d'affichage des tâches à faire : échéance la plus proche, sans échéance à la fin.
bool avantAFaire(const Tache& a, const Tache& b) {
  const long ea = a.echeance ? a.echeance : 99999999L;
  const long eb = b.echeance ? b.echeance : 99999999L;
  if (ea != eb) return ea < eb;
  return a.cree < b.cree;
}

bool avant(const Tache& a, const Tache& b) {
  if (a.faite != b.faite) return !a.faite;          // à faire d'abord
  if (!a.faite) return avantAFaire(a, b);
  return a.maj > b.maj;                              // faites : la plus récente d'abord
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
  s_taches[i].faite = faite;
  s_taches[i].maj = Horloge::maintenant();
  return sauverTaches();
}

bool echeanceTache(const char* id, int echeance) {
  const int i = indexTache(id);
  if (i < 0) return false;
  s_taches[i].echeance = echeance;
  s_taches[i].maj = Horloge::maintenant();
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
  for (int i = 0; i < s_nbT && n < max; i++) {
    const Tache& t = s_taches[i];
    if (f == TACHES_A_FAIRE && t.faite) continue;
    if (f == TACHES_FAITES && !t.faite) continue;
    // insertion triée
    int j = n - 1;
    while (j >= 0 && avant(t, s_taches[indices[j]])) {
      indices[j + 1] = indices[j];
      j--;
    }
    indices[j + 1] = i;
    n++;
  }
  return n;
}

int tachesDuJour(int date, int* indices, int max) {
  int n = 0;
  for (int i = 0; i < s_nbT && n < max; i++) {
    if (s_taches[i].echeance != date) continue;
    int j = n - 1;
    while (j >= 0 && avant(s_taches[i], s_taches[indices[j]])) {
      indices[j + 1] = indices[j];
      j--;
    }
    indices[j + 1] = i;
    n++;
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
    o["maj"] = s_taches[i].maj;
  }
  JsonArray na = doc["notes"].to<JsonArray>();
  for (int i = 0; i < s_nbN; i++) {
    JsonObject o = na.add<JsonObject>();
    o["id"] = (const char*)s_notes[i].id;
    o["texte"] = (const char*)s_notes[i].texte;
    o["maj"] = s_notes[i].maj;
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
