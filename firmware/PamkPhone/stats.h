// Statistiques de la semaine (28/10/2026) : minutes de concentration (Pomodoro), pomodoros
// terminés et tâches faites, jour par jour. Gardées dans stats.json (FFat), 35 jours.
#pragma once
#include "config.h"

struct JourStats {
  int date;              // AAAAMMJJ
  uint16_t minutes;      // concentration (phases de travail du Pomodoro, même interrompues)
  uint8_t pomodoros;     // phases de travail menées à leur terme
  uint8_t taches;        // tâches cochées ce jour-là (une tâche répétée compte à chaque fois)
};

namespace Stats {

void demarrer();                        // charge stats.json (après Stockage::demarrer)
void ajouterMinutes(int minutes);       // appelé par Minuteries (fin ou arrêt d'une phase de travail)
void pomodoroTermine();                 // appelé par Minuteries
void tacheFaite();                      // appelé par Stockage quand une tâche est cochée
void tacheDefaite();                    // appelé par Stockage quand elle est décochée le même jour
// Les n derniers jours (aujourd'hui compris), du plus ancien au plus récent ; jours vides inclus.
int derniersJours(JourStats* sortie, int n);
void effacer();

}  // namespace Stats
