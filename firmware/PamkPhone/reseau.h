// Wi-Fi : sert surtout à mettre l'heure à jour (NTP). Allumé à la demande puis éteint
// (mode « auto »), ou laissé allumé tant que l'écran est réveillé (mode « toujours »).
// Les réseaux connus (4 max) sont enregistrés en NVS (espace « wifi »), jamais dans le code.
#pragma once
#include "config.h"

enum EtatReseau : uint8_t {
  RESEAU_ETEINT,
  RESEAU_RECHERCHE,   // scan pour trouver un réseau connu
  RESEAU_CONNEXION,
  RESEAU_CONNECTE,
  RESEAU_ECHEC,
  RESEAU_SCAN,        // scan demandé par Réglages > Wi-Fi
};

namespace Reseau {

void demarrer();
void boucle();
EtatReseau etat();
bool occupe();                         // recherche / connexion / scan en cours
bool actif();                          // radio allumée
const char* texteEtat();               // pour l'interface
const char* ssidActuel();
int barres();                          // 0..4 selon le RSSI

void synchroniserHeure();              // connexion auto à un réseau connu, NTP, puis extinction (mode auto)
void connecter(const char* ssid, const char* mdp, bool memoriser);
void eteindre();

bool lancerScan();
bool scanFini();
int nbScan();
const char* scanSsid(int i);
int scanRssi(int i);
bool scanOuvert(int i);

int nbConnus();
void ssidConnu(int i, char* sortie, size_t taille);
bool estConnu(const char* ssid);
void oublier(int i);

}  // namespace Reseau
