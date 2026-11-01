#include "meteo.h"
#include "langue.h"
#include "dessin.h"
#include "horloge.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace Meteo {

// En mémoire RTC pour survivre à la veille
static RTC_DATA_ATTR VilleMeteo r_villes[MAX_VILLES] = {
  {"Clermont-Ferrand (63)", 45.75f, 3.10f, {0, 0, 0, 0, 0, false}},
  {"Paris", 48.85f, 2.35f, {0, 0, 0, 0, 0, false}},
};
static RTC_DATA_ATTR int r_nbVilles = 2;
static RTC_DATA_ATTR int r_villeActive = 0;

// Les villes sont aussi gardées en NVS (Preferences, espace « meteo ») : la mémoire RTC est
// remise à zéro par un reset, un nouveau firmware ou une batterie débranchée. La RTC ne sert
// plus que de cache des dernières mesures pendant la veille.
static bool s_villesLues = false;

static void sauverVilles() {
  Preferences p;
  if (!p.begin("meteo", false)) return;
  p.putUChar("nb", (uint8_t)r_nbVilles);
  p.putUChar("active", (uint8_t)r_villeActive);
  char cle[8];
  for (int i = 0; i < r_nbVilles; i++) {
    snprintf(cle, sizeof(cle), "nom%d", i);
    p.putString(cle, r_villes[i].nom);
    snprintf(cle, sizeof(cle), "lat%d", i);
    p.putFloat(cle, r_villes[i].lat);
    snprintf(cle, sizeof(cle), "lon%d", i);
    p.putFloat(cle, r_villes[i].lon);
  }
  p.end();
}

static void lireVilles() {
  s_villesLues = true;
  Preferences p;
  if (!p.begin("meteo", true)) return;   // jamais enregistrées : villes par défaut
  const int nb = p.getUChar("nb", 0);
  if (nb >= 1 && nb <= MAX_VILLES) {
    char cle[8];
    for (int i = 0; i < nb; i++) {
      VilleMeteo& v = r_villes[i];
      char nom[sizeof(v.nom)];
      snprintf(cle, sizeof(cle), "nom%d", i);
      String t = p.getString(cle, "");
      strncpy(nom, t.c_str(), sizeof(nom) - 1);
      nom[sizeof(nom) - 1] = 0;
      snprintf(cle, sizeof(cle), "lat%d", i);
      const float lat = p.getFloat(cle, 0.0f);
      snprintf(cle, sizeof(cle), "lon%d", i);
      const float lon = p.getFloat(cle, 0.0f);
      // Même ville qu'en mémoire RTC (réveil de veille) : on garde ses mesures.
      if (strcmp(v.nom, nom) != 0 || v.lat != lat || v.lon != lon) {
        memcpy(v.nom, nom, sizeof(v.nom));
        v.lat = lat;
        v.lon = lon;
        v.donnees.valide = false;
      }
    }
    r_nbVilles = nb;
    r_villeActive = p.getUChar("active", 0);
  }
  p.end();
}

int nbVilles() {
  if (!s_villesLues) lireVilles();
  if (r_nbVilles <= 0 || r_nbVilles > MAX_VILLES) {
    r_nbVilles = 1;
    strncpy(r_villes[0].nom, "Clermont-Ferrand (63)", sizeof(r_villes[0].nom) - 1);
    r_villes[0].lat = 45.75f;
    r_villes[0].lon = 3.10f;
    r_villes[0].donnees.valide = false;
  }
  return r_nbVilles;
}

int villeActive() {
  nbVilles();
  if (r_villeActive < 0 || r_villeActive >= r_nbVilles) r_villeActive = 0;
  return r_villeActive;
}

void selectionnerVille(int index) {
  if (index >= 0 && index < nbVilles()) {
    r_villeActive = index;
    sauverVilles();
  }
}

bool villeSuivante() {
  const int n = nbVilles();
  if (n <= 1) return false;
  r_villeActive = (r_villeActive + 1) % n;
  sauverVilles();
  return true;
}

bool villePrecedente() {
  const int n = nbVilles();
  if (n <= 1) return false;
  r_villeActive = (r_villeActive - 1 + n) % n;
  sauverVilles();
  return true;
}

const VilleMeteo& ville(int index) {
  nbVilles();
  if (index < 0 || index >= r_nbVilles) index = villeActive();
  return r_villes[index];
}

const char* lieu() { return ville(villeActive()).nom; }
float latitude() { return ville(villeActive()).lat; }
float longitude() { return ville(villeActive()).lon; }

bool disponible() { return ville(villeActive()).donnees.valide; }
const DonneesMeteo& donnees() { return ville(villeActive()).donnees; }
int16_t temperature() { return ville(villeActive()).donnees.temperature; }
int16_t tempMin() { return ville(villeActive()).donnees.tempMin; }
int16_t tempMax() { return ville(villeActive()).donnees.tempMax; }
uint8_t codeWmo() { return ville(villeActive()).donnees.codeWmo; }

