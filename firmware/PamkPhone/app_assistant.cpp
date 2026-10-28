// Premier démarrage guidé : une étape par écran, « Passer » à chaque étape.
// Bienvenue -> Prénom -> Wi-Fi -> Heure -> Les gestes -> C'est prêt. Le Wi-Fi et l'heure
// manuelle s'ouvrent dans les Réglages : en revenant (glisser du bord gauche ou flèche), on
// arrive à l'étape suivante. Ici, glisser du bord gauche = étape précédente.
// Ouvert par Systeme::demarrer() tant que Param.assistantFait est faux.
#include "app.h"
#include "systeme.h"
#include "ui.h"
#include "horloge.h"
#include "reseau.h"
#include "parametres.h"

namespace {

enum Etape : int8_t { E_BIENVENUE, E_PRENOM, E_WIFI, E_HEURE, E_GESTES, E_PRET, E_NB };
enum Code { C_SUIVANTE, C_TERMINER, C_PRENOM, C_WIFI, C_HEURE, C_REESSAYER };

const int AVANCEMENT_Y = 70;                    // petits carrés : une case par étape
const int TITRE_Y = 262;                        // ligne de base du titre sous le petit visage
const int TEXTE_Y = 316;
const int TEXTE_L = ECRAN_L - 2 * MARGE;        // 492
const Boite BOUTON_1 = {MARGE, 740, ECRAN_L - 2 * MARGE, 76};   // action de l'étape (plein)
const Boite BOUTON_2 = {MARGE, 836, ECRAN_L - 2 * MARGE, 76};   // « Passer » (discret)

struct Aide {
  const char* titre;
  const char* texte;
};
const Aide GESTES[3] = {
  {"Revenir en arrière", "Glisse le doigt du bord gauche vers la droite."},
  {"Revenir à l'accueil", "Appui long sur la flèche en haut à gauche."},
  {"Réveiller", BOUTONS_PRESENTS ? "En veille, appuie sur le bouton du dessus." : "En veille, touche simplement l'écran."},
};

// Paragraphe coupé aux espaces, centré dans [x, x + largeurMax] ou aligné à gauche.
// Retourne la ligne de base suivante.
int paragraphe(const GFXfont* p, int x, int y, int largeurMax, const char* s, uint8_t c, bool centre) {
  LigneTexte lignes[6];
  const int n = Dessin::couperLignes(p, s, largeurMax, lignes, 6);
  const int pas = Dessin::hauteurLigne(p) + 4;   // 38 pour F_TEXTE
  for (int i = 0; i < n; i++) {
    const int nb = lignes[i].fin - lignes[i].debut;
    const int dx = centre ? (largeurMax - Dessin::largeurN(p, s + lignes[i].debut, nb)) / 2 : 0;
    Dessin::texteN(p, x + dx, y, s + lignes[i].debut, nb, c);
    y += pas;
  }
  return y;
}

// Une ligne centrée, coupée avec « … » si elle dépasse la largeur de l'écran.
void ligneCentree(const GFXfont* p, int y, const char* s, uint8_t c) {
  const int l = mini(Dessin::largeur(p, s), TEXTE_L);
  Dessin::texteTronque(p, (ECRAN_L - l) / 2, y, TEXTE_L, s, c);
}

class AppAssistant : public App {
 public:
  IdApp id() const override { return APP_ASSISTANT; }
  bool reprendreApresVeille() const override { return false; }

  void recommencer() {
    m_etape = E_BIENVENUE;
    m_apresRetour = -1;
  }

  // Retour d'une page des Réglages (Wi-Fi, heure) : on passe à l'étape suivante.
  // (Le retour du clavier, lui, est traité par rappelPrenom.)
  void entrer() override {
    if (m_apresRetour < 0) return;
    const Etape e = (Etape)m_apresRetour;
    m_apresRetour = -1;
    aller(e);
  }

  void dessiner() override {
    dessinerAvancement();
    switch (m_etape) {
      case E_BIENVENUE: bienvenue(); break;
      case E_PRENOM:    prenom(); break;
      case E_WIFI:      wifi(); break;
      case E_HEURE:     heure(); break;
      case E_GESTES:    gestes(); break;
      default:          pret(); break;
    }
  }

  void action(int code, int arg) override {
    (void)arg;
    switch (code) {
      case C_SUIVANTE: aller((Etape)(m_etape + 1)); break;
      case C_TERMINER: terminer(); break;
      case C_PRENOM:
        Systeme::ouvrir(appClavier("Ton prénom", Param.prenom, sizeof(Param.prenom) - 1, SAISIE_LIGNE, rappelPrenom, this));
        break;
      case C_WIFI:
        m_apresRetour = E_HEURE;
        Systeme::ouvrir(appReglagesPage(PAGE_WIFI));
        break;
      case C_HEURE:
        m_apresRetour = E_GESTES;
        Systeme::ouvrir(appReglagesPage(PAGE_HEURE));
        break;
      case C_REESSAYER:
        Reseau::synchroniserHeure();
        Systeme::redessiner();
        break;
      default: break;
    }
  }

