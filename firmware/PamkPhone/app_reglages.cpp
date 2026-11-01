// Réglages : Wi-Fi, heure, écran, veille, Pomodoro, boutons, prénom, à propos, données.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "ecran.h"
#include "energie.h"
#include "horloge.h"
#include "reseau.h"
#include "stockage.h"
#include "parametres.h"
#include "tactile.h"
#include "polices.h"
#include "stats.h"
#include "agenda.h"
#include "langue.h"
#include <esp_heap_caps.h>

namespace {

enum Code {
  P_WIFI, P_HEURE, P_ECRAN, P_ENERGIE, P_POMODORO, P_BOUTONS, P_PRENOM, P_APROPOS, P_DONNEES, P_LANGUE,
  W_MODE, W_SYNCHRO, W_CHERCHER, W_OUBLIER, W_RESEAU, W_ETEINDRE,
  H_FUSEAU, H_NTP, H_PLUS, H_MOINS, H_ENREGISTRER,
  E_ORIENTATION, E_MODE, E_COMPLET, E_NETTOYER, E_MIRE, E_TOUCHER, E_CLAVIER, E_TAILLE,
  N_VEILLE, N_HORLOGE, N_LEGERE, N_USB, N_DORMIR, N_ETEINDRE, N_NUIT, N_PROFOND, N_NPD,
  O_MOINS, O_PLUS,
  B_INVERSER,
  D_EFFACER, D_ACCES, A_ASSISTANT,
  M_FERMER,
};

const int RANG_H = 84;

struct Fuseau {
  const char* nom;
  const char* regle;
};
const Fuseau FUSEAUX[] = {
  {"Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Londres", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"UTC", "UTC0"},
  {"New York", "EST5EDT,M3.2.0,M11.1.0"},
  {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"La Réunion", "<+04>-4"},
  {"Antilles", "AST4"},
  {"Nouméa", "<+11>-11"},
  {"Tokyo", "JST-9"},
};
const int NB_FUSEAUX = sizeof(FUSEAUX) / sizeof(FUSEAUX[0]);

const uint16_t VEILLES[] = {30, 60, 120, 300, 600, 0};
const uint8_t HORLOGES[] = {0, 5, 15, 30, 60};
const uint8_t PROFONDS[] = {0, 1, 2, 4, 8, 12};   // heures de veille avant le sommeil profond

const char* nomFuseau() {
  for (int i = 0; i < NB_FUSEAUX; i++) if (strcmp(Param.fuseau, FUSEAUX[i].regle) == 0) return FUSEAUX[i].nom;
  return "personnalisé";
}

void texteVeille(uint16_t s, char* b, size_t n) {
  if (s == 0) snprintf(b, n, TR("jamais"));
  else if (s < 60) snprintf(b, n, "%u s", (unsigned)s);
  else snprintf(b, n, "%u min", (unsigned)(s / 60));
}

class AppReglages : public App {
 public:
  IdApp id() const override { return APP_REGLAGES; }
  bool barreEtat() const override { return m_page != PAGE_MIRE; }

  void ouvrirPage(PageReglages p) {
    m_page = p;
    m_racine = p;
    m_pages.page = 0;
    if (p == PAGE_HEURE) chargerEdition();
  }

  void dessiner() override {
    switch (m_page) {
      case PAGE_WIFI:      pageWifi(); break;
      case PAGE_WIFI_SCAN: pageScan(); break;
      case PAGE_HEURE:     pageHeure(); break;
      case PAGE_ECRAN:     pageEcran(); break;
      case PAGE_MIRE:      pageMire(); break;
      case PAGE_ENERGIE:   pageEnergie(); break;
      case PAGE_POMODORO:  pagePomodoro(); break;
      case PAGE_BOUTONS:   pageBoutons(); break;
      case PAGE_APROPOS:   pageAPropos(); break;
      case PAGE_DONNEES:   pageDonnees(); break;
      default:             pagePrincipale(); break;
    }
  }