bool ajouterVille(const char* nom, float lat, float lon) {
  if (!nom || !*nom) return false;
  nbVilles();
  if (r_nbVilles >= MAX_VILLES) return false;
  strncpy(r_villes[r_nbVilles].nom, nom, sizeof(r_villes[r_nbVilles].nom) - 1);
  r_villes[r_nbVilles].nom[sizeof(r_villes[r_nbVilles].nom) - 1] = 0;
  r_villes[r_nbVilles].lat = lat;
  r_villes[r_nbVilles].lon = lon;
  r_villes[r_nbVilles].donnees.valide = false;
  r_villeActive = r_nbVilles;
  r_nbVilles++;
  sauverVilles();
  return true;
}

bool supprimerVille(int index) {
  nbVilles();
  if (r_nbVilles <= 1 || index < 0 || index >= r_nbVilles) return false;
  for (int i = index; i < r_nbVilles - 1; i++) {
    r_villes[i] = r_villes[i + 1];
  }
  r_nbVilles--;
  if (r_villeActive >= r_nbVilles) r_villeActive = r_nbVilles - 1;
  sauverVilles();
  return true;
}

struct VilleConnue { const char* nom; float lat; float lon; };
static const VilleConnue VILLES_CONNUES[] = {
  {"Clermont-Ferrand", 45.7772f, 3.0870f},
  {"Paris", 48.8566f, 2.3522f},
  {"Lyon", 45.7640f, 4.8357f},
  {"Marseille", 43.2965f, 5.3698f},
  {"Toulouse", 43.6047f, 1.4442f},
  {"Nice", 43.7102f, 7.2620f},
  {"Nantes", 47.2184f, -1.5536f},
  {"Strasbourg", 48.5734f, 7.7521f},
  {"Montpellier", 43.6108f, 3.8767f},
  {"Bordeaux", 44.8378f, -0.5792f},
  {"Lille", 50.6292f, 3.0573f},
  {"Rennes", 48.1173f, -1.6778f},
  {"Grenoble", 45.1885f, 5.7245f},
  {"Toulon", 43.1242f, 5.9280f},
  {"Angers", 47.4784f, -0.5632f},
  {"Dijon", 47.3220f, 5.0415f},
  {"Brest", 48.3904f, -4.4861f},
  {"Le Mans", 48.0061f, 0.1996f},
  {"Aix-en-Provence", 43.5297f, 5.4474f},
};

