#include "minuteries.h"
#include "langue.h"
#include "horloge.h"
#include "parametres.h"
#include "stats.h"

namespace Minuteries {

static const uint32_t MAGIE = 0x504D4B32;   // "PMK2" (28/10/2026 : + tâche du Pomodoro)

struct Etat {
  uint32_t magie;
  uint8_t phase;
  bool pomoSuspendu;
  uint8_t cycles;
  uint32_t pomoFin;        // UTC
  uint32_t pomoReste;      // secondes restantes si suspendu
  uint32_t pomoDuree;
  bool minActif;
  bool minSuspendu;
  uint32_t minFin;
  uint32_t minReste;
  uint32_t minDuree;
  char tache[ID_LONGUEUR + 1];   // tâche du Pomodoro (28/10/2026), "" = aucune
};

static RTC_DATA_ATTR Etat r;

void demarrer() {
  if (r.magie != MAGIE) {
    memset(&r, 0, sizeof(r));
    r.magie = MAGIE;
  }
}

static uint32_t maint() { return Horloge::maintenant(); }

static void lancerPhase(PhasePomo p) {
  uint32_t minutes = Param.pomoTravail;
  if (p == POMO_PAUSE) minutes = Param.pomoPause;
  else if (p == POMO_LONGUE) minutes = Param.pomoLongue;
  r.phase = p;
  r.pomoSuspendu = false;
  r.pomoDuree = minutes * 60UL;
  r.pomoFin = maint() + r.pomoDuree;
  r.pomoReste = r.pomoDuree;
}

// Statistiques : minutes réellement travaillées dans la phase de travail en cours (le temps
// passé en pause n'en fait pas partie). Appelé quand elle se termine, est passée ou arrêtée.
static void compterTravail() {
  if (r.phase != POMO_TRAVAIL) return;
  const int faites = (int)r.pomoDuree - pomoRestantS();
  if (faites > 0) Stats::ajouterMinutes((faites + 30) / 60);   // à la minute la plus proche
}

void pomoDemarrer() {
  compterTravail();   // relancé en plein travail : la phase interrompue compte quand même
  if (r.phase == POMO_ARRET && r.cycles >= Param.pomoCycles) r.cycles = 0;
  lancerPhase(POMO_TRAVAIL);
}

void pomoPauseReprise() {
  if (r.phase == POMO_ARRET) return;
  if (r.pomoSuspendu) {
    r.pomoFin = maint() + r.pomoReste;
    r.pomoSuspendu = false;
  } else {
    r.pomoReste = (uint32_t)pomoRestantS();
    r.pomoSuspendu = true;
  }
}

void pomoArreter() {
  compterTravail();
  r.phase = POMO_ARRET;
  r.pomoSuspendu = false;
  r.cycles = 0;
}

// Fin d'une phase : travail -> pause (lancée automatiquement) ; pause -> arrêt, prêt à relancer.
// aTerme : la phase est allée jusqu'au bout (sinon, elle a été passée).
static Alerte finPhase(bool aTerme) {
  if (r.phase == POMO_TRAVAIL) {
    if (aTerme) Stats::pomodoroTermine();
    compterTravail();
    r.cycles++;
    lancerPhase(r.cycles % (Param.pomoCycles ? Param.pomoCycles : 4) == 0 ? POMO_LONGUE : POMO_PAUSE);
    return ALERTE_FIN_TRAVAIL;
  }
  if (r.phase == POMO_PAUSE || r.phase == POMO_LONGUE) {
    r.phase = POMO_ARRET;
    r.pomoSuspendu = false;
    return ALERTE_FIN_PAUSE;
  }
  return ALERTE_AUCUNE;
}

void pomoPasser() {
  if (r.phase != POMO_ARRET) finPhase(false);
}

PhasePomo pomoPhase() { return (PhasePomo)r.phase; }
bool pomoSuspendu() { return r.pomoSuspendu; }
int pomoDureeS() { return (int)r.pomoDuree; }
int pomoCyclesFaits() { return r.cycles; }

int pomoRestantS() {
  if (r.phase == POMO_ARRET) return 0;
  if (r.pomoSuspendu) return (int)r.pomoReste;
  const uint32_t m = maint();
  return m >= r.pomoFin ? 0 : (int)(r.pomoFin - m);
}

const char* nomPhase(PhasePomo p) {
  switch (p) {
    case POMO_TRAVAIL: return tr("Travail");   // tr() : glissé dans des phrases (langue.h)
    case POMO_PAUSE:   return tr("Pause");
    case POMO_LONGUE:  return tr("Grande pause");
    default:           return tr("Prêt");
  }
}

void pomoChoisirTache(const char* id) {
  strncpy(r.tache, id ? id : "", sizeof(r.tache) - 1);
  r.tache[sizeof(r.tache) - 1] = 0;
}

const char* pomoTache() { return r.tache; }

void minDemarrer(int secondes) {
  if (secondes < 1) return;
  r.minActif = true;
  r.minSuspendu = false;
  r.minDuree = (uint32_t)secondes;
  r.minFin = maint() + r.minDuree;
  r.minReste = r.minDuree;
}

void minPauseReprise() {
  if (!r.minActif) return;
  if (r.minSuspendu) {
    r.minFin = maint() + r.minReste;
    r.minSuspendu = false;
  } else {
    r.minReste = (uint32_t)minRestantS();
    r.minSuspendu = true;
  }
}

void minArreter() {
  r.minActif = false;
  r.minSuspendu = false;
}

bool minActif() { return r.minActif; }
bool minSuspendu() { return r.minSuspendu; }
int minDureeS() { return (int)r.minDuree; }

int minRestantS() {
  if (!r.minActif) return 0;
  if (r.minSuspendu) return (int)r.minReste;
  const uint32_t m = maint();
  return m >= r.minFin ? 0 : (int)(r.minFin - m);
}

Alerte verifier() {
  const uint32_t m = maint();
  if (r.minActif && !r.minSuspendu && m >= r.minFin) {
    r.minActif = false;
    return ALERTE_FIN_MINUTEUR;
  }
  if (r.phase != POMO_ARRET && !r.pomoSuspendu && m >= r.pomoFin) return finPhase(true);
  return ALERTE_AUCUNE;
}

uint32_t prochaineEcheance() {
  uint32_t e = 0;
  if (r.minActif && !r.minSuspendu) e = r.minFin;
  if (r.phase != POMO_ARRET && !r.pomoSuspendu && (e == 0 || r.pomoFin < e)) e = r.pomoFin;
  return e;
}

void texteRestant(char* b, size_t n, int s) {
  if (s <= 0) { snprintf(b, n, "0 min"); return; }
  if (s < 60) { snprintf(b, n, "< 1 min"); return; }
  const int minutes = (s + 59) / 60;   // arrondi au-dessus : « 25 min » au départ
  if (minutes < 60) snprintf(b, n, "%d min", minutes);
  else snprintf(b, n, "%d h %02d", minutes / 60, minutes % 60);
}

}  // namespace Minuteries