  void action(int code, int arg) override {
    bool sauver = false;
    switch (code) {
      // --- Principale
      case P_WIFI:     aller(PAGE_WIFI); return;
      case P_HEURE:    chargerEdition(); aller(PAGE_HEURE); return;
      case P_ECRAN:    aller(PAGE_ECRAN); return;
      case P_ENERGIE:  aller(PAGE_ENERGIE); return;
      case P_POMODORO: aller(PAGE_POMODORO); return;
      case P_BOUTONS:  aller(PAGE_BOUTONS); return;
      case P_APROPOS:  aller(PAGE_APROPOS); return;
      case P_DONNEES:  aller(PAGE_DONNEES); return;
      case P_LANGUE:   Param.langue = anglais() ? LANGUE_FR : LANGUE_EN; sauver = true; break;
      case P_PRENOM:
        Systeme::ouvrir(appClavier("Ton prénom", Param.prenom, sizeof(Param.prenom) - 1, SAISIE_LIGNE, rappelPrenom, nullptr));
        return;
      // --- Wi-Fi
      case W_MODE: Param.wifiMode = (uint8_t)((Param.wifiMode + 1) % 3); sauver = true; break;
      case W_SYNCHRO: Reseau::synchroniserHeure(); break;
      case W_CHERCHER:
        Reseau::lancerScan();
        m_pages.page = 0;
        if (m_page != PAGE_WIFI_SCAN) { aller(PAGE_WIFI_SCAN); return; }
        break;
      case W_ETEINDRE: Reseau::eteindre(); break;
      case W_OUBLIER: Reseau::oublier(arg); break;
      case W_RESEAU: {
        const char* ssid = Reseau::scanSsid(arg);
        if (!*ssid) break;
        strncpy(m_ssid, ssid, sizeof(m_ssid) - 1);
        m_ssid[sizeof(m_ssid) - 1] = 0;
        if (Reseau::scanOuvert(arg)) {
          Reseau::connecter(m_ssid, "", true);
          m_page = PAGE_WIFI;
        } else {
          char titre[64];
          snprintf(titre, sizeof(titre), TR("Mot de passe de %s"), m_ssid);
          Systeme::ouvrir(appClavier(titre, "", 64, SAISIE_MOT_DE_PASSE, rappelMotDePasse, this));
          return;
        }
        break;
      }
      // --- Heure
      case H_FUSEAU: {
        int i = 0;
        for (; i < NB_FUSEAUX; i++) if (strcmp(Param.fuseau, FUSEAUX[i].regle) == 0) break;
        i = (i + 1) % NB_FUSEAUX;   // « personnalisé » -> Paris
        strncpy(Param.fuseau, FUSEAUX[i].regle, sizeof(Param.fuseau) - 1);
        Param.fuseau[sizeof(Param.fuseau) - 1] = 0;
        Horloge::definirFuseau(Param.fuseau);
        chargerEdition();
        sauver = true;
        break;
      }
      case H_NTP: Reseau::synchroniserHeure(); break;
      case H_PLUS:
      case H_MOINS: modifierChamp(arg, code == H_PLUS ? 1 : -1); break;
      case H_ENREGISTRER:
        Horloge::reglerLocale(m_edit[2], m_edit[1], m_edit[0], m_edit[3], m_edit[4]);
        Systeme::notifier("Heure enregistrée");
        break;
      // --- Écran
      case E_ORIENTATION:
        Param.orientation = Param.orientation ? 0 : 1;
        sauver = true;
        Systeme::appliquerParametres();
        Systeme::redessiner(true);
        break;
      case E_MODE: Param.modeMaj = Param.modeMaj == MAJ_DOUX ? MAJ_PIXELS : MAJ_DOUX; sauver = true; break;
      case E_COMPLET: Param.nettoyageAuto = (uint8_t)((Param.nettoyageAuto + 1) % 4); sauver = true; break;
      case E_TOUCHER: Param.retourToucher = !Param.retourToucher; sauver = true; break;
      case E_CLAVIER: Param.clavierRapide = !Param.clavierRapide; sauver = true; break;
      case E_TAILLE: Param.tailleTexte = Param.tailleTexte ? 0 : 1; sauver = true; break;
      case E_NETTOYER: Systeme::nettoyerEcran(); return;
      case E_MIRE: aller(PAGE_MIRE); return;
      case M_FERMER: m_page = PAGE_ECRAN; Systeme::redessiner(); return;
      // --- Énergie
      case N_VEILLE: {
        int i = 0;
        for (; i < 6; i++) if (VEILLES[i] == Param.veilleS) break;
        Param.veilleS = VEILLES[(i + 1) % 6];
        sauver = true;
        break;
      }
      case N_HORLOGE: Param.horlogeVeilleMin = suivant(HORLOGES, 5, Param.horlogeVeilleMin); sauver = true; break;
      case N_LEGERE: Param.veilleLegere = !Param.veilleLegere; sauver = true; break;
      case N_USB: Param.veilleSurUSB = !Param.veilleSurUSB; sauver = true; break;
      case N_NUIT: Param.ecoNuit = !Param.ecoNuit; sauver = true; break;
      case N_NPD: Param.nePasDeranger = !Param.nePasDeranger; sauver = true; break;
      case N_PROFOND: Param.sommeilApresH = suivant(PROFONDS, 6, Param.sommeilApresH); sauver = true; break;
      case N_DORMIR: Systeme::mettreEnVeille(); return;
      case N_ETEINDRE: Systeme::eteindre(); return;
      // --- Pomodoro
      case O_MOINS:
      case O_PLUS: reglerPomodoro(arg, code == O_PLUS ? 1 : -1); sauver = true; break;
      // --- Boutons
      case B_INVERSER: Param.inverserVolume = !Param.inverserVolume; sauver = true; break;
      // --- Données
      case D_ACCES: Systeme::ouvrir(appAcces()); return;
      case A_ASSISTANT: Systeme::ouvrir(appAssistant()); return;
      case D_EFFACER:
        Systeme::ouvrir(appDialogue("Tout effacer ?",
                                    "Tâches, notes, réglages et réseaux Wi-Fi seront effacés, puis le téléphone redémarre.",
                                    "Effacer", rappelEffacer, nullptr));
        return;
      case CODE_PAGE_PREC: m_pages.precedente(); break;
      case CODE_PAGE_SUIV: m_pages.suivante(); break;
      default: break;
    }
    if (sauver) {
      sauverParametres();
      Systeme::appliquerParametres();
    }
    Systeme::redessiner();
  }

