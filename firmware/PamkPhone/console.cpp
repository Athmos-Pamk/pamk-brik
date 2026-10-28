#include "console.h"
#include "systeme.h"
#include "ecran.h"
#include "tactile.h"
#include "boutons.h"
#include "energie.h"
#include "horloge.h"
#include "reseau.h"
#include "stockage.h"
#include "parametres.h"
#include "minuteries.h"
#include <WiFi.h>
#include <esp_heap_caps.h>

namespace Console {

static char s_ligne[256];
static int s_long = 0;
static uint32_t s_tCommande = 0;
static bool s_resetDemande = false;

typedef void (*Commande)(const char* args);
struct DefCommande {
  const char* nom;
  const char* alias;
  Commande f;
  const char* aide;
};

static void cmdAide(const char*);

static void afficherHeure() {
  struct tm t;
  Horloge::locale(t);
  char h[8], d[64];
  Horloge::texteHeure(h, sizeof(h), t);
  Horloge::texteDateLongue(d, sizeof(d), Horloge::dateDe(t), true);
  Serial.printf("Heure : %s, %s (source : %s, %s)\r\n", h, d, Horloge::source(),
                Horloge::valide() ? "valide" : "NON valide");
}

static void cmdEtat(const char*) {
  Serial.printf("%s %s\r\n", PAMK_NOM, PAMK_VERSION);
  Serial.printf("Allumé depuis %lu s, réveil : %s\r\n", (unsigned long)(millis() / 1000), Energie::texteCauseReveil());
  afficherHeure();
  Serial.printf("Batterie : %d mV, %d %%%s\r\n", Energie::millivolts(), Energie::pourcentage(),
                Energie::enCharge() ? " (en charge / USB)" : "");
  Serial.printf("Wi-Fi : %s\r\n", Reseau::texteEtat());
  Serial.printf("Tactile : %s (0x%02X), RTC : %s\r\n", Tactile::present() ? "GT911" : "absent",
                Tactile::adresse(), Horloge::rtcPresente() ? "PCF8563" : "absente");
  Serial.printf("Mémoire : %lu octets libres (interne), %lu octets libres (PSRAM)\r\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("Stockage : %lu / %lu octets, %d tâche(s), %d note(s)\r\n",
                (unsigned long)Stockage::octetsUtilises(), (unsigned long)Stockage::octetsTotal(),
                Stockage::nbTaches(), Stockage::nbNotes());
  Serial.printf("Écran : %lu rafraîchissement(s), dernier « %s » en %lu ms, orientation %u\r\n",
                (unsigned long)Ecran::nbRafraichissements(), Ecran::dernierMode(),
                (unsigned long)Ecran::dureeDerniereMs(), (unsigned)Ecran::orientation());
  App* a = Systeme::courante();
  Serial.printf("Appli : %d (pile de %d)\r\n", a ? (int)a->id() : -1, Systeme::profondeur());
  Serial.printf("Inactif depuis %lu s\r\n", (unsigned long)(Energie::inactifDepuisMs() / 1000));
}

static void cmdTaches(const char*) {
  static int idx[TACHES_MAX];
  const int n = Stockage::listeTaches(TACHES_TOUTES, idx, TACHES_MAX);
  if (n == 0) Serial.println("Aucune tâche.");
  for (int i = 0; i < n; i++) {
    const Tache* t = Stockage::tache(idx[i]);
    char e[16] = "";
    if (t->echeance) Horloge::texteISO(e, sizeof(e), t->echeance);
    Serial.printf("%3d. [%c] %s%s%s\r\n", i + 1, t->faite ? 'x' : ' ', t->titre, e[0] ? "  -> " : "", e);
  }
}

static void cmdAjouter(const char* a) {
  if (!*a) { Serial.println("Usage : ajouter <titre de la tâche>"); return; }
  if (Stockage::ajouterTache(a, 0)) Serial.println("Tâche ajoutée.");
  else Serial.println("Impossible d'ajouter la tâche (pleine ?).");
  Systeme::redessiner();
}

static void cmdFait(const char* a) {
  static int idx[TACHES_MAX];
  const int n = Stockage::listeTaches(TACHES_TOUTES, idx, TACHES_MAX);
  const int k = atoi(a);
  if (k < 1 || k > n) { Serial.println("Usage : fait <numéro> (voir « taches »)"); return; }
  const Tache* t = Stockage::tache(idx[k - 1]);
  Stockage::cocherTache(t->id, !t->faite);
  Serial.printf("« %s » : %s\r\n", t->titre, t->faite ? "faite" : "à faire");
  Systeme::redessiner();
}

static void cmdNotes(const char*) {
  if (Stockage::nbNotes() == 0) Serial.println("Aucune note.");
  for (int i = 0; i < Stockage::nbNotes(); i++) {
    char titre[64];
    Stockage::titreNote(Stockage::note(i), titre, sizeof(titre));
    Serial.printf("%3d. %s (%u octets)\r\n", i + 1, titre, (unsigned)strlen(Stockage::note(i)->texte));
  }
}

static void cmdNote(const char* a) {
  if (!*a) { Serial.println("Usage : note <texte>"); return; }
  if (Stockage::ajouterNote(a)) Serial.println("Note ajoutée.");
  Systeme::redessiner();
}

static void cmdVeille(const char*) {
  Serial.println("Mise en veille...");
  Systeme::mettreEnVeille();
}

static void cmdEteindre(const char*) {
  Serial.println("Extinction...");
  Systeme::eteindre();
}

static void cmdBatterie(const char*) {
  if (Reseau::actif()) Serial.println("(Wi-Fi allumé : ADC2 indisponible, dernière mesure)");
  Energie::mesurerMaintenant();
  Serial.printf("Batterie : %d mV, %d %%, %s%s\r\n", Energie::millivolts(), Energie::pourcentage(),
                Energie::enCharge() ? "en charge / USB" : "sur batterie",
                Energie::usbOrdinateur() ? ", PC détecté" : "");
}

static void cmdWifi(const char* a) {
  if (strncmp(a, "scan", 4) == 0) {
    if (!Reseau::lancerScan()) { Serial.println("Scan impossible maintenant."); return; }
    const uint32_t t0 = millis();
    while (!Reseau::scanFini() && millis() - t0 < 16000) { Reseau::boucle(); delay(50); }
    for (int i = 0; i < Reseau::nbScan(); i++) {
      Serial.printf("  %-32s %4d dBm %s\r\n", Reseau::scanSsid(i), Reseau::scanRssi(i), Reseau::scanOuvert(i) ? "ouvert" : "");
    }
    return;
  }
  if (strncmp(a, "connecter ", 10) == 0) {
    char ssid[33] = "", mdp[65] = "";
    const char* p = a + 10;
    const char* esp = strchr(p, ' ');
    if (esp) {
      strncpy(ssid, p, mini((int)(esp - p), 32));
      strncpy(mdp, esp + 1, 64);
    } else {
      strncpy(ssid, p, 32);
    }
    Reseau::connecter(ssid, mdp, true);
    Serial.printf("Connexion à « %s »... (le réseau sera mémorisé si elle réussit)\r\n", ssid);
    return;
  }
  if (strncmp(a, "oublier ", 8) == 0) {
    const int k = atoi(a + 8);
    if (k >= 1 && k <= Reseau::nbConnus()) { Reseau::oublier(k - 1); Serial.println("Réseau oublié."); }
    return;
  }
  if (strncmp(a, "sync", 4) == 0) { Reseau::synchroniserHeure(); Serial.println("Synchronisation lancée."); return; }
  if (strncmp(a, "off", 3) == 0) { Reseau::eteindre(); Serial.println("Wi-Fi éteint."); return; }
  Serial.printf("Wi-Fi : %s (mode %u)\r\n", Reseau::texteEtat(), (unsigned)Param.wifiMode);
  if (Reseau::etat() == RESEAU_CONNECTE) Serial.printf("IP %s, %d dBm\r\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  const int n = Reseau::nbConnus();
  for (int i = 0; i < n; i++) {
    char s[33];
    Reseau::ssidConnu(i, s, sizeof(s));
    Serial.printf("  %d. %s\r\n", i + 1, s);
  }
  Serial.println("Sous-commandes : wifi scan | wifi connecter <ssid> <mot de passe> | wifi oublier <n> | wifi sync | wifi off");
}

static void cmdHeure(const char* a) {
  if (strncmp(a, "ntp", 3) == 0) { Reseau::synchroniserHeure(); Serial.println("Synchronisation NTP lancée."); return; }
  int an, mo, j, h, mi;
  if (sscanf(a, "%d-%d-%d %d:%d", &an, &mo, &j, &h, &mi) == 5) {
    if (!Horloge::dateValide(Horloge::composer(an, mo, j)) || h < 0 || h > 23 || mi < 0 || mi > 59) {
      Serial.println("Date invalide.");
      return;
    }
    Horloge::reglerLocale(an, mo, j, h, mi);
    Systeme::redessiner();
  } else if (*a) {
    Serial.println("Usage : heure | heure AAAA-MM-JJ HH:MM | heure ntp");
  }
  afficherHeure();
}

static void cmdFuseau(const char* a) {
  if (*a) {
    strncpy(Param.fuseau, a, sizeof(Param.fuseau) - 1);
    Param.fuseau[sizeof(Param.fuseau) - 1] = 0;
    sauverParametres();
    Horloge::definirFuseau(Param.fuseau);
    Systeme::redessiner();
  }
  Serial.printf("Fuseau : %s\r\n", Param.fuseau);
}

static void cmdRafraichir(const char*) {
  Systeme::redessiner(true);
  Serial.println("Rafraîchissement complet demandé.");
}

static void cmdNettoyer(const char*) {
  Serial.println("Nettoyage de l'écran...");
  Systeme::nettoyerEcran();
}

static void cmdTactile(const char*) {
  Tactile::definirDebug(!Tactile::debug());
  Serial.printf("Trace du tactile : %s\r\n", Tactile::debug() ? "activée" : "désactivée");
}

static bool s_debugBoutons = false;
static void cmdBoutons(const char*) {
  s_debugBoutons = !s_debugBoutons;
  Boutons::definirDebug(s_debugBoutons);
  Serial.printf("Trace des boutons : %s\r\n", s_debugBoutons ? "activée" : "désactivée");
}

static void cmdMemoire(const char*) {
  Serial.printf("Interne : %lu libres (plus grand bloc %lu)\r\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("PSRAM   : %lu libres sur %lu\r\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned long)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  Serial.printf("Pile de loop() : %lu octets jamais utilisés\r\n", (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
}

static void cmdFichiers(const char*) { Stockage::listerFichiers(Serial); }
static void cmdExport(const char*) { Stockage::exporter(Serial); }

static void cmdReglages(const char*) {
  static const char* const NETTOYAGES[3] = {"jamais", "à chaque veille", "toutes les 50 mises à jour"};
  Serial.printf("veille %u s, heure en veille %u min, orientation %u, mise à jour %s, image complète auto : %s\r\n",
                (unsigned)Param.veilleS, (unsigned)Param.horlogeVeilleMin, (unsigned)Param.orientation,
                Param.modeMaj == MAJ_DOUX ? "douce" : "pixels", NETTOYAGES[Param.nettoyageAuto % 3]);
  Serial.printf("volume inversé %s, veille légère %s, veille sur USB %s, Wi-Fi mode %u\r\n",
                Param.inverserVolume ? "oui" : "non", Param.veilleLegere ? "oui" : "non",
                Param.veilleSurUSB ? "oui" : "non", (unsigned)Param.wifiMode);
  Serial.printf("pomodoro %u/%u/%u min x%u, minuteur %u min, fuseau %s, NTP %s, prénom « %s »\r\n",
                (unsigned)Param.pomoTravail, (unsigned)Param.pomoPause, (unsigned)Param.pomoLongue,
                (unsigned)Param.pomoCycles, (unsigned)Param.minuteurMin, Param.fuseau, Param.ntp, Param.prenom);
}

static void cmdLegere(const char* a) {
  if (strncmp(a, "on", 2) == 0) Param.veilleLegere = true;
  else if (strncmp(a, "off", 3) == 0) Param.veilleLegere = false;
  sauverParametres();
  Serial.printf("Veille légère : %s\r\n", Param.veilleLegere ? "activée" : "désactivée");
}

static void cmdRedemarrer(const char*) {
  Serial.println("Redémarrage...");
  Serial.flush();
  Systeme::redemarrer();
}

static void cmdReset(const char* a) {
  if (strcmp(a, "oui") != 0) {
    Serial.println("Efface TOUTES les tâches, notes, réglages et réseaux Wi-Fi.");
    Serial.println("Pour confirmer, tape : reset oui");
    s_resetDemande = true;
    return;
  }
  Stockage::toutEffacer();
  effacerParametres();
  Serial.println("Tout est effacé. Redémarrage...");
  Serial.flush();
  Systeme::redemarrer();
}

static const DefCommande COMMANDES[] = {
  {"aide", "help", cmdAide, "cette liste"},
  {"etat", "état", cmdEtat, "état général (heure, batterie, mémoire...)"},
  {"taches", "tâches", cmdTaches, "liste des tâches numérotées"},
  {"ajouter", nullptr, cmdAjouter, "ajouter <titre> : nouvelle tâche"},
  {"fait", nullptr, cmdFait, "fait <n> : coche / décoche la tâche n"},
  {"notes", nullptr, cmdNotes, "liste des notes"},
  {"note", nullptr, cmdNote, "note <texte> : nouvelle note"},
  {"veille", nullptr, cmdVeille, "mise en veille (sommeil profond)"},
  {"eteindre", "éteindre", cmdEteindre, "éteindre (réveil : appui long sur marche)"},
  {"batterie", nullptr, cmdBatterie, "mesure de la batterie"},
  {"wifi", nullptr, cmdWifi, "état du Wi-Fi ; wifi scan | connecter | oublier | sync | off"},
  {"heure", nullptr, cmdHeure, "heure | heure AAAA-MM-JJ HH:MM | heure ntp"},
  {"fuseau", nullptr, cmdFuseau, "fuseau <règle POSIX> (défaut " FUSEAU_DEFAUT ")"},
  {"rafraichir", "rafraîchir", cmdRafraichir, "rafraîchissement complet de l'écran"},
  {"nettoyer", nullptr, cmdNettoyer, "nettoyage anti-fantôme de l'écran"},
  {"tactile", nullptr, cmdTactile, "trace des coordonnées tactiles (marche/arrêt)"},
  {"boutons", nullptr, cmdBoutons, "trace des boutons (marche/arrêt)"},
  {"memoire", "mémoire", cmdMemoire, "mémoire libre"},
  {"fichiers", nullptr, cmdFichiers, "fichiers de la flash (FFat)"},
  {"export", nullptr, cmdExport, "toutes les tâches et notes en JSON (sauvegarde)"},
  {"reglages", "réglages", cmdReglages, "réglages actuels"},
  {"legere", "légère", cmdLegere, "legere on|off : veille légère"},
  {"redemarrer", "redémarrer", cmdRedemarrer, "redémarrer"},
  {"reset", nullptr, cmdReset, "effacer toutes les données (demande confirmation)"},
};
static const int NB_COMMANDES = sizeof(COMMANDES) / sizeof(COMMANDES[0]);

static void cmdAide(const char*) {
  Serial.println("Commandes :");
  for (int i = 0; i < NB_COMMANDES; i++) Serial.printf("  %-11s %s\r\n", COMMANDES[i].nom, COMMANDES[i].aide);
}

static void executer(char* ligne) {
  while (*ligne == ' ') ligne++;
  int n = (int)strlen(ligne);
  while (n > 0 && (ligne[n - 1] == ' ' || ligne[n - 1] == '\r')) ligne[--n] = 0;
  if (!*ligne) return;
  char* args = strchr(ligne, ' ');
  if (args) {
    *args++ = 0;
    while (*args == ' ') args++;
  } else {
    args = ligne + n;   // chaîne vide
  }
  for (int i = 0; i < NB_COMMANDES; i++) {
    const DefCommande& c = COMMANDES[i];
    if (strcmp(ligne, c.nom) == 0 || (c.alias && strcmp(ligne, c.alias) == 0)) {
      if (c.f != cmdReset) s_resetDemande = false;
      c.f(args);
      return;
    }
  }
  Serial.printf("Commande inconnue « %s ». Tape « aide ».\r\n", ligne);
}

void demarrer() {
  s_long = 0;
  Serial.println();
  Serial.printf("%s %s : console prête, tape « aide ».\r\n", PAMK_NOM, PAMK_VERSION);
}

void boucle() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    s_tCommande = millis();
    if (c == '\n' || c == '\r') {
      if (s_long > 0) {
        s_ligne[s_long] = 0;
        s_long = 0;
        Energie::activite();
        executer(s_ligne);
      }
    } else if (s_long < (int)sizeof(s_ligne) - 1) {
      s_ligne[s_long++] = (char)c;
    }
  }
}

bool active() { return s_tCommande != 0 && millis() - s_tCommande < 120000UL; }

}  // namespace Console
