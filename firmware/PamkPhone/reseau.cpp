#include "reseau.h"
#include "horloge.h"
#include "parametres.h"
#include "agenda.h"
#include "meteo.h"
#include "langue.h"
#include <WiFi.h>
#include <Preferences.h>

namespace Reseau {

struct Resultat {
  char ssid[33];
  int rssi;
  bool ouvert;
};

static const int SCAN_MAX = 24;
static const uint32_t DELAI_RECHERCHE_MS = 15000;
static const uint32_t DELAI_CONNEXION_MS = 20000;
static const uint32_t DELAI_NTP_MS = 45000;

static Resultat s_scan[SCAN_MAX];
static int s_nbScan = 0;
static EtatReseau s_etat = RESEAU_ETEINT;
static EtatReseau s_etatAvantScan = RESEAU_ETEINT;
static uint32_t s_t0 = 0;
static bool s_pourHeure = false;
static bool s_memoriser = false;
static char s_ssid[33] = "";
static char s_mdp[65] = "";
static char s_texte[80] = "Éteint";
static uint32_t s_synchroAvant = 0;
static bool s_garde = false;          // Accès depuis l'ordinateur : rester connecté
static bool s_agendaDemande = false;  // mise à jour de l'agenda à faire dès que possible
static bool s_agendaFait = false;     // agenda déjà tenté pendant cette connexion
static bool s_meteoFaite = false;

static void copier(char* d, size_t n, const char* s) {
  strncpy(d, s ? s : "", n - 1);
  d[n - 1] = 0;
}

static void allumer() {
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.persistent(false);   // les identifiants sont gérés ici, pas par la pile Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(!s_garde);  // économie d'énergie de la radio, sauf pour l'accès depuis l'ordinateur
  }
}

static void radioOff() {
  WiFi.scanDelete();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void demarrer() {
  s_etat = RESEAU_ETEINT;
  copier(s_texte, sizeof(s_texte), "Éteint");
}

EtatReseau etat() { return s_etat; }
bool occupe() { return s_etat == RESEAU_RECHERCHE || s_etat == RESEAU_CONNEXION || s_etat == RESEAU_SCAN; }
bool actif() { return WiFi.getMode() != WIFI_OFF; }
const char* texteEtat() { return tr(s_texte); }   // glissé dans « Agenda : %s »
const char* ssidActuel() { return s_ssid; }

int barres() {
  if (s_etat != RESEAU_CONNECTE) return 0;
  const int r = WiFi.RSSI();
  if (r > -55) return 4;
  if (r > -65) return 3;
  if (r > -75) return 2;
  if (r > -85) return 1;
  return 0;
}

// --- Réseaux connus (NVS) ---------------------------------------------------
int nbConnus() {
  Preferences p;
  if (!p.begin("wifi", true)) return 0;
  const int n = p.getUChar("n", 0);
  p.end();
  return mini(n, WIFI_RESEAUX_MAX);
}

static bool lireConnu(int i, char* ssid, size_t ns, char* mdp, size_t nm) {
  Preferences p;
  if (!p.begin("wifi", true)) return false;
  char cle[4] = {'s', (char)('0' + i), 0, 0};
  String s = p.getString(cle, "");
  cle[0] = 'p';
  String m = p.getString(cle, "");
  p.end();
  if (ssid) copier(ssid, ns, s.c_str());
  if (mdp) copier(mdp, nm, m.c_str());
  return s.length() > 0;
}

void ssidConnu(int i, char* sortie, size_t taille) {
  if (!lireConnu(i, sortie, taille, nullptr, 0)) sortie[0] = 0;
}

bool estConnu(const char* ssid) {
  char s[33];
  const int n = nbConnus();
  for (int i = 0; i < n; i++) {
    ssidConnu(i, s, sizeof(s));
    if (strcmp(s, ssid) == 0) return true;
  }
  return false;
}

static void ecrireListe(char ssids[][33], char mdps[][65], int n) {
  Preferences p;
  if (!p.begin("wifi", false)) return;
  p.clear();
  p.putUChar("n", (uint8_t)n);
  for (int i = 0; i < n; i++) {
    char cle[4] = {'s', (char)('0' + i), 0, 0};
    p.putString(cle, ssids[i]);
    cle[0] = 'p';
    p.putString(cle, mdps[i]);
  }
  p.end();
}

static void memoriser(const char* ssid, const char* mdp) {
  char ssids[WIFI_RESEAUX_MAX][33];
  char mdps[WIFI_RESEAUX_MAX][65];
  int n = nbConnus();
  for (int i = 0; i < n; i++) lireConnu(i, ssids[i], 33, mdps[i], 65);
  int place = -1;
  for (int i = 0; i < n; i++) if (strcmp(ssids[i], ssid) == 0) place = i;
  if (place < 0) {
    if (n == WIFI_RESEAUX_MAX) {   // liste pleine : on oublie le plus ancien
      for (int i = 0; i + 1 < n; i++) {
        memcpy(ssids[i], ssids[i + 1], 33);
        memcpy(mdps[i], mdps[i + 1], 65);
      }
      n--;
    }
    place = n++;
  }
  copier(ssids[place], 33, ssid);
  copier(mdps[place], 65, mdp);
  ecrireListe(ssids, mdps, n);
}

void oublier(int index) {
  char ssids[WIFI_RESEAUX_MAX][33];
  char mdps[WIFI_RESEAUX_MAX][65];
  const int n = nbConnus();
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (i == index) continue;
    lireConnu(i, ssids[k], 33, mdps[k], 65);
    k++;
  }
  ecrireListe(ssids, mdps, k);
}