  void toucherLibre(int x, int y) override {
    (void)x; (void)y;
    if (m_page == PAGE_MIRE) action(M_FERMER, 0);
  }

  bool bouton(Bouton b) override {
    if (m_page == PAGE_BOUTONS) {
      m_dernierBouton = (b == BOUTON_PLUS) ? "volume +" : "volume −";
      Systeme::redessiner();
      return true;
    }
    if (m_page == PAGE_WIFI_SCAN) {
      if ((b == BOUTON_PLUS) ? m_pages.suivante() : m_pages.precedente()) Systeme::redessiner();
      return true;
    }
    return false;
  }

  bool retour() override {
    if (m_page == m_racine) {
      m_page = PAGE_PRINCIPALE;
      m_racine = PAGE_PRINCIPALE;
      Systeme::retour();
      return true;
    }
    m_page = (m_page == PAGE_WIFI_SCAN) ? PAGE_WIFI : (m_page == PAGE_MIRE ? PAGE_ECRAN : PAGE_PRINCIPALE);
    m_pages.page = 0;
    Systeme::redessiner();
    return true;
  }

 private:
  PageReglages m_page = PAGE_PRINCIPALE;
  PageReglages m_racine = PAGE_PRINCIPALE;
  Pages m_pages = {0, 1};
  char m_ssid[33] = "";
  int m_edit[5] = {1, 1, 2026, 12, 0};   // jour, mois, année, heure, minute
  const char* m_dernierBouton = "appuie sur volume + ou −";

  void aller(PageReglages p) {
    m_page = p;
    m_pages.page = 0;
    Systeme::redessiner();
  }

  static uint8_t suivant(const uint8_t* valeurs, int n, uint8_t actuel) {
    for (int i = 0; i < n; i++) if (valeurs[i] == actuel) return valeurs[(i + 1) % n];
    return valeurs[0];
  }

  void chargerEdition() {
    struct tm t;
    Horloge::locale(t);
    if (Horloge::valide() && t.tm_year + 1900 >= 2024) {
      m_edit[0] = t.tm_mday;
      m_edit[1] = t.tm_mon + 1;
      m_edit[2] = t.tm_year + 1900;
      m_edit[3] = t.tm_hour;
      m_edit[4] = t.tm_min;
    }
  }

  void modifierChamp(int champ, int d) {
    static const int MIN[5] = {1, 1, 2024, 0, 0};
    static const int MAX[5] = {31, 12, 2099, 23, 59};
    if (champ < 0 || champ > 4) return;
    int v = m_edit[champ] + d;
    if (v > MAX[champ]) v = MIN[champ];
    if (v < MIN[champ]) v = MAX[champ];
    m_edit[champ] = v;
    const int jm = Horloge::joursDansMois(m_edit[2], m_edit[1]);
    if (m_edit[0] > jm) m_edit[0] = (champ == 0 && d > 0) ? 1 : jm;
  }

  void reglerPomodoro(int champ, int d) {
    switch (champ) {
      case 0: Param.pomoTravail = (uint8_t)borner(Param.pomoTravail + d * 5, 5, 120); break;
      case 1: Param.pomoPause = (uint8_t)borner(Param.pomoPause + d, 1, 30); break;
      case 2: Param.pomoLongue = (uint8_t)borner(Param.pomoLongue + d * 5, 5, 60); break;
      case 3: Param.pomoCycles = (uint8_t)borner(Param.pomoCycles + d, 1, 8); break;
      default: break;
    }
  }

