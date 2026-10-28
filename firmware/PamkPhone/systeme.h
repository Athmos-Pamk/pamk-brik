// Cœur du téléphone : démarrage, pile d'applications, événements, rendu, veille.
#pragma once
#include "config.h"
#include "app.h"

namespace Systeme {

void demarrer();
void boucle();

void ouvrir(App* a);               // empile
void retour();                     // dépile (jamais sous l'accueil)
void accueil();                    // revient à l'accueil
void remplacer(App* a);            // remplace l'appli du dessus
App* courante();
int profondeur();

void redessiner(bool complet = false);
// Clavier : affiche tout de suite en noir et blanc (Ecran::rafraichirRapide), puis corrige
// les gris une fois la frappe arrêtée. Sans effet si Param.clavierRapide est coupé.
void redessinerRapide();
void notifier(const char* message);   // bandeau en bas de l'écran pendant ~3 s
void nettoyerEcran();                 // nettoyage anti-fantôme demandé
void appliquerParametres();           // après un changement de réglage

void mettreEnVeille();             // écran de veille + sommeil profond (ne revient pas) ;
                                   // sans bouton : veille légère, revient au toucher
void eteindre();                   // écran « éteint » + sommeil sans minuterie (ne revient pas)
void redemarrer();

}  // namespace Systeme
