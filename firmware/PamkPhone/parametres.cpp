#include "parametres.h"
#include <Preferences.h>

Parametres Param;

static void copier(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

void parametresParDefaut() {
  Param.veilleS = 120;
  Param.horlogeVeilleMin = 15;
  Param.orientation = ORIENTATION_DEFAUT;
  Param.modeMaj = 0;          // pixels : la zone qui change passe au blanc, puis est redessinée
  Param.nettoyageAuto = 3;    // auto : image complète à la mise en veille quand l'écran a servi
  Param.inverserVolume = false;
  Param.veilleLegere = true;
  Param.veilleSurUSB = false;
  Param.wifiMode = 1;
  Param.pomoTravail = 25;
  Param.pomoPause = 5;
  Param.pomoLongue = 15;
  Param.pomoCycles = 4;
  Param.minuteurMin = 10;
  copier(Param.fuseau, sizeof(Param.fuseau), FUSEAU_DEFAUT);
  copier(Param.ntp, sizeof(Param.ntp), NTP_DEFAUT);
  copier(Param.prenom, sizeof(Param.prenom), "");
  Param.retourToucher = false;    // désactivé par défaut : évite le double-flash saccadé à chaque clic
  Param.clavierRapide = true;
  Param.tailleTexte = 0;
  Param.ecoNuit = true;
  Param.sommeilApresH = 0;    // jamais : sans bouton, seul le bouton de la carte en réveillerait
  Param.assistantFait = false;
  Param.nePasDeranger = false;
  copier(Param.icalUrl, sizeof(Param.icalUrl), "");
}

// Version des réglages enregistrés. 2 (27/10/2026) : orientation par défaut retournée,
// nouveau mode de mise à jour de l'écran ; les anciennes clés « orient », « doux » et
// « complet » de la première version sont abandonnées une fois pour toutes.
// 3 (28/10/2026) : image complète « auto » par défaut ; un téléphone déjà utilisé ne
// repasse pas par le premier démarrage guidé.
// 4 (29/10/2026) : mise à jour « douce » (sans clignotement, avec finition) et image complète
// « auto » (faite à la mise en veille seulement) pour tous : c'est la réponse au
// noir-blanc-noir et à l'usure de la dalle.
// 5 (29/10/2026) : retour au mode pixels (Thomas : en doux, les pixels communs aux deux
// pages restent noirs pendant la transition, un peu partout) ; il efface maintenant toute
// la zone qui change au blanc, sans flash noir.
// 6 (30/10/2026) : plus de sommeil profond après N heures de veille par défaut (Thomas ne veut
// pas réveiller avec le bouton de la carte).
static const uint8_t VERSION_REGLAGES = 6;

static void migrer(uint8_t version) {
  Preferences p;
  if (!p.begin("pamk", false)) return;
  if (version < 2) {
    p.remove("orient");    // valeur 1 enregistrée par la V0.1 = image à l'envers
    p.remove("doux");
    p.remove("complet");
  }
  if (version < 3) {
    if (p.getUChar("netauto", 0) == 0) p.putUChar("netauto", 3);   // « jamais » était le défaut
    p.putBool("assistant", true);
  }
  if (version < 4) {
    p.putUChar("netauto", 3);
  }
  if (version < 5) {
    p.putUChar("majmode", 0);
  }
  if (version < 6) {
    p.putUChar("sommeilh", 0);
  }
  p.putUChar("version", VERSION_REGLAGES);
  p.end();
  JOURNAL("[reglages] réglages migrés de la version %u à %u", (unsigned)version, (unsigned)VERSION_REGLAGES);
}

void chargerParametres() {
  parametresParDefaut();
  Preferences p;
  if (!p.begin("pamk", true)) return;   // première fois : l'espace n'existe pas encore
  const uint8_t version = p.getUChar("version", 1);
  if (version < VERSION_REGLAGES) {
    p.end();
    migrer(version);
    if (!p.begin("pamk", true)) return;
  }
  Param.veilleS = p.getUShort("veille", Param.veilleS);
  Param.horlogeVeilleMin = p.getUChar("hveille", Param.horlogeVeilleMin);
  Param.orientation = p.getUChar("orient", Param.orientation) ? 1 : 0;
  Param.modeMaj = p.getUChar("majmode", Param.modeMaj);
  Param.nettoyageAuto = p.getUChar("netauto", Param.nettoyageAuto);
  Param.inverserVolume = p.getBool("invvol", Param.inverserVolume);
  Param.veilleLegere = p.getBool("legere", Param.veilleLegere);
  Param.veilleSurUSB = p.getBool("vusb", Param.veilleSurUSB);
  Param.wifiMode = p.getUChar("wifi", Param.wifiMode);
  Param.pomoTravail = p.getUChar("ptravail", Param.pomoTravail);
  Param.pomoPause = p.getUChar("ppause", Param.pomoPause);
  Param.pomoLongue = p.getUChar("plongue", Param.pomoLongue);
  Param.pomoCycles = p.getUChar("pcycles", Param.pomoCycles);
  Param.minuteurMin = p.getUChar("minuteur", Param.minuteurMin);
  String s = p.getString("fuseau", Param.fuseau);
  copier(Param.fuseau, sizeof(Param.fuseau), s.c_str());
  s = p.getString("ntp", Param.ntp);
  copier(Param.ntp, sizeof(Param.ntp), s.c_str());
  s = p.getString("prenom", Param.prenom);
  copier(Param.prenom, sizeof(Param.prenom), s.c_str());
  Param.retourToucher = p.getBool("retour", Param.retourToucher);
  Param.clavierRapide = p.getBool("clavrapide", Param.clavierRapide);
  Param.tailleTexte = p.getUChar("taille", Param.tailleTexte);
  Param.ecoNuit = p.getBool("econuit", Param.ecoNuit);
  Param.sommeilApresH = p.getUChar("sommeilh", Param.sommeilApresH);
  Param.assistantFait = p.getBool("assistant", Param.assistantFait);
  Param.nePasDeranger = p.getBool("npd", Param.nePasDeranger);
  s = p.getString("ical", Param.icalUrl);
  copier(Param.icalUrl, sizeof(Param.icalUrl), s.c_str());
  p.end();
  // Garde-fous
  if (Param.modeMaj > 1) Param.modeMaj = 0;
  if (Param.nettoyageAuto > 3) Param.nettoyageAuto = 3;
  if (Param.tailleTexte > 1) Param.tailleTexte = 0;
  if (Param.wifiMode > 2) Param.wifiMode = 1;
  if (Param.pomoTravail < 1) Param.pomoTravail = 25;
  if (Param.pomoPause < 1) Param.pomoPause = 5;
  if (Param.pomoLongue < 1) Param.pomoLongue = 15;
  if (Param.pomoCycles < 1) Param.pomoCycles = 4;
  if (Param.minuteurMin < 1) Param.minuteurMin = 10;
}

void sauverParametres() {
  Preferences p;
  if (!p.begin("pamk", false)) {
    JOURNAL("[reglages] NVS inaccessible");
    return;
  }
  p.putUShort("veille", Param.veilleS);
  p.putUChar("hveille", Param.horlogeVeilleMin);
  p.putUChar("version", VERSION_REGLAGES);
  p.putUChar("orient", Param.orientation);
  p.putUChar("majmode", Param.modeMaj);
  p.putUChar("netauto", Param.nettoyageAuto);
  p.putBool("invvol", Param.inverserVolume);
  p.putBool("legere", Param.veilleLegere);
  p.putBool("vusb", Param.veilleSurUSB);
  p.putUChar("wifi", Param.wifiMode);
  p.putUChar("ptravail", Param.pomoTravail);
  p.putUChar("ppause", Param.pomoPause);
  p.putUChar("plongue", Param.pomoLongue);
  p.putUChar("pcycles", Param.pomoCycles);
  p.putUChar("minuteur", Param.minuteurMin);
  p.putString("fuseau", Param.fuseau);
  p.putString("ntp", Param.ntp);
  p.putString("prenom", Param.prenom);
  p.putBool("retour", Param.retourToucher);
  p.putBool("clavrapide", Param.clavierRapide);
  p.putUChar("taille", Param.tailleTexte);
  p.putBool("econuit", Param.ecoNuit);
  p.putUChar("sommeilh", Param.sommeilApresH);
  p.putBool("assistant", Param.assistantFait);
  p.putBool("npd", Param.nePasDeranger);
  p.putString("ical", Param.icalUrl);
  p.end();
}

void effacerParametres() {
  Preferences p;
  if (p.begin("pamk", false)) { p.clear(); p.end(); }
  if (p.begin("wifi", false)) { p.clear(); p.end(); }
  parametresParDefaut();
}