  static void rappelPrenom(bool ok, const char* texte, void* ctx) {
    (void)ctx;
    if (!ok || !texte) return;
    strncpy(Param.prenom, texte, sizeof(Param.prenom) - 1);
    Param.prenom[sizeof(Param.prenom) - 1] = 0;
    sauverParametres();
  }

  static void rappelMotDePasse(bool ok, const char* texte, void* ctx) {
    AppReglages* moi = static_cast<AppReglages*>(ctx);
    if (!ok) return;
    Reseau::connecter(moi->m_ssid, texte ? texte : "", true);
    moi->m_page = PAGE_WIFI;
  }

  static void rappelEffacer(bool oui, void* ctx) {
    (void)ctx;
    if (!oui) return;
    Stockage::toutEffacer();
    Stats::effacer();
    Agenda::effacer();
    effacerParametres();
    Systeme::redemarrer();
  }

  // --- Pages ----------------------------------------------------------------
  void pagePrincipale() {
    UI::barreTitre("Réglages", true);
    static const char* const MODES[3] = {"éteint", "auto", "toujours"};
    char v[48];
    int y = CONTENU_Y;
    if (Reseau::etat() == RESEAU_CONNECTE) snprintf(v, sizeof(v), TR("connecté"));
    else snprintf(v, sizeof(v), "%s", MODES[Param.wifiMode % 3]);
    UI::ligneReglage(y, "Wi-Fi", v, P_WIFI); y += RANG_H;
    if (Horloge::valide()) {
      struct tm t;
      Horloge::locale(t);
      Horloge::texteHeure(v, sizeof(v), t);
    } else {
      snprintf(v, sizeof(v), TR("à régler"));
    }
    UI::ligneReglage(y, "Heure et date", v, P_HEURE); y += RANG_H;
    UI::ligneReglage(y, "Écran", Param.modeMaj == MAJ_DOUX ? "doux" : "pixels", P_ECRAN); y += RANG_H;
    texteVeille(Param.veilleS, v, sizeof(v));
    UI::ligneReglage(y, "Veille et batterie", v, P_ENERGIE); y += RANG_H;
    snprintf(v, sizeof(v), "%u / %u / %u", (unsigned)Param.pomoTravail, (unsigned)Param.pomoPause, (unsigned)Param.pomoLongue);
    UI::ligneReglage(y, "Pomodoro", v, P_POMODORO); y += RANG_H;
    if (BOUTONS_PRESENTS) {
      UI::ligneReglage(y, "Boutons", Param.inverserVolume ? "inversés" : "normaux", P_BOUTONS); y += RANG_H;
    }
    UI::ligneReglage(y, "Prénom", Param.prenom[0] ? Param.prenom : "—", P_PRENOM); y += RANG_H;
    // 01/11/2026 : 9 lignes, jusqu'à y = 888 (une de plus avec les boutons : à resserrer alors)
    UI::ligneReglage(y, "Langue", anglais() ? "English" : "Français", P_LANGUE); y += RANG_H;
    UI::ligneReglage(y, "À propos", "V0 " PAMK_VERSION, P_APROPOS); y += RANG_H;
    snprintf(v, sizeof(v), TR("%d tâches · %d notes"), Stockage::nbTaches(), Stockage::nbNotes());
    UI::ligneReglage(y, "Données", v, P_DONNEES);
  }

