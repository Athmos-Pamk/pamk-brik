// Heure : horloge RTC PCF8563 de la carte (I2C 0x51, pile de sauvegarde MS412FE),
// synchronisation NTP, fuseau POSIX, formats français et calculs de dates.
// La RTC est réglée en UTC ; le fuseau ne sert qu'à l'affichage.
#pragma once
#include "config.h"
#include <time.h>

namespace Horloge {

void demarrer();                        // lit la RTC, applique le fuseau
void boucle();                          // enregistre l'heure NTP dans la RTC quand elle arrive
bool rtcPresente();
bool valide();                          // heure crédible (RTC, NTP ou réglage manuel)
const char* source();                   // "RTC", "NTP", "manuel", "aucune"
uint32_t maintenant();                  // secondes UTC depuis 1970
void locale(struct tm& t);              // heure locale actuelle
void localeDe(uint32_t utc, struct tm& t);
void regler(uint32_t utc, const char* source);
void reglerLocale(int annee, int mois, int jour, int heure, int minute);
void definirFuseau(const char* tz);
void lancerNTP(const char* serveur);    // Wi-Fi connecté requis
bool ntpRecu();                         // une synchro NTP a eu lieu depuis le démarrage
uint32_t derniereSynchro();             // UTC de la dernière synchro NTP (0 = jamais)
void relireRTC();                       // recale l'heure système sur la RTC (après réveil)

// --- Dates « civiles » codées AAAAMMJJ (ex. 20261014) ---
int aujourdhui();
int dateDe(const struct tm& t);
void decomposer(int date, int& annee, int& mois, int& jour);
int composer(int annee, int mois, int jour);
int joursDepuis1970(int date);
int dateDepuisJours(int jours);
int ajouterJours(int date, int n);
int ecartJours(int de, int a);          // a - de en jours
int joursDansMois(int annee, int mois); // mois 1..12
int jourSemaine(int date);              // 0 = lundi ... 6 = dimanche
bool dateValide(int date);

// --- Textes français ---
const char* nomJour(int lundi0);        // "lundi"
const char* nomJourCourt(int lundi0);   // "lun."
const char* nomMois(int mois);          // 1..12 -> "janvier"
const char* nomMoisCourt(int mois);     // "janv."
void texteHeure(char* b, size_t n, const struct tm& t);              // "14:32"
void texteDateLongue(char* b, size_t n, int date, bool annee);     // "mardi 13 octobre (2026)"
void texteDateCourte(char* b, size_t n, int date);                  // "mar. 13 oct."
void texteDateRelative(char* b, size_t n, int date);                // "aujourd'hui", "demain", "hier"...
void texteISO(char* b, size_t n, int date);                         // "2026-10-13"
int dateDepuisISO(const char* s);                                   // 0 si invalide

}  // namespace Horloge
