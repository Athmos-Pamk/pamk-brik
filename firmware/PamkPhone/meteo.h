// =============================================================================
//  Pamk Phone — météo locale ultra-légère via Open-Meteo (sans clé d'API)
//  Les données sont gardées en mémoire RTC pour persister en veille.
// =============================================================================
#pragma once
#include "config.h"

struct DonneesMeteo {
  int16_t temperature;       // en degrés C (arrondi)
  int16_t tempMin;           // min du jour
  int16_t tempMax;           // max du jour
  uint8_t codeWmo;           // code météo standard WMO
  uint32_t majUtc;           // date UTC de la dernière mise à jour
  bool valide;
};

struct VilleMeteo {
  char nom[36];
  float lat;
  float lon;
  DonneesMeteo donnees;
};

namespace Meteo {

const int MAX_VILLES = 6;

bool disponible();
const DonneesMeteo& donnees();
const char* lieu();
float latitude();
float longitude();
int16_t temperature();
int16_t tempMin();
int16_t tempMax();
uint8_t codeWmo();
const char* texteCondition(uint8_t code);

// Gestion multi-villes
int nbVilles();
int villeActive();
void selectionnerVille(int index);
bool villeSuivante();
bool villePrecedente();
const VilleMeteo& ville(int index);
bool ajouterVille(const char* nom, float lat, float lon);
bool supprimerVille(int index);
bool rechercherVille(const char* nom, char* nomTrouve, size_t maxNom, float& lat, float& lon);

// Synchronisation via HTTP (à appeler quand le Wi-Fi est connecté)
bool synchroniser(float lat = -999.0f, float lon = -999.0f);

// Dessin d'une icône météo pixel (soleil, nuage, pluie, etc.)
void dessinerIcone(uint8_t code, int x, int y, int taille = 3, uint8_t couleur = NOIR);

}  // namespace Meteo