  void pageWifi() {
    UI::barreTitre("Wi-Fi", true);
    int y = CONTENU_Y + 16;
    Dessin::pictoWifi(MARGE, y + 14, Reseau::barres(), Reseau::etat() == RESEAU_CONNECTE ? NOIR : GRIS_CLAIR);
    Dessin::texteTronque(F_GRAS, MARGE + 48, y + 34, ECRAN_L - 2 * MARGE - 48, Reseau::texteEtat(), NOIR);
    y += 64;
    static const char* const MODES[3] = {"éteint", "auto (heure)", "toujours"};
    UI::ligneReglage(y, "Mode", MODES[Param.wifiMode % 3], W_MODE); y += RANG_H;
    UI::ligneReglage(y, "Mettre l'heure à jour", "", W_SYNCHRO); y += RANG_H;
    UI::ligneReglage(y, "Chercher un réseau", "", W_CHERCHER); y += RANG_H;
    if (Reseau::actif()) { UI::ligneReglage(y, "Éteindre le Wi-Fi", "", W_ETEINDRE); y += RANG_H; }
    UI::etiquette(MARGE, y + 46, "RÉSEAUX ENREGISTRÉS");
    y += 64;
    const int n = Reseau::nbConnus();
    if (n == 0) {
      Dessin::texte(F_TEXTE, MARGE, y + 40, "Aucun pour l'instant", GRIS_FONCE);
      y += 64;
    }
    for (int i = 0; i < n; i++) {
      char s[33];
      Reseau::ssidConnu(i, s, sizeof(s));
      Dessin::texteTronque(F_TEXTE, MARGE, y + 46, ECRAN_L - 2 * MARGE - 150, s, NOIR);
      UI::bouton(boite(ECRAN_L - MARGE - 130, y + 10, 130, 56), "Oublier", W_OUBLIER, i, STYLE_DISCRET, F_PETITE);
      UI::separateur(y + 74);
      y += 76;
    }
    Dessin::texte(F_PETITE, MARGE, mini(y + 40, 930), "Le Wi-Fi sert à mettre l'heure ; en mode auto il s'éteint ensuite.", GRIS_FONCE);
  }

  void pageScan() {
    UI::barreTitre("Réseaux", true, "Relancer", W_CHERCHER);
    if (!Reseau::scanFini()) {
      UI::messageVide(300, "Recherche en cours…", "Quelques secondes.");
      return;
    }
    const int n = Reseau::nbScan();
    const int parPage = (PIED_Y - CONTENU_Y - 8) / RANG_H;   // 8
    m_pages.regler(n, parPage);
    if (n == 0) UI::messageVide(300, "Aucun réseau trouvé", "Touche « Relancer ».");
    for (int i = 0; i < parPage; i++) {
      const int k = m_pages.page * parPage + i;
      if (k >= n) break;
      const int y = CONTENU_Y + 8 + i * RANG_H;
      const int r = Reseau::scanRssi(k);
      const int barres = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0;
      Dessin::texteTronque(F_TEXTE, MARGE, y + 40, ECRAN_L - 2 * MARGE - 60, Reseau::scanSsid(k), NOIR);
      const char* info = Reseau::estConnu(Reseau::scanSsid(k)) ? "enregistré" : (Reseau::scanOuvert(k) ? "ouvert" : "protégé");
      Dessin::texte(F_PETITE, MARGE, y + 70, info, GRIS_FONCE);
      Dessin::pictoWifi(ECRAN_L - MARGE - 30, y + 30, barres, NOIR);
      UI::separateur(y + RANG_H - 2);
      UI::zone(boite(0, y, ECRAN_L, RANG_H), W_RESEAU, k);
    }
    UI::pagination(m_pages);
  }

  void pageHeure() {
    UI::barreTitre("Heure et date", true);
    struct tm t;
    Horloge::locale(t);
    char h[8], d[64], l[96];
    if (Horloge::valide()) {
      Horloge::texteHeure(h, sizeof(h), t);
      Horloge::texteDateLongue(d, sizeof(d), Horloge::dateDe(t), true);
      d[0] = (char)toupper((unsigned char)d[0]);
    } else {
      strcpy(h, "--:--");
      strcpy(d, "Heure inconnue");
    }
    Dessin::texteCentre(F_GRANDE, ECRAN_L / 2, 246, h, NOIR);
    Dessin::texteCentre(F_GRAS, ECRAN_L / 2, 292, d, NOIR);
    snprintf(l, sizeof(l), TR("Source : %s · horloge de la carte : %s"), tr(Horloge::source()),
             tr(Horloge::rtcPresente() ? "oui" : "absente"));
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 330, l, GRIS_FONCE);
    UI::ligneReglage(348, "Fuseau horaire", nomFuseau(), H_FUSEAU);
    UI::ligneReglage(348 + RANG_H, "Mettre à l'heure par Wi-Fi", "", H_NTP);
    UI::etiquette(MARGE, 568, "RÉGLAGE MANUEL");
    static const char* const NOMS[5] = {"jour", "mois", "année", "heure", "min"};
    static const int LARGEURS[5] = {80, 80, 112, 80, 80};   // 432 + 4 x 15 = 492 px
    int x = MARGE;
    for (int i = 0; i < 5; i++) {
      const int lc = LARGEURS[i];
      UI::bouton(boite(x, 588, lc, 56), "+", H_PLUS, i, STYLE_DISCRET);
      char v[8];
      snprintf(v, sizeof(v), i == 2 ? "%04d" : "%02d", m_edit[i]);
      Dessin::texteCentre(F_TITRE, x + lc / 2, 704, v, NOIR);
      UI::bouton(boite(x, 718, lc, 56), "-", H_MOINS, i, STYLE_DISCRET);
      Dessin::texteCentre(F_PETITE, x + lc / 2, 800, NOMS[i], GRIS_FONCE);
      x += lc + 15;
    }
    UI::bouton(boite(MARGE, 826, ECRAN_L - 2 * MARGE, 76), "Enregistrer cette heure", H_ENREGISTRER, 0, STYLE_PLEIN);
  }

