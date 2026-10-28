// Agenda en lecture seule (28/10/2026) : téléchargé depuis l'adresse iCal secrète
// (Param.icalUrl, par ex. Google Agenda > Paramètres > « Adresse secrète au format iCal »),
// lu ligne à ligne, et gardé dans agenda.json (FFat) pour être consulté sans Wi-Fi.
// Seuls les événements de J-1 à J+60 sont gardés, répétitions simples développées.
#pragma once
#include "config.h"

struct Evenement {
  int date;              // AAAAMMJJ (heure locale)
  int16_t debut;         // minutes depuis minuit, -1 = toute la journée
  int16_t fin;           // minutes depuis minuit, -1 = inconnue / toute la journée
  char titre[72];        // UTF-8
  char lieu[48];
};

namespace Agenda {

void demarrer();                                   // charge agenda.json (après Stockage::demarrer)
bool configure();                                  // une adresse iCal est enregistrée
// Télécharge et remplace l'agenda. Wi-Fi déjà connecté requis ; bloquant (quelques secondes).
bool synchroniser();
bool aSynchroniser();                              // configuré et dernière synchro > 3 h
uint32_t derniereSynchro();                        // UTC, 0 = jamais
const char* derniereErreur();                      // texte court, "" si tout va bien
int nbEvenements();
int evenementsDuJour(int date, Evenement* sortie, int max);   // triés par heure
bool prochainEvenement(Evenement& e);              // le prochain à venir (ou en cours) à partir de maintenant
uint32_t joursAvecEvenements(int annee, int mois); // bits 1..31
void effacer();
// Ajouts (28/10/2026)
uint16_t numeroEssai();                            // nombre de synchros tentées depuis le démarrage
                                                   // (savoir si une tentative a eu lieu)

}  // namespace Agenda