bool rechercherVille(const char* nom, char* nomTrouve, size_t maxNom, float& lat, float& lon) {
  if (!nom || !*nom) return false;

  for (size_t i = 0; i < sizeof(VILLES_CONNUES) / sizeof(VILLES_CONNUES[0]); i++) {
    if (strcasestr(VILLES_CONNUES[i].nom, nom) != nullptr || strcasestr(nom, VILLES_CONNUES[i].nom) != nullptr) {
      strncpy(nomTrouve, VILLES_CONNUES[i].nom, maxNom - 1);
      nomTrouve[maxNom - 1] = 0;
      lat = VILLES_CONNUES[i].lat;
      lon = VILLES_CONNUES[i].lon;
      return true;
    }
  }

  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(4000);   // court : l'interface attend pendant ce temps
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("PamkBrik/" PAMK_VERSION);

  String q = "";
  for (const char* p = nom; *p; p++) {
    if (*p == ' ') q += "%20";
    else q += *p;
  }

  char url[220];
  snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=fr&format=json", q.c_str());

  if (!http.begin(client, url)) return false;
  const int codeHttp = http.GET();
  if (codeHttp != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  if (doc["results"].is<JsonArray>() && doc["results"].size() > 0) {
    const char* n = doc["results"][0]["name"] | nom;
    lat = doc["results"][0]["latitude"] | 0.0f;
    lon = doc["results"][0]["longitude"] | 0.0f;
    strncpy(nomTrouve, n, maxNom - 1);
    nomTrouve[maxNom - 1] = 0;
    return true;
  }
  return false;
}

// Soleil
static const char* const PIX_SOLEIL[] = {
  "....#...#...",
  "..#.......#.",
  ".....###....",
  "...#######..",
  "..#########.",
  "#..#######..#",
  "#..#######..#",
  "..#########.",
  "...#######..",
  ".....###....",
  "..#.......#.",
  "....#...#...",
};

// Nuage bien galbé et mignon avec double dôme
static const char* const PIX_NUAGE[] = {
  "............",
  "......####..",
  "....########",
  "..##########",
  ".############",
  "#############",
  "#############",
  "..#########.",
  "............",
  "............",
  "............",
  "............",
};

// Éclaircie (soleil derrière le nuage)
static const char* const PIX_ECLAIRCIE[] = {
  "..#...#.....",
  "....#.......",
  "..#####.###.",
  "..#####.####",
  "....#..#####",
  ".....#######",
  "...#########",
  "..##########",
  "..#########.",
  "............",
  "............",
  "............",
};

// Pluie
static const char* const PIX_PLUIE[] = {
  "......####..",
  "....########",
  "..##########",
  ".############",
  "#############",
  "..#########.",
  "............",
  "..#...#...#.",
  ".#...#...#..",
  "............",
  "..#...#...#.",
  ".#...#...#..",
};

// Orage
static const char* const PIX_ORAGE[] = {
  "......####..",
  "....########",
  "..##########",
  ".############",
  "#############",
  "..#########.",
  ".....###....",
  "....###.....",
  "....####....",
  "......##....",
  ".....##.....",
  "....#.......",
};

// Neige
static const char* const PIX_NEIGE[] = {
  "......####..",
  "....########",
  "..##########",
  ".############",
  "#############",
  "..#########.",
  "............",
  "..#...#...#.",
  ".###.###.###",
  "..#...#...#.",
  "............",
  "..#...#...#.",
};

static const char* texteConditionFr(uint8_t code);

// tr() : la condition est aussi glissée dans des phrases (« 11°C · Sunny »), voir langue.h
const char* texteCondition(uint8_t code) { return tr(texteConditionFr(code)); }

static const char* texteConditionFr(uint8_t code) {
  switch (code) {
    case 0: return "Ensoleillé";
    case 1: return "Peu nuageux";
    case 2: return "Éclaircies";
    case 3: return "Couvert";
    case 45: case 48: return "Brouillard";
    case 51: case 53: case 55: return "Bruine";
    case 61: case 63: case 65: return "Pluie";
    case 71: case 73: case 75: case 77: return "Neige";
    case 80: case 81: case 82: return "Averses";
    case 85: case 86: return "Averses de neige";
    case 95: case 96: case 99: return "Orage";
    default: return "Variable";
  }
}

void dessinerIcone(uint8_t code, int x, int y, int taille, uint8_t couleur) {
  const char* const* m = PIX_SOLEIL;
  if (code == 0) m = PIX_SOLEIL;
  else if (code == 1 || code == 2) m = PIX_ECLAIRCIE;
  else if (code == 3 || code == 45 || code == 48) m = PIX_NUAGE;
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) m = PIX_PLUIE;
  else if (code >= 95) m = PIX_ORAGE;
  else if ((code >= 71 && code <= 77) || code == 85 || code == 86) m = PIX_NEIGE;

  Dessin::motif(m, 12, x, y, taille, couleur);
}

bool synchroniser(float lat, float lon) {
  if (lat < -900.0f) {
    lat = latitude();
    lon = longitude();
  }

  if (WiFi.status() != WL_CONNECTED) {
    JOURNAL("[meteo] Wi-Fi non connecté");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(4000);   // court : l'interface attend pendant ce temps
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("PamkBrik/" PAMK_VERSION);

  char url[220];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto",
           lat, lon);

  JOURNAL("[meteo] requête HTTPS %s", url);
  if (!http.begin(client, url)) {
    JOURNAL("[meteo] échec http.begin");
    return false;
  }
  http.addHeader("Accept-Encoding", "identity");

  const int codeHttp = http.GET();
  if (codeHttp != HTTP_CODE_OK) {
    JOURNAL("[meteo] erreur HTTP %d (%s)", codeHttp, HTTPClient::errorToString(codeHttp).c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (payload.length() == 0) {
    JOURNAL("[meteo] réponse vide");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    JOURNAL("[meteo] JSON invalide : %s", err.c_str());
    return false;
  }

  VilleMeteo& v = r_villes[villeActive()];
  v.donnees.temperature = (int16_t)round(doc["current"]["temperature_2m"] | 0.0f);
  v.donnees.codeWmo = (uint8_t)(doc["current"]["weather_code"] | 0);
  v.donnees.tempMin = (int16_t)round(doc["daily"]["temperature_2m_min"][0] | (float)v.donnees.temperature);
  v.donnees.tempMax = (int16_t)round(doc["daily"]["temperature_2m_max"][0] | (float)v.donnees.temperature);
  v.donnees.majUtc = Horloge::maintenant();
  v.donnees.valide = true;

  JOURNAL("[meteo] OK pour %s : %d°C (min %d°C, max %d°C), code %u (%s)",
          v.nom, v.donnees.temperature, v.donnees.tempMin, v.donnees.tempMax,
          v.donnees.codeWmo, texteCondition(v.donnees.codeWmo));
  return true;
}

}  // namespace Meteo