  void pageEcran() {
    UI::barreTitre("Écran", true);
    int y = CONTENU_Y;
    UI::ligneReglage(y, "Retourner l'écran", Param.orientation == ORIENTATION_DEFAUT ? "non" : "oui", E_ORIENTATION); y += RANG_H;
    UI::ligneReglage(y, "Mise à jour", Param.modeMaj == MAJ_DOUX ? "sans clignotement" : "pixel par pixel", E_MODE); y += RANG_H;
    static const char* const AUTO[4] = {"jamais", "à la veille", "toutes les 50", "auto"};
    UI::ligneReglage(y, "Image complète auto", AUTO[Param.nettoyageAuto % 4], E_COMPLET); y += RANG_H;
    UI::ligneInterrupteur(y, "Réaction au toucher", Param.retourToucher, E_TOUCHER); y += RANG_H;
    UI::ligneInterrupteur(y, "Clavier rapide", Param.clavierRapide, E_CLAVIER); y += RANG_H;
    UI::ligneReglage(y, "Taille du texte", Param.tailleTexte ? "grande" : "normale", E_TAILLE); y += RANG_H;
    UI::ligneReglage(y, "Nettoyer l'écran", "", E_NETTOYER); y += RANG_H;
    UI::ligneReglage(y, "Mire de gris", "test", E_MIRE); y += RANG_H;
    y += 26;
    const char* aide[] = {
      "Image complète auto : quand l'écran a beaucoup servi,",
      "il repart propre à la mise en veille (on ne regarde plus).",
    };
    for (int i = 0; i < 2; i++) Dessin::texte(F_PETITE, MARGE, y + i * 30, aide[i], GRIS_FONCE);
    char info[112];
    snprintf(info, sizeof(info), TR("Dernière mise à jour : %s, %lu ms"), tr(Ecran::dernierMode()),
             (unsigned long)Ecran::dureeDerniereMs());
    Dessin::texteTronque(F_PETITE, MARGE, 920, ECRAN_L - 2 * MARGE, info, GRIS);
  }

  void pageMire() {
    // 16 niveaux de gris en bandes, pour juger le rendu de la dalle. Toucher pour quitter.
    const int h = ECRAN_H / 16;
    for (int i = 0; i < 16; i++) {
      Dessin::rectPlein(0, i * h, ECRAN_L, h, (uint8_t)i);
      char n[4];
      snprintf(n, sizeof(n), "%d", i);
      Dessin::texte(F_PIXEL, MARGE, i * h + 44, n, i < 8 ? BLANC : NOIR);
    }
    Dessin::chanfreinPlein(ECRAN_L - 250, 8, 226, 52, 8, BLANC);
    Dessin::texteCentre(F_PETITE, ECRAN_L - 137, 42, "toucher pour quitter", NOIR);
  }