// --- Actions ------------------------------------------------------------------
static void echec(const char* message) {
  radioOff();
  s_agendaDemande = false;
  s_etat = RESEAU_ECHEC;
  copier(s_texte, sizeof(s_texte), message);
  JOURNAL("[wifi] %s", message);
}

void eteindre() {
  radioOff();
  s_agendaDemande = false;
  s_etat = RESEAU_ETEINT;
  copier(s_texte, sizeof(s_texte), "Éteint");
}

// Cherche les réseaux connus à portée ; boucle() se connecte ensuite au meilleur.
static bool lancerRecherche() {
  if (nbConnus() == 0) {
    copier(s_texte, sizeof(s_texte), "Aucun réseau enregistré");
    return false;
  }
  allumer();
  WiFi.scanNetworks(true);
  s_etat = RESEAU_RECHERCHE;
  s_t0 = millis();
  s_pourHeure = Param.wifiMode != 2 && !s_garde;
  s_memoriser = false;
  copier(s_texte, sizeof(s_texte), "Recherche des réseaux…");
  return true;
}

void synchroniserHeure() {
  if (occupe() || s_etat == RESEAU_CONNECTE) return;
  lancerRecherche();
}

// --- Accès depuis l'ordinateur et agenda (28/10/2026) ----------------------------
bool garde() { return s_garde; }
bool agendaEnCours() { return s_agendaDemande; }

void garderConnecte() {
  s_garde = true;
  s_pourHeure = false;
  if (actif()) WiFi.setSleep(false);
  if (s_etat == RESEAU_CONNECTE || s_etat == RESEAU_RECHERCHE || s_etat == RESEAU_CONNEXION) return;
  if (s_etat == RESEAU_SCAN) return;   // scan de Réglages en cours : Acces réessaiera
  lancerRecherche();
}

void liberer() {
  if (!s_garde) return;
  s_garde = false;
  if (actif()) WiFi.setSleep(true);
  if (Param.wifiMode == 2) return;     // « toujours » : on reste connecté
  if (s_agendaDemande && (s_etat == RESEAU_RECHERCHE || s_etat == RESEAU_CONNEXION || s_etat == RESEAU_CONNECTE)) {
    s_pourHeure = true;                // l'agenda d'abord, puis extinction comme après la mise à l'heure
    return;
  }
  if (actif()) eteindre();
}

void mettreAJourAgenda() {
  if (!Agenda::configure() || s_etat == RESEAU_SCAN) return;
  s_agendaDemande = true;
  if (s_etat == RESEAU_CONNECTE || s_etat == RESEAU_RECHERCHE || s_etat == RESEAU_CONNEXION) return;
  if (!lancerRecherche()) s_agendaDemande = false;
}

// Bloquant (quelques secondes) : téléchargement et lecture du fichier iCal.
static void synchroniserAgenda() {
  s_agendaDemande = false;
  s_agendaFait = true;
  Agenda::synchroniser();
}

void connecter(const char* ssid, const char* mdp, bool memoriserReseau) {
  allumer();
  WiFi.disconnect();
  copier(s_ssid, sizeof(s_ssid), ssid);
  copier(s_mdp, sizeof(s_mdp), mdp);
  if (s_mdp[0]) WiFi.begin(s_ssid, s_mdp);
  else WiFi.begin(s_ssid);
  s_etat = RESEAU_CONNEXION;
  s_t0 = millis();
  s_memoriser = memoriserReseau;
  s_pourHeure = Param.wifiMode != 2 && !s_garde;
  snprintf(s_texte, sizeof(s_texte), TR("Connexion à %s…"), s_ssid);
}

bool lancerScan() {
  if (s_etat == RESEAU_RECHERCHE || s_etat == RESEAU_CONNEXION || s_etat == RESEAU_SCAN) return false;
  allumer();
  s_etatAvantScan = s_etat == RESEAU_CONNECTE ? RESEAU_CONNECTE : RESEAU_ETEINT;
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    echec("Scan impossible");
    return false;
  }
  s_nbScan = 0;
  s_etat = RESEAU_SCAN;
  s_t0 = millis();
  copier(s_texte, sizeof(s_texte), "Scan en cours…");
  return true;
}

bool scanFini() { return s_etat != RESEAU_SCAN; }
int nbScan() { return s_nbScan; }
const char* scanSsid(int i) { return (i >= 0 && i < s_nbScan) ? s_scan[i].ssid : ""; }
int scanRssi(int i) { return (i >= 0 && i < s_nbScan) ? s_scan[i].rssi : -100; }
bool scanOuvert(int i) { return (i >= 0 && i < s_nbScan) ? s_scan[i].ouvert : false; }

