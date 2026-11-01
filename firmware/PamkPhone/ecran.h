// Écran e-paper : toile portrait 540x960 (1 octet par pixel, 0 = noir, 15 = blanc)
// et envoi vers la dalle native 960x540 via LilyGo-EPD47 (epd_driver.h).
//
// Principe : on redessine toute la toile, puis rafraichir() la compare pixel par pixel avec
// ce que la dalle montre déjà, et ne pilote QUE les pixels qui changent (tous les autres
// reçoivent la valeur « ne rien faire » du mode de dessin) :
//  - « pixels » (défaut) : les pixels changés qui n'étaient pas blancs sont nettoyés
//    (poussés au noir puis au blanc), avec les pixels non blancs qui les touchent de proche
//    en proche (toute la lettre ou la forme), puis tous reçoivent leur nouvelle valeur.
//    Rien d'autre ne bouge à l'écran : jamais de rectangle noir.
//  - « doux » : chaque pixel changé va directement de l'ancien gris au nouveau, sans
//    nettoyage (aucun clignotement, mais des traces peuvent rester).
//  - complet : epd_clear() + image entière, seulement au premier affichage (contenu de la
//    dalle inconnu), sur demande (nettoyage) ou selon l'option « nettoyage automatique ».
#pragma once
#include "config.h"

enum ModeMaj : uint8_t { MAJ_PIXELS = 0, MAJ_DOUX = 1 };

namespace Ecran {

bool demarrer();                    // alloue les tampons (PSRAM) et lance epd_init()
uint8_t* toile();                   // ECRAN_L * ECRAN_H octets
void effacer(uint8_t couleur);

void rafraichir(bool complet = false);
// 28/10/2026
// Rapide : les pixels qui changent vont directement au noir ou au blanc (gris arrondis),
// sans nettoyage. Pour la frappe au clavier et le retour au toucher ; les gris sont
// corrigés par le rafraichir() normal suivant.
void rafraichirRapide();
// Retour immédiat au toucher : inverse la zone (noir <-> blanc) sur la dalle tout de suite.
// Le rafraichir() suivant (page redessinée) la remet d'aplomb.
void inverserZone(const Boite& b);
void definirCompletAuto(bool actif); // image complète quand l'écran a beaucoup « travaillé »
void nettoyerEnProfondeur();        // longue séquence anti-fantôme puis image complète
// Image complète auto (Param.nettoyageAuto == 3) : l'écran a assez servi pour qu'une image
// complète soit utile ; systeme la fait à la mise en veille, quand on ne regarde plus.
bool nettoyageConseille();
void supposerAffiche();             // la dalle montre déjà la toile (réveil sans redessin)
void invaliderZone(const Boite& b); // contenu de cette zone inconnu (sera nettoyé puis redessiné)
void oublierAffichage();            // contenu de la dalle inconnu -> prochain = complet

void definirOrientation(uint8_t o); // 0 ou 1 (rotation de 180°)
uint8_t orientation();
void definirModeMaj(uint8_t mode);  // MAJ_PIXELS ou MAJ_DOUX
void definirCompletTous(uint16_t n);// image complète toutes les n mises à jour (0 = jamais)

// Appelé pendant que l'alimentation de la dalle (PWR_EN) est active : sert à mesurer
// la batterie, dont le pont diviseur dépend de PWR_EN.
void definirRappelAlimente(void (*rappel)());
void executerAlimente(void (*fonction)());   // allume, appelle, éteint

uint32_t nbRafraichissements();
uint32_t dureeDerniereMs();
const char* dernierMode();          // ex. « pixels, 1 groupe, 3 passes »

}  // namespace Ecran