  void pageEnergie() {
    UI::barreTitre("Veille et batterie", true);
    char v[40];
    int y = CONTENU_Y;
    texteVeille(Param.veilleS, v, sizeof(v));
    UI::ligneReglage(y, "Mise en veille après", v, N_VEILLE); y += RANG_H;
    if (Param.horlogeVeilleMin) snprintf(v, sizeof(v), TR("toutes les %u min"), (unsigned)Param.horlogeVeilleMin);
    else snprintf(v, sizeof(v), "non");
    UI::ligneReglage(y, "Heure en veille", v, N_HORLOGE); y += RANG_H;
    UI::ligneInterrupteur(y, "Veille légère", Param.veilleLegere, N_LEGERE); y += RANG_H;
    UI::ligneInterrupteur(y, "Veille même branché au PC", Param.veilleSurUSB, N_USB); y += RANG_H;
    UI::ligneInterrupteur(y, "Mode nuit (23 h - 7 h)", Param.ecoNuit, N_NUIT); y += RANG_H;
    UI::ligneInterrupteur(y, "Mode Ne pas déranger", Param.nePasDeranger, N_NPD); y += RANG_H;
    if (Param.sommeilApresH) snprintf(v, sizeof(v), TR("après %u h"), (unsigned)Param.sommeilApresH);
    else snprintf(v, sizeof(v), TR("jamais"));
    UI::ligneReglage(y, "Sommeil profond", v, N_PROFOND); y += RANG_H;
    // Bloc batterie compact : 7 lignes de réglages au-dessus, boutons en 836 (29/10/2026).
    UI::etiquette(MARGE, y + 16, "BATTERIE");
    char b[64];
    if (Energie::mesureValide()) {
      const int mv = Energie::millivolts();
      snprintf(b, sizeof(b), "%d,%02d V · %d %%", mv / 1000, (mv % 1000) / 10, Energie::pourcentage());
    } else {
      snprintf(b, sizeof(b), TR("pas encore mesurée"));
    }
    Dessin::texte(F_GRAS, MARGE, y + 52, b, NOIR);
    const char* usb = Energie::usbOrdinateur() ? "Ordinateur USB" :
                      (Energie::enCharge() ? "En charge (USB ?)" : "Sur batterie");
    char ligne[96];
    char autonomie[48];
    if (Energie::texteAutonomie(autonomie, sizeof(autonomie))) {
      snprintf(ligne, sizeof(ligne), TR("%s · autonomie %s"), usb, autonomie);
    } else {
      snprintf(ligne, sizeof(ligne), "%s", usb);
    }
    Dessin::texteTronque(F_PETITE, MARGE, y + 84, ECRAN_L - 2 * MARGE, ligne, GRIS_FONCE);
    const int lb = (ECRAN_L - 2 * MARGE - 16) / 2;
    UI::bouton(boite(MARGE, 836, lb, 76), "Mettre en veille", N_DORMIR);
    UI::bouton(boite(MARGE + lb + 16, 836, lb, 76), "Éteindre", N_ETEINDRE);
  }

  void pagePomodoro() {
    UI::barreTitre("Pomodoro", true);
    const char* noms[4] = {"Travail", "Pause", "Grande pause", "Cycles avant la grande"};
    char v[16];
    for (int i = 0; i < 4; i++) {
      const int y = CONTENU_Y + 16 + i * 104;
      int val = 0;
      switch (i) {
        case 0: val = Param.pomoTravail; break;
        case 1: val = Param.pomoPause; break;
        case 2: val = Param.pomoLongue; break;
        default: val = Param.pomoCycles; break;
      }
      if (i < 3) snprintf(v, sizeof(v), "%d min", val);
      else snprintf(v, sizeof(v), "%d", val);
      Dessin::texte(F_TEXTE, MARGE, y + 36, noms[i], NOIR);
      Dessin::texte(F_GRAS, MARGE, y + 76, v, GRIS_FONCE);
      UI::bouton(boite(ECRAN_L - MARGE - 188, y + 12, 84, 72), "-", O_MOINS, i);
      UI::bouton(boite(ECRAN_L - MARGE - 88, y + 12, 84, 72), "+", O_PLUS, i);
      UI::separateur(y + 100);
    }
    const char* aide[] = {
      "À la fin du travail, la pause démarre toute seule et",
      "un écran noir te prévient (pas de son en V0), même",
      "si le téléphone était en veille.",
    };
    for (int i = 0; i < 3; i++) Dessin::texte(F_PETITE, MARGE, 620 + i * 30, aide[i], GRIS_FONCE);
  }

  void pageBoutons() {
    UI::barreTitre("Boutons", true);
    UI::ligneInterrupteur(CONTENU_Y, "Inverser volume + et −", Param.inverserVolume, B_INVERSER);
    struct Ligne { const char* titre; const char* texte; };
    const Ligne lignes[] = {
      {"Marche (dessus)", "court : veille · long (2 s) : éteindre"},
      {"Volume +", "suivant, page suivante, + · long : accueil"},
      {"Volume −", "précédent, page précédente, − · long : retour"},
      {"Les deux ensemble", "accueil"},
      {"Glisser du bord gauche", "retour"},
    };
    int y = CONTENU_Y + RANG_H + 20;
    for (int i = 0; i < 5; i++) {
      Dessin::texte(F_GRAS, MARGE, y + 30, lignes[i].titre, NOIR);
      Dessin::texte(F_PETITE, MARGE, y + 62, lignes[i].texte, GRIS_FONCE);
      y += 84;
    }
    UI::etiquette(MARGE, y + 40, "TEST");
    Dessin::texte(F_TEXTE, MARGE, y + 84, m_dernierBouton, NOIR);
  }