  // Glisser depuis le bord gauche : étape précédente (rien avant la bienvenue).
  bool retour() override {
    if (m_etape > E_BIENVENUE) aller((Etape)(m_etape - 1));
    return true;
  }

 private:
  Etape m_etape = E_BIENVENUE;
  int8_t m_apresRetour = -1;   // étape où aller en revenant des Réglages (-1 = aucune)

  void aller(Etape e) {
    m_etape = (e >= E_BIENVENUE && e < E_NB) ? e : E_PRET;
    // Heure : mise à l'heure automatique si un réseau est enregistré. Sans effet si le Wi-Fi
    // s'en occupe déjà (réseau choisi juste avant : la connexion lance la synchro NTP).
    if (m_etape == E_HEURE && !Horloge::ntpRecu() && Reseau::nbConnus() > 0) Reseau::synchroniserHeure();
    Systeme::redessiner();
  }

  void terminer() {
    Param.assistantFait = true;
    sauverParametres();
    Systeme::accueil();
  }

  static void rappelPrenom(bool ok, const char* texte, void* ctx) {
    AppAssistant* moi = static_cast<AppAssistant*>(ctx);
    if (!ok || !texte) return;   // annulé : on reste sur l'étape
    while (*texte == ' ') texte++;
    strncpy(Param.prenom, texte, sizeof(Param.prenom) - 1);
    Param.prenom[sizeof(Param.prenom) - 1] = 0;
    int l = (int)strlen(Param.prenom);
    while (l > 0 && Param.prenom[l - 1] == ' ') Param.prenom[--l] = 0;
    sauverParametres();
    if (Param.prenom[0]) moi->aller(E_WIFI);
  }

  // --- Morceaux communs ------------------------------------------------------
  void dessinerAvancement() {
    const int c = 14, ecart = 12;
    const int x0 = (ECRAN_L - E_NB * c - (E_NB - 1) * ecart) / 2;
    for (int i = 0; i < E_NB; i++) {
      const int x = x0 + i * (c + ecart);
      if (i < m_etape) Dessin::rectPlein(x, AVANCEMENT_Y, c, c, GRIS);
      else if (i == m_etape) Dessin::rectPlein(x, AVANCEMENT_Y, c, c, NOIR);
      else Dessin::rect(x, AVANCEMENT_Y, c, c, 2, GRIS);
    }
  }

  // Petit visage de Pamk et titre pixel.
  void entete(const char* titre) {
    Dessin::visagePamk(ECRAN_L / 2, 112, 10, VISAGE_NORMAL, NOIR);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, TITRE_Y, titre, NOIR);
  }

  void boutons(const char* principal, int codePrincipal, const char* secondaire, int codeSecondaire) {
    UI::bouton(BOUTON_1, principal, codePrincipal, 0, STYLE_PLEIN);
    if (secondaire) UI::bouton(BOUTON_2, secondaire, codeSecondaire, 0, STYLE_DISCRET);
  }

