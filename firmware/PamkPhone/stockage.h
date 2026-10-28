// Données : tâches et notes en JSON (ArduinoJson 7) sur la partition FFat de la flash.
// Chaque élément a un identifiant unique, une date de création et de modification
// (« maj », secondes UTC), et les suppressions laissent une trace : tout est prêt pour
// une future synchronisation (NON implémentée en V0 : palier réservé de Pamk).
#pragma once
#include "config.h"

struct Tache {
  char id[ID_LONGUEUR + 1];
  char titre[TACHE_TITRE_MAX + 1];
  int echeance;        // AAAAMMJJ, 0 = aucune
  bool faite;
  uint32_t cree;       // UTC
  uint32_t maj;        // UTC (updated_at)
};

struct Note {
  char id[ID_LONGUEUR + 1];
  char* texte;         // UTF-8 en PSRAM ; la première ligne sert de titre
  uint32_t cree;
  uint32_t maj;
};

enum FiltreTaches : uint8_t { TACHES_A_FAIRE, TACHES_FAITES, TACHES_TOUTES };

namespace Stockage {

bool demarrer();                 // monte FFat (formatage si besoin) et charge les fichiers
bool monte();

// --- Tâches ---
int nbTaches();
const Tache* tache(int index);
int indexTache(const char* id);                          // -1 si absente
const Tache* ajouterTache(const char* titre, int echeance);
bool renommerTache(const char* id, const char* titre);
bool cocherTache(const char* id, bool faite);
bool echeanceTache(const char* id, int echeance);
bool supprimerTache(const char* id);
int listeTaches(FiltreTaches f, int* indices, int max);  // triées pour l'affichage
int tachesDuJour(int date, int* indices, int max);       // échéance = date
int nbTachesAFaire();
int nbTachesFaites();
int nbTachesEnRetard();
// Bits 1..31 : jours du mois avec au moins une tâche à faire / seulement des tâches faites.
void joursAvecTaches(int annee, int mois, uint32_t& aFaire, uint32_t& faites);

// --- Notes ---
int nbNotes();
const Note* note(int index);                             // triées : la plus récente d'abord
int indexNote(const char* id);
const Note* ajouterNote(const char* texte);
bool modifierNote(const char* id, const char* texte);
bool supprimerNote(const char* id);
void titreNote(const Note* n, char* sortie, size_t taille);   // première ligne non vide

// --- Divers ---
int nbTraces();
void toutEffacer();
uint32_t octetsTotal();
uint32_t octetsUtilises();
void exporter(Print& sortie);                            // JSON complet (sauvegarde par la console)
void listerFichiers(Print& sortie);

}  // namespace Stockage