  void pageAPropos() {
    UI::barreTitre("À propos", true);
    Dessin::visagePamk(ECRAN_L / 2, CONTENU_Y + 24, 12, VISAGE_CONTENT, NOIR);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, CONTENU_Y + 170, PAMK_NOM, NOIR);
    Dessin::texteCentre(F_PIXEL, ECRAN_L / 2, CONTENU_Y + 214, "V0 - " PAMK_VERSION, GRIS_FONCE);
    char l[10][80];
    const int mv = Energie::millivolts();
    snprintf(l[0], 80, TR("Carte : LilyGO T5 4.7\" ESP32-S3 (V2.3)"));
    snprintf(l[1], 80, TR("Batterie : %d,%02d V (%d %%)"), mv / 1000, (mv % 1000) / 10, Energie::pourcentage());
    snprintf(l[2], 80, TR("Mémoire libre : %lu Kio, PSRAM : %lu Kio"),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    snprintf(l[3], 80, TR("Stockage : %lu / %lu Kio"), (unsigned long)(Stockage::octetsUtilises() / 1024),
             (unsigned long)(Stockage::octetsTotal() / 1024));
    snprintf(l[4], 80, TR("Tactile : %s, horloge : %s"), tr(Tactile::present() ? "GT911" : "absent"),
             tr(Horloge::rtcPresente() ? "PCF8563" : "absente"));
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(l[5], 80, TR("Adresse MAC : %02X:%02X:%02X:%02X:%02X:%02X"), (unsigned)(mac & 0xFF),
             (unsigned)((mac >> 8) & 0xFF), (unsigned)((mac >> 16) & 0xFF), (unsigned)((mac >> 24) & 0xFF),
             (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
    snprintf(l[6], 80, TR("Réveil : %s"), Energie::texteCauseReveil());
    snprintf(l[7], 80, TR("Écran : %lu mises à jour depuis le démarrage"), (unsigned long)Ecran::nbRafraichissements());
    snprintf(l[8], 80, TR("Polices Barlow et VT323 (SIL OFL 1.1)"));
    snprintf(l[9], 80, TR("Pilote d'écran LilyGo-EPD47 1.1.0 (GPL-3.0)"));
    for (int i = 0; i < 10; i++) Dessin::texte(F_PETITE, MARGE, CONTENU_Y + 270 + i * 36, l[i], i < 8 ? NOIR : GRIS_FONCE);
    UI::bouton(boite(MARGE, 836, ECRAN_L - 2 * MARGE, 76), "Revoir le premier démarrage", A_ASSISTANT);
  }

  void pageDonnees() {
    UI::barreTitre("Données", true);
    char l[96];
    int y = CONTENU_Y + 50;
    snprintf(l, sizeof(l), TR("Tâches : %d (dont %d à faire)"), Stockage::nbTaches(), Stockage::nbTachesAFaire());
    Dessin::texte(F_TEXTE, MARGE, y, l, NOIR); y += 44;
    snprintf(l, sizeof(l), TR("Notes : %d"), Stockage::nbNotes());
    Dessin::texte(F_TEXTE, MARGE, y, l, NOIR); y += 44;
    snprintf(l, sizeof(l), TR("Suppressions mémorisées : %d"), Stockage::nbTraces());
    Dessin::texte(F_TEXTE, MARGE, y, l, NOIR); y += 44;
    snprintf(l, sizeof(l), TR("Flash utilisée : %lu / %lu Kio"), (unsigned long)(Stockage::octetsUtilises() / 1024),
             (unsigned long)(Stockage::octetsTotal() / 1024));
    Dessin::texte(F_TEXTE, MARGE, y, l, NOIR); y += 70;
    const char* aide[] = {
      "Chaque tâche et chaque note a un identifiant et",
      "une date de modification ; les suppressions sont notées.",
      "",
      "Sauvegarde : « Accès depuis l'ordinateur » ouvre une",
      "page web pour tout lire, modifier et télécharger.",
      "(Ou « export » dans le moniteur série, 115200 bauds.)",
    };
    for (int i = 0; i < 6; i++) Dessin::texte(F_PETITE, MARGE, y + i * 30, aide[i], GRIS_FONCE);
    UI::bouton(boite(MARGE, 740, ECRAN_L - 2 * MARGE, 76), "Accès depuis l'ordinateur", D_ACCES, 0, STYLE_PLEIN);
    UI::bouton(boite(MARGE, 836, ECRAN_L - 2 * MARGE, 76), "Tout effacer…", D_EFFACER);
  }
};

AppReglages g_reglages;

}  // namespace

App* appReglages() {
  g_reglages.ouvrirPage(PAGE_PRINCIPALE);
  return &g_reglages;
}

App* appReglagesPage(PageReglages page) {
  g_reglages.ouvrirPage(page);
  return &g_reglages;
}
