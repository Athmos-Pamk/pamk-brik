// Pomodoro et minuteur. L'état est gardé en mémoire RTC : ils continuent pendant le
// sommeil profond, qui programme un réveil à leur échéance (écran de fin de cycle).
#pragma once
#include "config.h"

enum PhasePomo : uint8_t { POMO_ARRET, POMO_TRAVAIL, POMO_PAUSE, POMO_LONGUE };
enum Alerte : uint8_t { ALERTE_AUCUNE, ALERTE_FIN_TRAVAIL, ALERTE_FIN_PAUSE, ALERTE_FIN_MINUTEUR };

namespace Minuteries {

void demarrer();
// Pomodoro
void pomoDemarrer();                 // lance (ou relance) une séance de travail
void pomoPauseReprise();             // suspend / reprend
void pomoArreter();
void pomoPasser();                   // termine la phase en cours tout de suite
PhasePomo pomoPhase();
bool pomoSuspendu();
int pomoRestantS();
int pomoDureeS();
int pomoCyclesFaits();               // pomodoros terminés depuis le début de la série
const char* nomPhase(PhasePomo p);
// Minuteur
void minDemarrer(int secondes);
void minPauseReprise();
void minArreter();
bool minActif();
bool minSuspendu();
int minRestantS();
int minDureeS();
// Surveillance (chaque seconde) : retourne l'alerte qui vient d'arriver.
Alerte verifier();
uint32_t prochaineEcheance();        // UTC de la prochaine fin (0 = aucune)
void texteRestant(char* b, size_t n, int secondes);   // "24 min", "< 1 min", "1 h 05"

}  // namespace Minuteries
