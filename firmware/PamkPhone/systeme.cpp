#include "systeme.h"
#include "ecran.h"
#include "dessin.h"
#include "ui.h"
#include "tactile.h"
#include "boutons.h"
#include "energie.h"
#include "horloge.h"
#include "reseau.h"
#include "stockage.h"
#include "parametres.h"
#include "minuteries.h"
#include "ecrans.h"
#include "console.h"
#include "acces.h"
#include "agenda.h"
#include "stats.h"
#include <Wire.h>

// --- Comportements par défaut des applis ------------------------------------
void App::glisser(int dx, int dy) {
  // Vers la gauche ou vers le haut = suivant (comme volume +) ; sinon précédent.
  if (abs(dx) >= abs(dy)) bouton(dx < 0 ? BOUTON_PLUS : BOUTON_MOINS);
  else bouton(dy < 0 ? BOUTON_PLUS : BOUTON_MOINS);
}

bool App::retour() {
  Systeme::retour();
  return true;
}

App* appParId(uint8_t id) {
  switch (id) {
    case APP_MENU:         return appMenu();
    case APP_TACHES:       return appTaches();
    case APP_NOTES:        return appNotes();
    case APP_POMODORO:     return appPomodoro();
    case APP_CALENDRIER:   return appCalendrier();
    case APP_CALCULATRICE: return appCalculatrice();
    case APP_REGLAGES:     return appReglages();
    case APP_RECHERCHE:    return appRecherche();
    case APP_BILAN:        return appBilan();
    case APP_ACCES:        return appAcces();
    case APP_VOLET:        return appVolet();
    case APP_METEO:        return appMeteo();
    case APP_HABITUDES:    return appHabitudes();
    case APP_ZEN:          return appZen();
    case APP_CONVERTISSEUR:return appConvertisseur();
    default:               return nullptr;
  }
}