  // --- Étapes ------------------------------------------------------------------
  void bienvenue() {
    Dessin::visagePamk(ECRAN_L / 2, 132, 20, VISAGE_NORMAL, NOIR);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 404, "Bonjour !", NOIR);
    paragraphe(F_TEXTE, MARGE, 470, TEXTE_L,
               "Je suis Pamk, ton téléphone tout simple. On me prépare ensemble ? Ça prend deux minutes.",
               GRIS_FONCE, true);
    Dessin::texteCentre(F_PETITE, ECRAN_L / 2, 704, "Tout se règle aussi plus tard dans Réglages.", GRIS_FONCE);
    boutons("Commencer", C_SUIVANTE, "Passer", C_TERMINER);   // ici, « Passer » saute tout
  }

  void prenom() {
    entete("Ton prénom");
    paragraphe(F_TEXTE, MARGE, TEXTE_Y, TEXTE_L, "Comment tu t'appelles ? Je te dirai bonjour sur l'accueil.",
               GRIS_FONCE, true);
    if (Param.prenom[0]) {
      ligneCentree(F_TITRE, 520, Param.prenom, NOIR);
      boutons("Continuer", C_SUIVANTE, "Modifier", C_PRENOM);
    } else {
      boutons("Écrire mon prénom", C_PRENOM, "Passer", C_SUIVANTE);
    }
  }

  void wifi() {
    entete("Le Wi-Fi");
    paragraphe(F_TEXTE, MARGE, TEXTE_Y, TEXTE_L,
               "Je m'en sers pour me mettre à l'heure tout seul, puis je l'éteins pour ménager la batterie.",
               GRIS_FONCE, true);
    const int n = Reseau::nbConnus();
    if (n > 0) {
      char s[33], l[64];
      Reseau::ssidConnu(n - 1, s, sizeof(s));   // le dernier enregistré
      snprintf(l, sizeof(l), "Réseau enregistré : %s", s);
      ligneCentree(F_GRAS, 490, l, NOIR);
    }
    const EtatReseau er = Reseau::etat();
    if (Reseau::occupe() || er == RESEAU_CONNECTE || er == RESEAU_ECHEC) {
      ligneCentree(F_PETITE, 530, Reseau::texteEtat(), GRIS_FONCE);
    }
    if (n > 0) boutons("Continuer", C_SUIVANTE, "Changer de réseau", C_WIFI);
    else boutons("Choisir un réseau", C_WIFI, "Passer", C_SUIVANTE);
  }

  void heure() {
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 170, "L'heure", NOIR);
    const bool valide = Horloge::valide();
    struct tm t;
    Horloge::locale(t);
    char h[8], d[64];
    if (valide) {
      Horloge::texteHeure(h, sizeof(h), t);
      Horloge::texteDateLongue(d, sizeof(d), Horloge::dateDe(t), true);
      d[0] = (char)toupper((unsigned char)d[0]);
    } else {
      strcpy(h, "--:--");
      strcpy(d, "Heure inconnue");
    }
    Dessin::texteCentre(F_GRANDE, ECRAN_L / 2, 330, h, valide ? NOIR : GRIS);
    ligneCentree(F_GRAS, 386, d, NOIR);
    // Où en est la mise à l'heure (l'écran se redessine quand l'état du Wi-Fi change)
    const bool enCours = Reseau::occupe() || Reseau::etat() == RESEAU_CONNECTE;
    const bool connu = Reseau::nbConnus() > 0;
    const char* etat;
    if (Horloge::ntpRecu()) etat = "Je me suis mis à l'heure par Wi-Fi.";
    else if (enCours) etat = "Je me mets à l'heure par Wi-Fi…";
    else if (Reseau::etat() == RESEAU_ECHEC) etat = Reseau::texteEtat();
    else if (valide) etat = "Heure gardée par l'horloge de la carte.";
    else if (connu) etat = "Pas encore à l'heure.";
    else etat = "Sans Wi-Fi, règle-la à la main.";
    paragraphe(F_TEXTE, MARGE, 456, TEXTE_L, etat, GRIS_FONCE, true);
    if (connu && !enCours && !Horloge::ntpRecu()) {
      UI::bouton(boite(ECRAN_L / 2 - 130, 540, 260, 64), "Réessayer", C_REESSAYER);
    }
    if (valide) boutons("Continuer", C_SUIVANTE, "Régler à la main", C_HEURE);
    else boutons("Régler à la main", C_HEURE, "Passer", C_SUIVANTE);
  }

  void gestes() {
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 170, "Les gestes", NOIR);
    const int x = MARGE + 124;
    for (int i = 0; i < 3; i++) {
      const int y = 214 + i * 160;
      pictoGeste(i, MARGE, y + 8);
      Dessin::texteTronque(F_GRAS, x, y + 36, ECRAN_L - MARGE - x, GESTES[i].titre, NOIR);
      paragraphe(F_TEXTE, x, y + 74, ECRAN_L - MARGE - x, GESTES[i].texte, GRIS_FONCE, false);
    }
    boutons("Compris", C_SUIVANTE, "Passer", C_SUIVANTE);
  }

  // Pictogramme d'un geste, dans un cadre de 100 x 100.
  void pictoGeste(int i, int x, int y) {
    Dessin::chanfrein(x, y, 100, 100, CHANFREIN, 2, GRIS);
    const int cy = y + 50;
    switch (i) {
      case 0:   // bord de l'écran, puis flèche vers la droite
        Dessin::rectPlein(x + 14, y + 18, 6, 64, NOIR);
        Dessin::rectPlein(x + 30, cy - 3, 42, 6, NOIR);
        Dessin::icone(ICONE_CHEVRON_D, x + 58, cy - 14, 4, NOIR);
        break;
      case 1:   // flèche retour tenue (cercle), puis la maison
        Dessin::cercle(x + 31, cy, 22, 3, GRIS);
        Dessin::icone(ICONE_CHEVRON_G, x + 21, cy - 14, 4, NOIR);
        Dessin::icone(ICONE_ACCUEIL, x + 57, cy - 18, 3, NOIR);
        break;
      default:  // doigt posé et ondes
        Dessin::cercle(x + 50, cy, 38, 3, GRIS_CLAIR);
        Dessin::cercle(x + 50, cy, 25, 3, GRIS);
        Dessin::cerclePlein(x + 50, cy, 11, NOIR);
        break;
    }
  }

  void pret() {
    Dessin::visagePamk(ECRAN_L / 2, 132, 20, VISAGE_CONTENT, NOIR);
    Dessin::texteCentre(F_TITRE, ECRAN_L / 2, 404, "C'est prêt !", NOIR);
    char l[128];
    if (Param.prenom[0]) {
      snprintf(l, sizeof(l), "À toi de jouer, %s ! Tout se règle dans Applications, puis Réglages.", Param.prenom);
    } else {
      snprintf(l, sizeof(l), "À toi de jouer ! Tout se règle dans Applications, puis Réglages.");
    }
    paragraphe(F_TEXTE, MARGE, 470, TEXTE_L, l, GRIS_FONCE, true);
    boutons("C'est parti", C_TERMINER, nullptr, 0);
  }
};

AppAssistant g_assistant;

}  // namespace

App* appAssistant() {
  g_assistant.recommencer();
  return &g_assistant;
}
