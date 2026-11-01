// Base commune des applications et accès aux instances.
#pragma once
#include "config.h"
#include "boutons.h"
#include "minuteries.h"

enum IdApp : uint8_t {
  APP_ACCUEIL, APP_MENU, APP_TACHES, APP_TACHE, APP_NOTES, APP_NOTE, APP_POMODORO,
  APP_CALENDRIER, APP_CALCULATRICE, APP_REGLAGES, APP_CLAVIER, APP_DIALOGUE, APP_ALERTE,
  // 28/10/2026
  APP_RECHERCHE, APP_BILAN, APP_ASSISTANT, APP_ACCES,
  APP_VOLET, APP_METEO, APP_HABITUDES, APP_ZEN, APP_CONVERTISSEUR,
};

class App {
 public:
  virtual ~App() {}
  virtual IdApp id() const = 0;
  virtual void entrer() {}                           // l'appli devient visible
  virtual void dessiner() = 0;                       // dessine tout sauf la barre d'état
  virtual void action(int code, int arg) { (void)code; (void)arg; }
  virtual void appuiLong(int code, int arg) { action(code, arg); }
  virtual void toucherLibre(int x, int y) { (void)x; (void)y; }   // toucher hors des zones
  virtual bool bouton(Bouton b) { (void)b; return false; }       // volume + / − (appui court)
  virtual void glisser(int dx, int dy);              // défaut : pages (haut/bas) via bouton()
  virtual bool retour();                             // défaut : Systeme::retour()
  virtual void seconde() {}
  virtual void minute() {}
  virtual bool barreEtat() const { return true; }
  virtual bool afficherHeure() const { return true; }
  virtual bool modale() const { return false; }      // dessinée par-dessus l'appli précédente
  virtual bool reprendreApresVeille() const { return true; }
  virtual uint32_t delaiVeilleMs(uint32_t defaut) const { return defaut; }
};

// Accès aux applications (une instance chacune)
App* appAccueil();
App* appMenu();
App* appTaches();
App* appTache(const char* id, bool nouvelle = false);   // nouvelle : juste créée (Annuler / Ajouter)
App* appNotes();
App* appNote(const char* id);
App* appPomodoro();
App* appCalendrier();
App* appCalculatrice();
App* appReglages();
enum PageReglages : uint8_t {
  PAGE_PRINCIPALE, PAGE_WIFI, PAGE_WIFI_SCAN, PAGE_HEURE, PAGE_ECRAN, PAGE_MIRE, PAGE_ENERGIE,
  PAGE_POMODORO, PAGE_BOUTONS, PAGE_APROPOS, PAGE_DONNEES,
};
App* appReglagesPage(PageReglages page);   // ouvre directement une page des réglages
App* appAlerte(Alerte a);
App* appParId(uint8_t id);   // pour rouvrir l'appli après la veille

// 28/10/2026
App* appRecherche();                  // app_recherche.cpp : tâches et notes
App* appBilan();                      // app_bilan.cpp : bilan de la semaine
App* appAssistant();                  // app_assistant.cpp : premier démarrage guidé
App* appAcces();                      // app_acces.cpp : page web et mise à jour depuis l'ordinateur
App* appVolet();                      // app_volet.cpp : volet déroulant réglages rapides
App* appMeteo();                      // app_meteo.cpp : prévisions météo locales
App* appHabitudes();                  // app_habitudes.cpp : suivi d'habitudes et rituels quotidiens
App* appZen();                        // app_zen.cpp : respiration et cohérence cardiaque
App* appConvertisseur();              // app_convertisseur.cpp : conversions d'unités
App* appAlerteRappel(const char* idTache);   // app_alerte.cpp : rappel d'une tâche (id() = APP_ALERTE)

// Saisie au clavier et confirmation (applis modales)
enum ModeSaisie : uint8_t { SAISIE_LIGNE, SAISIE_TEXTE, SAISIE_MOT_DE_PASSE };
typedef void (*RappelSaisie)(bool valide, const char* texte, void* contexte);
typedef void (*RappelConfirmation)(bool oui, void* contexte);
App* appClavier(const char* titre, const char* initial, int maxOctets, ModeSaisie mode,
                RappelSaisie rappel, void* contexte);
App* appDialogue(const char* titre, const char* message, const char* oui,
                 RappelConfirmation rappel, void* contexte);

// Partagé entre Tâches et Calendrier (défini dans app_taches.cpp)
void dessinerLigneTache(int y, int index, int codeCocher, int codeOuvrir);
void ajouterTacheAuClavier(int echeance);