namespace Systeme {

static const int PILE_MAX = 8;
static App* s_pile[PILE_MAX];
static int s_nb = 0;
static bool s_redessin = false;
static bool s_complet = false;
static char s_toast[96] = "";
static bool s_toastVisible = false;
static uint32_t s_toastFin = 0;
static uint32_t s_tSeconde = 0;
static int s_derniereMinute = -1;
static int s_derniereMinuterie = -1;
static uint32_t s_tDemarrage = 0;
static bool s_avertiFaible = false;
static EtatReseau s_dernierEtatReseau = RESEAU_ETEINT;
static int s_dernierPct = -1;
static bool s_rapide = false;             // prochain affichage : rapide (clavier)
static bool s_correction = false;         // gris à corriger après un affichage rapide
static uint32_t s_tRapide = 0;
static bool s_appuiAffiche = false;       // une zone est inversée (retour au toucher)
static const uint32_t CORRECTION_APRES_MS = 1500;   // après la dernière frappe
static Alerte s_alerteManquee = ALERTE_AUCUNE;   // Ne pas déranger : fin arrivée pendant la veille

// Gardés pendant le sommeil profond
static RTC_DATA_ATTR uint8_t r_derniereApp = APP_ACCUEIL;
static RTC_DATA_ATTR bool r_eteint = false;
static RTC_DATA_ATTR uint32_t r_essaiWifi = 0;     // dernière synchro automatique tentée (UTC)

// ---------------------------------------------------------------------------
//  Pile d'applications
// ---------------------------------------------------------------------------
App* courante() { return s_nb > 0 ? s_pile[s_nb - 1] : nullptr; }
int profondeur() { return s_nb; }

void redessiner(bool complet) {
  s_redessin = true;
  if (complet) s_complet = true;
}

void redessinerRapide() {
  s_redessin = true;
  if (Param.clavierRapide) s_rapide = true;
}

void ouvrir(App* a) {
  if (!a) return;
  if (s_nb >= PILE_MAX) {   // pile pleine : on oublie la plus ancienne après l'accueil
    for (int i = 1; i + 1 < s_nb; i++) s_pile[i] = s_pile[i + 1];
    s_nb--;
  }
  s_pile[s_nb++] = a;
  a->entrer();
  redessiner();
}

void retour() {
  if (s_nb > 1) {
    s_nb--;
    s_pile[s_nb - 1]->entrer();
  }
  redessiner();
}

void accueil() {
  s_nb = 1;
  s_pile[0] = appAccueil();
  s_pile[0]->entrer();
  redessiner();
}

void remplacer(App* a) {
  if (!a) return;
  if (s_nb <= 1) {
    ouvrir(a);
    return;
  }
  s_pile[s_nb - 1] = a;
  a->entrer();
  redessiner();
}

void notifier(const char* message) {
  strncpy(s_toast, message ? message : "", sizeof(s_toast) - 1);
  s_toast[sizeof(s_toast) - 1] = 0;
  s_toastVisible = true;
  s_toastFin = millis() + 3000;
  redessiner();
}

void appliquerParametres() {
  Ecran::definirOrientation(Param.orientation);
  Ecran::definirModeMaj(Param.modeMaj);
  Ecran::definirCompletTous(Param.nettoyageAuto == 2 ? (uint16_t)NETTOYAGE_AUTO_TOUS : (uint16_t)0);
  Ecran::definirCompletAuto(Param.nettoyageAuto == 3);
  Boutons::inverserVolume(Param.inverserVolume);
  Horloge::definirFuseau(Param.fuseau);
}

// ---------------------------------------------------------------------------
//  Rendu
// ---------------------------------------------------------------------------
static void dessinerApp(App* a) {
  Dessin::sansDecoupe();
  if (a->barreEtat()) UI::barreEtat(a->afficherHeure());
  a->dessiner();
  Dessin::sansDecoupe();
}

static void rendre() {
  Ecran::effacer(BLANC);
  UI::viderZones();
  App* haut = courante();
  if (!haut) return;
  if (haut->modale() && s_nb >= 2) {
    dessinerApp(s_pile[s_nb - 2]);
    UI::viderZones();          // seules les zones de la fenêtre modale répondent
  }
  dessinerApp(haut);
  if (s_toastVisible) UI::toast(s_toast);
}

static void afficher() {
  s_redessin = false;
  const bool complet = s_complet;
  const bool rapide = s_rapide && !complet;
  s_complet = false;
  s_rapide = false;
  rendre();
  if (rapide) {
    Ecran::rafraichirRapide();
    s_correction = true;          // les gris (bords des lettres) seront corrigés plus tard
    s_tRapide = millis();
  } else {
    Ecran::rafraichir(complet);
    s_correction = false;
  }
  s_appuiAffiche = false;         // la page redessinée remplace la zone inversée
  Tactile::purger();
}

void nettoyerEcran() {
  rendre();
  Ecran::nettoyerEnProfondeur();
  Tactile::purger();
  s_redessin = false;
}

// ---------------------------------------------------------------------------
//  Veille
// ---------------------------------------------------------------------------
// Mode nuit (Param.ecoNuit) : de NUIT_DEBUT_H à NUIT_FIN_H, l'heure de l'écran de veille
// n'est plus mise à jour et le Wi-Fi ne se met pas à l'heure tout seul.
static bool estLaNuit() {
  if (!Param.ecoNuit || !Horloge::valide()) return false;
  struct tm t;
  Horloge::locale(t);
  return t.tm_hour >= NUIT_DEBUT_H || t.tm_hour < NUIT_FIN_H;
}

// Secondes jusqu'à la fin de la nuit (NUIT_FIN_H h 00, heure locale).
static uint32_t secondesJusquAuMatin() {
  struct tm t;
  Horloge::locale(t);
  const int h = (24 + NUIT_FIN_H - t.tm_hour) % 24;
  const long s = (long)h * 3600L - t.tm_min * 60L - t.tm_sec;
  return s > 60 ? (uint32_t)s : 60;
}

// Secondes avant le prochain réveil programmé (0 = seulement le bouton).
static uint32_t secondesAvantReveil() {
  const uint32_t m = Horloge::maintenant();
  uint32_t s = 0;
  if (Param.horlogeVeilleMin > 0 && Horloge::valide()) {
    if (estLaNuit()) {
      s = secondesJusquAuMatin();
    } else {
      const uint32_t pas = Param.horlogeVeilleMin * 60UL;
      s = pas - (m % pas);
      if (s < 20) s += pas;
    }
  }
  const uint32_t fins[2] = {Minuteries::prochaineEcheance(), Horloge::valide() ? Stockage::prochainRappel() : 0};
  for (int i = 0; i < 2; i++) {
    if (!fins[i]) continue;
    const uint32_t d = fins[i] > m ? fins[i] - m : 1;
    if (s == 0 || d < s) s = d;
  }
  return s;
}

static void veilleAuToucher();

void mettreEnVeille() {
  App* a = courante();
  r_derniereApp = (a && a->reprendreApresVeille()) ? (uint8_t)a->id() : (uint8_t)APP_ACCUEIL;
  r_eteint = false;
  Reseau::eteindre();
  Energie::mesurerMaintenant();
  Ecrans::veille();
  // Sans image complète (aucun rectangle noir), sauf avec l'option « à la veille », ou « auto »
  // quand l'écran a assez servi (Réglages > Écran) : on ne regarde plus, le flash ne gêne pas.
  Ecran::rafraichir(Param.nettoyageAuto == 1 || Ecran::nettoyageConseille());
  if (!BOUTONS_PRESENTS) {
    veilleAuToucher();   // revient au toucher, sur l'appli ouverte
    return;
  }
  Energie::sommeilProfond(secondesAvantReveil());
}

void eteindre() {
  r_eteint = true;
  r_derniereApp = APP_ACCUEIL;
  Minuteries::pomoArreter();
  Minuteries::minArreter();
  Reseau::eteindre();
  Ecrans::eteint();
  Ecran::rafraichir(Param.nettoyageAuto == 1 || Ecran::nettoyageConseille());
  Energie::sommeilProfond(0);
}

void redemarrer() {
  Reseau::eteindre();
  delay(50);
  ESP.restart();
}

static void dormirBatterieVide() {
  Reseau::eteindre();
  Ecrans::batterieVide();
  Ecran::rafraichir();   // complet tout seul si le contenu de la dalle est inconnu
  // Sans bouton, rien ne réveillerait la carte une fois rebranchée : elle revérifie la
  // batterie toutes les 5 minutes (et redémarre normalement si elle est rechargée).
  Energie::sommeilProfond(BOUTONS_PRESENTS ? 0 : 300);
}

// Réveil par minuterie sans alerte : on met juste à jour l'heure de l'écran de veille.
// La dalle montre l'écran de veille mémorisé : on le redessine à l'identique, puis
// rafraichir() ne pilote que les pixels de l'heure et de la batterie qui ont changé.
static void rafraichirHeureVeille() {
  Energie::mesurerMaintenant();
  if (Energie::vide()) dormirBatterieVide();
  Ecrans::veille(!BOUTONS_PRESENTS);   // sans bouton, on n'arrive ici que depuis la veille profonde
  Ecran::rafraichir();
  Energie::sommeilProfond(secondesAvantReveil());
}

// ---------------------------------------------------------------------------
//  Événements
// ---------------------------------------------------------------------------
// Ne pas déranger : la fin d'un Pomodoro ou du minuteur ne prend pas tout l'écran, un bandeau
// la signale (les phases continuent quand même).
static const char* texteAlerte(Alerte al) {
  switch (al) {
    case ALERTE_FIN_TRAVAIL:  return "Pomodoro : travail terminé";
    case ALERTE_FIN_PAUSE:    return "Pomodoro : fin de la pause";
    case ALERTE_FIN_MINUTEUR: return "Minuteur terminé";
    default:                  return "";
  }
}

static void ouvrirAlerte(Alerte al) {
  if (Param.nePasDeranger) {
    notifier(texteAlerte(al));
    return;
  }
  Energie::activite();
  App* a = courante();
  if (a && a->id() == APP_ALERTE) remplacer(appAlerte(al));
  else ouvrir(appAlerte(al));
}

static void ouvrirRappel(const char* id) {
  if (Param.nePasDeranger) return;
  Energie::activite();
  App* a = courante();
  if (a && a->id() == APP_ALERTE) remplacer(appAlerteRappel(id));
  else ouvrir(appAlerteRappel(id));
}

// Rappel de tâche arrivé (et pas encore affiché) : ouvre son écran. Retourne true si oui.
// Une alerte (fin de Pomodoro, autre rappel) déjà à l'écran passe d'abord : le rappel attend
// qu'elle soit fermée, il n'est marqué « vu » qu'une fois affiché.
static bool verifierRappels() {
  if (Param.nePasDeranger) return false;
  if (!Horloge::valide()) return false;
  App* a = courante();
  if (a && a->id() == APP_ALERTE) return false;
  char id[ID_LONGUEUR + 1];
  if (!Stockage::rappelArrive(Horloge::maintenant(), id, sizeof(id))) return false;
  ouvrirRappel(id);
  return true;
}

// Sans bouton marche (BOUTONS_PRESENTS = false) : la broche INT du tactile (GPIO47) ne peut
// pas réveiller du sommeil profond. La veille est donc une suite de veilles légères (la RAM
// et l'appli ouverte sont gardées) jusqu'à un contact sur l'écran. L'écran de veille est mis
// à jour comme en sommeil profond (heure, batterie, fin de minuterie, rappels). Le tactile
// reste allumé : la veille consomme plus qu'avec le bouton. Après Param.sommeilApresH heures
// sans contact, on passe au sommeil profond (réveil par le bouton SENSOR_VN de la carte).
static void veilleAuToucher() {
  Tactile::purger();
  const uint32_t pasS = Param.horlogeVeilleMin * 60UL;
  uint32_t dernierPas = pasS ? Horloge::maintenant() / pasS : 0;
  const uint32_t debut = millis();
  while (true) {
    if (Param.sommeilApresH > 0 && !Energie::usbOrdinateur() &&
        millis() - debut >= Param.sommeilApresH * 3600000UL) {
      Ecrans::veille(true);
      Ecran::rafraichir();
      Energie::sommeilProfond(secondesAvantReveil());   // ne revient pas
    }
    if (Energie::usbOrdinateur()) delay(VEILLE_TACTILE_SCRUTATION_MS);   // la veille légère couperait l'USB
    else Energie::veilleLegere(5000);   // réveil immédiat par contact tactile (GPIO47) ou max 5 s
    if (Tactile::reveilParContact()) break;
    if (Boutons::marcheAppuye()) {   // bouton S4 de la carte
      Boutons::demarrer();           // il est encore enfoncé : on ignorera son relâchement
      break;
    }
    const Alerte al = Minuteries::verifier();
    if (al != ALERTE_AUCUNE) {
      if (!Param.nePasDeranger) {
        ouvrirAlerte(al);
        break;
      }
      s_alerteManquee = al;   // Ne pas déranger : bandeau au réveil, sans réveiller
    }
    if (!Param.nePasDeranger && verifierRappels()) break;
    if (pasS && Horloge::valide() && !estLaNuit() && Horloge::maintenant() / pasS != dernierPas) {
      dernierPas = Horloge::maintenant() / pasS;
      Energie::mesurerMaintenant();
      if (Energie::vide()) dormirBatterieVide();
      Ecrans::veille();
      Ecran::rafraichir();
    }
  }
  if (s_alerteManquee != ALERTE_AUCUNE) {
    notifier(texteAlerte(s_alerteManquee));
    s_alerteManquee = ALERTE_AUCUNE;
  }
  Energie::activite();
  redessiner();
}

static void traiterBouton(const EvenementBouton& e) {
  Energie::activite();
  App* a = courante();
  if (e.bouton == BOUTON_MARCHE) {
    if (e.type == APPUI_COURT) mettreEnVeille();
    else if (e.type == APPUI_LONG) eteindre();
    return;
  }
  if (e.type == APPUI_DUO) {
    accueil();
    return;
  }
  if (e.type == APPUI_LONG) {
    if (e.bouton == BOUTON_MOINS) {
      if (a) a->retour();
    } else {
      accueil();
    }
    return;
  }
  if (a) a->bouton(e.bouton);
}

static void traiterGeste(const Geste& g) {
  Energie::activite();
  App* a = courante();
  if (!a) return;
  int code = 0, arg = 0;
  switch (g.type) {
    case GESTE_POSE: {
      // Retour immédiat : la zone touchée s'inverse tout de suite. La page redessinée après
      // l'action la remet d'aplomb (ou boucle(), si le doigt part sans rien déclencher).
      Boite b;
      if (Param.retourToucher && !(s_toastVisible && g.y >= 776 && g.y < 852) && UI::chercherZone(g.x, g.y, b)) {
        Ecran::inverserZone(b);
        s_appuiAffiche = true;
      }
      break;
    }
    case GESTE_TOUCHER:
      if (s_toastVisible && g.y >= 776 && g.y < 852) {   // toucher le bandeau le ferme
        s_toastVisible = false;
        redessiner();
        return;
      }
      if (UI::chercher(g.x, g.y, code, arg)) {
        if (code == CODE_RETOUR) a->retour();
        else a->action(code, arg);
      } else {
        a->toucherLibre(g.x, g.y);
      }
      break;
    case GESTE_APPUI_LONG:
      if (UI::chercher(g.x, g.y, code, arg)) {
        if (code == CODE_RETOUR) accueil();
        else a->appuiLong(code, arg);
      }
      break;
    case GESTE_GLISSER:
      // Si on est dans le volet rapide : glisser vers le haut ferme le volet
      if (a && a->id() == APP_VOLET) {
        if (g.dy < -35 && abs(g.dy) > abs(g.dx)) {
          retour();
          return;
        }
      }

      // Glisser vers le bas en partant du haut de l'écran (barre d'état) = volet rapide.
      // Ailleurs, glisser vers le bas reste « page précédente » dans les listes.
      if (g.y < VOLET_DEPART_MAX_Y && g.dy > 40 && abs(g.dy) > abs(g.dx)) {
        if (!a || a->id() != APP_VOLET) {
          ouvrir(appVolet());
          return;
        }
      }

      // Glisser depuis le bord gauche vers la droite = retour (sauf dans le lanceur d'applications)
      if (a && a->id() != APP_MENU && g.x < 60 && g.dx > 80 && abs(g.dx) > abs(g.dy)) {
        a->retour();
      } else {
        a->glisser(g.dx, g.dy);
      }
      break;
    default:
      break;
  }
}

static void tic() {
  const Alerte al = Minuteries::verifier();
  if (al != ALERTE_AUCUNE) ouvrirAlerte(al);
  verifierRappels();
  App* a = courante();
  if (a) a->seconde();

  struct tm t;
  Horloge::locale(t);
  if (t.tm_min != s_derniereMinute) {
    s_derniereMinute = t.tm_min;
    if (a) a->minute();
    redessiner();   // l'heure de la barre d'état ; rien n'est envoyé si rien n'a changé
  }
  // Minutes restantes affichées dans la barre d'état
  int mr = -1;
  if (Minuteries::pomoPhase() != POMO_ARRET) mr = (Minuteries::pomoRestantS() + 59) / 60;
  else if (Minuteries::minActif()) mr = (Minuteries::minRestantS() + 59) / 60;
  if (mr != s_derniereMinuterie) {
    s_derniereMinuterie = mr;
    redessiner();
  }
  if (s_toastVisible && (int32_t)(millis() - s_toastFin) > 0) {
    s_toastVisible = false;
    redessiner();
  }
  // Batterie
  Energie::boucle();
  const int pct = Energie::pourcentage();
  if (pct != s_dernierPct) {
    s_dernierPct = pct;
    redessiner();
  }
  if (Energie::vide()) dormirBatterieVide();
  if (Energie::faible() && !s_avertiFaible) {
    s_avertiFaible = true;
    notifier("Batterie faible : pense à me recharger");
  }
  // État du Wi-Fi (icône, écran Réglages)
  const EtatReseau er = Reseau::etat();
  if (er != s_dernierEtatReseau) {
    s_dernierEtatReseau = er;
    redessiner();
  }
}

static void verifierVeilleAuto() {
  if (Param.veilleS == 0) return;
  if (Acces::actif()) return;   // page web ouverte depuis l'ordinateur
  if (Energie::usbOrdinateur() && !Param.veilleSurUSB) return;
  if (Reseau::occupe()) return;
  if (Reseau::agendaEnCours()) return;   // téléchargement de l'agenda (idée FolloUp : pas de veille en plein travail)
  App* a = courante();
  uint32_t delai = Param.veilleS * 1000UL;
  if (a) delai = a->delaiVeilleMs(delai);
  if (Energie::inactifDepuisMs() >= delai) mettreEnVeille();
}

static void repos() {
  const bool legere = Param.veilleLegere && !Energie::usbOrdinateur() && !Reseau::actif() &&
                      !Tactile::doigtPose() && !Boutons::unAppuye() && !s_redessin && !Console::active() &&
                      Energie::inactifDepuisMs() > DELAI_AVANT_VEILLE_LEGERE_MS &&
                      millis() - s_tDemarrage > 10000;
  if (legere) Energie::veilleLegere(VEILLE_LEGERE_MAX_MS);
  else delay(TACTILE_PERIODE_MS / 2);
}

// ---------------------------------------------------------------------------
//  Démarrage
// ---------------------------------------------------------------------------
void demarrer() {
  Serial.begin(115200);
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // ne jamais bloquer si aucun PC n'écoute
#endif
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  chargerParametres();
  Wire.begin(BROCHE_I2C_SDA, BROCHE_I2C_SCL, I2C_FREQUENCE);
  Wire.setTimeOut(30);
  Horloge::definirFuseau(Param.fuseau);
  Horloge::demarrer();
  Minuteries::demarrer();
  Energie::demarrer();
  Boutons::demarrer();
  Boutons::inverserVolume(Param.inverserVolume);

  // Réveil alors que le téléphone était « éteint » : il faut tenir le bouton 1,5 s.
  if (cause == ESP_SLEEP_WAKEUP_EXT1 && r_eteint) {
    const uint32_t t0 = millis();
    while (Boutons::marcheAppuye() && millis() - t0 < 1500) delay(10);
    if (millis() - t0 < 1500) Energie::sommeilProfond(0);   // appui trop court : on se rendort
    r_eteint = false;
    Boutons::demarrer();   // le bouton est encore enfoncé : on ignorera son relâchement
  }

  if (!Ecran::demarrer()) {
    while (true) {
      JOURNAL("[systeme] écran indisponible : vérifie Outils > PSRAM > OPI PSRAM");
      delay(2000);
    }
  }
  appliquerParametres();
  Ecran::definirRappelAlimente(Energie::mesurer);
  bool dalleConnue = false;
  if (cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER) {
    // Réveil : la dalle montre l'écran mémorisé avant le sommeil. On le redessine à
    // l'identique pour que la suite ne change que les pixels qui diffèrent.
    dalleConnue = Ecrans::redessinerMemorise();
    if (dalleConnue) Ecran::supposerAffiche();
  }
  if (!dalleConnue) {
    // Démarrage à froid (ou rien de mémorisé) : contenu de la dalle inconnu, la première
    // image est forcément complète. On l'affiche avant de monter la flash : le tout premier
    // formatage de la partition FFat peut prendre un moment.
    Ecrans::demarrage("Démarrage…");
    Ecran::rafraichir();
  }
  const bool stockageOk = Stockage::demarrer();
  Stats::demarrer();
  Agenda::demarrer();

  Alerte alerte = ALERTE_AUCUNE;
  char rappel[ID_LONGUEUR + 1] = "";
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    alerte = Minuteries::verifier();
    if (Horloge::valide() && !Param.nePasDeranger) Stockage::rappelArrive(Horloge::maintenant(), rappel, sizeof(rappel));
    if (Param.nePasDeranger) alerte = ALERTE_AUCUNE;   // pas de réveil pour une fin de minuterie
    // Sans bouton, seule la veille profonde (après Param.sommeilApresH heures) programme ce
    // réveil ; l'autre cas est l'écran « batterie vide », et alors on démarre normalement.
    if (alerte == ALERTE_AUCUNE && !rappel[0] && (BOUTONS_PRESENTS || Ecrans::veilleProfonde())) {
      rafraichirHeureVeille();   // ne revient pas
    }
  }