static void copierResultats(int n) {
  s_nbScan = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;   // réseau caché
    const int r = WiFi.RSSI(i);
    int dejaVu = -1;
    for (int k = 0; k < s_nbScan; k++) if (strcmp(s_scan[k].ssid, s.c_str()) == 0) dejaVu = k;
    if (dejaVu >= 0) {
      if (r > s_scan[dejaVu].rssi) s_scan[dejaVu].rssi = r;
      continue;
    }
    if (s_nbScan >= SCAN_MAX) continue;
    Resultat& res = s_scan[s_nbScan++];
    copier(res.ssid, sizeof(res.ssid), s.c_str());
    res.rssi = r;
    res.ouvert = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  // tri par signal décroissant
  for (int i = 1; i < s_nbScan; i++) {
    Resultat x = s_scan[i];
    int j = i - 1;
    while (j >= 0 && s_scan[j].rssi < x.rssi) { s_scan[j + 1] = s_scan[j]; j--; }
    s_scan[j + 1] = x;
  }
  WiFi.scanDelete();
}

void boucle() {
  const uint32_t m = millis();
  switch (s_etat) {
    case RESEAU_RECHERCHE: {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        if (m - s_t0 > DELAI_RECHERCHE_MS) echec("Recherche trop longue");
        break;
      }
      if (n < 0) { echec("Scan impossible"); break; }
      copierResultats(n);
      // Le réseau connu au meilleur signal
      for (int i = 0; i < s_nbScan; i++) {
        const int k = nbConnus();
        for (int j = 0; j < k; j++) {
          char ssid[33], mdp[65];
          if (lireConnu(j, ssid, sizeof(ssid), mdp, sizeof(mdp)) && strcmp(ssid, s_scan[i].ssid) == 0) {
            const bool pourHeure = s_pourHeure;
            connecter(ssid, mdp, false);
            s_pourHeure = pourHeure;
            return;
          }
        }
      }
      echec("Aucun réseau connu à portée");
      break;
    }
    case RESEAU_CONNEXION: {
      const wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        s_etat = RESEAU_CONNECTE;
        s_t0 = m;
        snprintf(s_texte, sizeof(s_texte), TR("Connecté à %s"), s_ssid);
        JOURNAL("[wifi] %s, IP %s", s_texte, WiFi.localIP().toString().c_str());
        if (s_memoriser) memoriser(s_ssid, s_mdp);
        s_synchroAvant = Horloge::derniereSynchro();
        s_agendaFait = false;
        s_meteoFaite = false;
        Horloge::lancerNTP(Param.ntp);
      } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || m - s_t0 > DELAI_CONNEXION_MS) {
        char msg[80];
        snprintf(msg, sizeof(msg), TR("Échec de connexion à %s"), s_ssid);
        echec(msg);
      }
      break;
    }
    case RESEAU_CONNECTE: {
      if (WiFi.status() != WL_CONNECTED) {
        echec("Connexion perdue");
        break;
      }
      const bool heureRecue = Horloge::derniereSynchro() != s_synchroAvant;
      // Agenda : demandé (dès que l'heure est crédible, il en faut une pour la fenêtre J-1..J+60),
      // ou une fois par connexion après une mise à l'heure réussie s'il date de plus de 3 h.
      if (s_agendaDemande && Horloge::valide()) {
        synchroniserAgenda();
      } else if (heureRecue && !s_agendaFait && Agenda::aSynchroniser()) {
        synchroniserAgenda();
      }
      if (!s_meteoFaite && (heureRecue || !Meteo::disponible() || (Horloge::valide() && Horloge::maintenant() - Meteo::donnees().majUtc > 7200UL))) {
        s_meteoFaite = true;
        Meteo::synchroniser();
      }
      if (s_agendaDemande && millis() - s_t0 > DELAI_NTP_MS) s_agendaDemande = false;   // jamais d'heure : abandon
      if (s_pourHeure && !s_garde && !s_agendaDemande && (heureRecue || millis() - s_t0 > DELAI_NTP_MS)) {
        eteindre();
        copier(s_texte, sizeof(s_texte), heureRecue ? "Heure synchronisée" : "Pas de réponse NTP");
      }
      break;
    }
    case RESEAU_SCAN: {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        if (m - s_t0 > DELAI_RECHERCHE_MS) echec("Scan trop long");
        break;
      }
      if (n >= 0) copierResultats(n);
      if (s_etatAvantScan == RESEAU_CONNECTE && WiFi.status() == WL_CONNECTED) {
        s_etat = RESEAU_CONNECTE;
      } else {
        radioOff();
        s_etat = RESEAU_ETEINT;
      }
      snprintf(s_texte, sizeof(s_texte), TR("%d réseau(x) trouvé(s)"), s_nbScan);
      break;
    }
    default:
      break;
  }
}

}  // namespace Reseau