  Energie::mesurerMaintenant();
  if (Energie::vide()) dormirBatterieVide();
  JOURNAL("[systeme] %s %s, réveil : %s", PAMK_NOM, PAMK_VERSION, Energie::texteCauseReveil());

  Tactile::demarrer(cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER);
  Reseau::demarrer();
  Console::demarrer();

  s_nb = 0;
  s_pile[s_nb++] = appAccueil();
  appAccueil()->entrer();
  if (cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER) {
    App* a = appParId(r_derniereApp);
    if (a) ouvrir(a);
  }
  if (alerte != ALERTE_AUCUNE) ouvrir(appAlerte(alerte));
  if (rappel[0]) ouvrir(appAlerteRappel(rappel));
  if (!Param.assistantFait) ouvrir(appAssistant());   // tout premier démarrage

  // Mise à l'heure par Wi-Fi
  // (au plus une tentative automatique toutes les 6 h si ça échoue, pour ménager la batterie)
  const uint32_t maint = Horloge::maintenant();
  const bool heureVieille = !Horloge::valide() || maint - Horloge::derniereSynchro() > 86400UL;
  const bool essaiRecent = r_essaiWifi != 0 && maint - r_essaiWifi < 6UL * 3600UL;
  if (Param.wifiMode == 2 ||
      (Param.wifiMode == 1 && !estLaNuit() &&
       (cause == ESP_SLEEP_WAKEUP_UNDEFINED || (heureVieille && !essaiRecent)))) {
    r_essaiWifi = maint;
    Reseau::synchroniserHeure();
  }

  if (!stockageOk) notifier("Mémoire de stockage indisponible");

  struct tm t;
  Horloge::locale(t);
  s_derniereMinute = t.tm_min;
  s_dernierPct = Energie::pourcentage();
  redessiner();   // passage pixel par pixel depuis l'écran de démarrage ou de veille
  Energie::activite();
  s_tDemarrage = millis();
  s_tSeconde = millis();
}

void boucle() {
  Console::boucle();
  Reseau::boucle();
  Horloge::boucle();
  Boutons::scruter();
  EvenementBouton eb;
  while (Boutons::lire(eb)) traiterBouton(eb);
  if (Acces::actif()) Acces::boucle();
  const Geste g = Tactile::suivre();
  if (g.type != GESTE_AUCUN) traiterGeste(g);
  // Zone inversée par le retour au toucher, mais le doigt est parti sans rien déclencher
  if (s_appuiAffiche && !s_redessin && !Tactile::doigtPose()) redessiner();
  // Après une frappe rapide au clavier : corrige les gris une fois la frappe arrêtée
  if (s_correction && !s_redessin && millis() - s_tRapide > CORRECTION_APRES_MS) redessiner();
  if (millis() - s_tSeconde >= 1000) {
    s_tSeconde = millis();
    tic();
  }
  if (s_redessin) afficher();
  verifierVeilleAuto();
  repos();
}

}  // namespace Systeme
