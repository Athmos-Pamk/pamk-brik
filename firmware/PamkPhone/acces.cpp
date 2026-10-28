// Accès depuis l'ordinateur : petite page web (port 80) et mise à jour sans fil depuis
// l'Arduino IDE (ArduinoOTA), protégées par un code à 4 chiffres tiré au hasard.
// Les requêtes sont traitées dans boucle(), dans la même tâche que l'interface : aucune
// concurrence avec Stockage ni avec l'écran.
#include "acces.h"
#include "app.h"
#include "systeme.h"
#include "reseau.h"
#include "stockage.h"
#include "agenda.h"
#include "horloge.h"
#include "energie.h"
#include "parametres.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_random.h>
#include <new>

namespace {

const char* const NOM_RESEAU = "pamk-phone";      // ArduinoOTA et mDNS (http://pamk-phone.local)
const int ECHECS_MAX = 5;                         // codes faux d'affilée avant la pause
const uint32_t PAUSE_ECHECS_MS = 60000;
const uint32_t REESSAI_WIFI_MS = 15000;
const uint32_t INACTIF_MAX_MS = 10UL * 60UL * 1000UL;   // ni requête ni toucher : on arrête tout
const uint32_t MESSAGE_MS = 10000;                // durée d'un message d'échec à l'écran

// ---------------------------------------------------------------------------
//  La page (HTML + JS, sans rien d'extérieur). Les données sont insérées par textContent /
//  value, jamais en HTML : un titre de tâche ne peut pas injecter de code.
// ---------------------------------------------------------------------------
const char PAGE[] PROGMEM = R"PAMK(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pamk Phone</title>
<style>
:root{--n:#111;--g:#666;--c:#d6d6d2;--f:#f4f4f1}
*{box-sizing:border-box}
body{margin:0;background:var(--f);color:var(--n);font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:780px;margin:0 auto;padding:16px}
h1{font:700 30px/1 ui-monospace,Consolas,monospace;margin:12px 0 2px}
h2{font:700 14px/1 ui-monospace,Consolas,monospace;letter-spacing:2px;text-transform:uppercase;color:var(--g);margin:0 0 12px}
section{background:#fff;border:2px solid var(--n);padding:16px;margin:16px 0}
button,input,select,textarea{font:inherit;color:inherit}
button{background:var(--n);color:#fff;border:2px solid var(--n);padding:7px 14px;cursor:pointer}
button.d{background:#fff;color:var(--n)}
button:disabled{opacity:.35;cursor:default}
input,select,textarea{border:2px solid var(--c);background:#fff;padding:6px 8px;min-width:0}
input:focus,select:focus,textarea:focus{outline:none;border-color:var(--n)}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--n)}
textarea{width:100%;min-height:90px;resize:vertical;display:block;margin-bottom:8px}
.l{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.x{flex:1 1 220px}
.t{display:grid;grid-template-columns:auto 1fr auto;gap:6px 10px;align-items:center;padding:10px 0;border-bottom:1px solid var(--c)}
.t .o{grid-column:2/4;display:flex;gap:6px;flex-wrap:wrap}
.t.f .ti{text-decoration:line-through;color:var(--g)}
.n{padding:12px 0;border-bottom:1px solid var(--c)}
.m{color:var(--g);font-size:14px;margin:8px 0 0}
.e{color:#b00020}
progress{width:100%;height:14px;margin-top:10px;accent-color:var(--n)}
</style></head><body><main>
<h1>Pamk Phone</h1><div class="m" id="ver">accès depuis l'ordinateur</div>
<section id="entree"><h2>Code</h2>
<form id="fc" class="l"><input id="code" class="x" inputmode="numeric" maxlength="4" autocomplete="off" placeholder="code à 4 chiffres"><button>Entrer</button></form>
<p class="m">Le code est affiché en grand sur l'écran du téléphone.</p><p id="ce" class="m e"></p></section>
<div id="app" hidden>
<p id="msg" class="m"></p>
<section><h2>Tâches</h2>
<form id="ft" class="l"><input id="nt" class="x" maxlength="120" placeholder="Nouvelle tâche"><input id="nd" type="date" title="Échéance (facultative)"><button>Ajouter</button></form>
<div id="taches"></div></section>
<section><h2>Notes</h2>
<form id="fn"><textarea id="nn" placeholder="Nouvelle note (la première ligne sert de titre)"></textarea><button>Ajouter la note</button></form>
<div id="notes"></div></section>
<section><h2>Agenda</h2><p id="ai" class="m"></p>
<form id="fa" class="l" style="margin-top:10px"><input id="au" class="x" type="url" autocomplete="off" placeholder="Adresse iCal de ton agenda (https://…/basic.ics)"><button>Enregistrer</button></form>
<div class="l" style="margin-top:8px"><button id="as" class="d">Mettre à jour maintenant</button><button id="ar" class="d">Retirer l'agenda</button></div>
<p class="m">Google Agenda, sur ordinateur : Paramètres › dans « Paramètres de mes agendas », clique sur ton agenda › « Intégrer l'agenda » › <b>Adresse secrète au format iCal</b>. Garde-la secrète : qui l'a peut lire ton agenda. Le téléphone garde les événements d'hier à dans 60 jours.</p></section>
<section><h2>Sauvegarde</h2><p class="m" style="margin:0 0 10px">Toutes les tâches et notes dans un fichier JSON.</p>
<button id="bs">Télécharger la sauvegarde</button></section>
<section><h2>Nouveau firmware</h2>
<p class="m" style="margin:0 0 10px">Arduino IDE : Croquis › Exporter les binaires compilés, puis choisis <b>PamkPhone.ino.bin</b> dans le dossier build (pas le « bootloader » ni les « partitions »). Le téléphone vérifie le fichier puis redémarre.</p>
<div class="l"><input id="ff" class="x" type="file" accept=".bin"><button id="bf">Envoyer</button></div>
<progress id="pf" max="100" value="0" hidden></progress><p id="mf" class="m"></p></section>
</div></main>
<script>
"use strict";
let code = "", reps = [];
try { code = sessionStorage.getItem("pamk-code") || ""; } catch (e) {}
const $ = i => document.getElementById(i);
function el(t, p, ...f) { const e = document.createElement(t); Object.assign(e, p || {}); e.append(...f); return e; }
function msg(t, err) { $("msg").textContent = t || ""; $("msg").className = err ? "m e" : "m"; }
function verrouiller(t) {
  code = ""; try { sessionStorage.removeItem("pamk-code"); } catch (e) {}
  $("app").hidden = true; $("entree").hidden = false; $("ce").textContent = t || "";
}
async function api(chemin, donnees) {
  const o = { headers: { "X-Code": code } };
  if (donnees) { o.method = "POST"; o.body = new URLSearchParams(donnees); }
  const r = await fetch(chemin, o);
  let j = {}; try { j = await r.json(); } catch (e) {}
  if (r.status === 403) { verrouiller("Code incorrect."); throw new Error("code"); }
  if (!r.ok) throw new Error(j.erreur || ("erreur " + r.status));
  return j;
}
async function agir(quoi, donnees) {
  try { await api("/api/" + quoi, donnees); msg(""); } catch (e) { if (e.message === "code") return; msg(e.message, true); }
  if (quoi === "tache") chargerTaches(); else chargerNotes();
}
function ligneTache(t) {
  const c = el("input", { type: "checkbox", checked: t.faite, title: "Faite" });
  c.onchange = () => agir("tache", { action: "cocher", id: t.id, faite: c.checked ? 1 : 0 });
  const ti = el("input", { className: "ti", value: t.titre, maxLength: 120 });
  ti.onchange = () => agir("tache", { action: "renommer", id: t.id, titre: ti.value });
  const s = el("button", { className: "d", textContent: "Supprimer" });
  s.onclick = () => { if (confirm("Supprimer « " + t.titre + " » ?")) agir("tache", { action: "supprimer", id: t.id }); };
  const d = el("input", { type: "date", value: t.echeance, title: "Échéance" });
  d.onchange = () => agir("tache", { action: "echeance", id: t.id, echeance: d.value });
  const h = el("input", { type: "time", value: t.heure, title: "Heure" });
  h.onchange = () => agir("tache", { action: "heure", id: t.id, heure: h.value });
  const r = el("select", { title: "Répétition" });
  reps.forEach((n, i) => r.append(el("option", { value: i, textContent: i ? n : "ne se répète pas", selected: i === t.rep })));
  r.onchange = () => agir("tache", { action: "repetition", id: t.id, rep: r.value });
  return el("div", { className: "t" + (t.faite ? " f" : "") }, c, ti, s, el("div", { className: "o" }, d, h, r));
}
function blocNote(n) {
  const ta = el("textarea", { value: n.texte });
  const e = el("button", { textContent: "Enregistrer" });
  e.onclick = () => agir("note", { action: "modifier", id: n.id, texte: ta.value });
  const s = el("button", { className: "d", textContent: "Supprimer" });
  s.onclick = () => { if (confirm("Supprimer cette note ?")) agir("note", { action: "supprimer", id: n.id }); };
  return el("div", { className: "n" }, ta, el("div", { className: "l" }, e, s, el("span", { className: "m", textContent: "modifiée " + n.quand })));
}
async function chargerTaches() {
  try {
    const j = await api("/api/taches"); reps = j.reps;
    $("taches").replaceChildren(...j.taches.map(ligneTache));
    if (!j.taches.length) $("taches").append(el("p", { className: "m", textContent: "Aucune tâche." }));
  } catch (e) { if (e.message !== "code") msg(e.message, true); }
}
async function chargerNotes() {
  try {
    const j = await api("/api/notes");
    $("notes").replaceChildren(...j.notes.map(blocNote));
    if (!j.notes.length) $("notes").append(el("p", { className: "m", textContent: "Aucune note." }));
  } catch (e) { if (e.message !== "code") msg(e.message, true); }
}
function etat(j) {
  $("ver").textContent = "accès depuis l'ordinateur · version " + j.version;
  const a = j.agenda; let t;
  if (!a.configure) t = "Pas d'agenda pour l'instant : colle ci-dessous l'adresse iCal secrète.";
  else {
    t = "Agenda : " + a.hote + " · " + a.evenements + " événement(s) gardé(s)";
    if (a.synchro) t += " · mis à jour " + a.synchro;
    if (a.enCours) t += " · mise à jour en cours…"; else if (a.erreur) t += " · dernier essai : " + a.erreur;
  }
  $("ai").textContent = t; $("as").disabled = $("ar").disabled = !a.configure;
  return a;
}
async function suivreAgenda() {
  for (let i = 0; i < 45; i++) {
    await new Promise(r => setTimeout(r, 2000));
    try { if (!etat(await api("/api/etat")).enCours) return; } catch (e) { return; }
  }
}
async function agenda(donnees) {
  try { await api("/api/agenda", donnees); msg(""); } catch (e) { if (e.message !== "code") msg(e.message, true); return false; }
  try { etat(await api("/api/etat")); } catch (e) {}
  suivreAgenda(); return true;
}
async function entrer(c) {
  code = c;
  try {
    const j = await api("/api/etat");
    try { sessionStorage.setItem("pamk-code", c); } catch (e) {}
    $("entree").hidden = true; $("app").hidden = false; $("ce").textContent = "";
    etat(j); chargerTaches(); chargerNotes();
  } catch (e) { if (e.message !== "code") $("ce").textContent = e.message; }
}
$("fc").onsubmit = e => { e.preventDefault(); entrer($("code").value.trim()); };
$("ft").onsubmit = e => {
  e.preventDefault(); if (!$("nt").value.trim()) return;
  agir("tache", { action: "ajouter", titre: $("nt").value, echeance: $("nd").value }); $("nt").value = "";
};
$("fn").onsubmit = e => {
  e.preventDefault(); if (!$("nn").value.trim()) return;
  agir("note", { action: "ajouter", texte: $("nn").value }); $("nn").value = "";
};
$("fa").onsubmit = async e => { e.preventDefault(); if (await agenda({ action: "enregistrer", url: $("au").value.trim() })) $("au").value = ""; };
$("as").onclick = () => agenda({ action: "synchroniser" });
$("ar").onclick = () => { if (confirm("Retirer l'agenda du téléphone ?")) agenda({ action: "retirer" }); };
$("bs").onclick = async () => {
  try {
    const r = await fetch("/api/sauvegarde", { headers: { "X-Code": code } });
    if (r.status === 403) return verrouiller("Code incorrect.");
    if (!r.ok) throw new Error("erreur " + r.status);
    const a = el("a", { href: URL.createObjectURL(await r.blob()), download: "pamk-phone-" + new Date().toISOString().slice(0, 10) + ".json" });
    document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  } catch (e) { msg(e.message, true); }
};
$("bf").onclick = () => {
  const f = $("ff").files[0];
  if (!f) { $("mf").textContent = "Choisis d'abord le fichier .bin."; return; }
  if (!confirm("Installer " + f.name + " (" + Math.round(f.size / 1024) + " Kio) ? Le téléphone redémarrera.")) return;
  const x = new XMLHttpRequest();
  x.open("POST", "/api/firmware");
  x.setRequestHeader("X-Code", code);
  x.setRequestHeader("Content-Type", "application/octet-stream");
  $("pf").hidden = false; $("pf").value = 0; $("bf").disabled = true; $("mf").textContent = "Envoi…";
  x.upload.onprogress = e => { if (e.lengthComputable) $("pf").value = Math.round(e.loaded * 100 / e.total); };
  x.onload = () => {
    $("bf").disabled = false;
    let j = {}; try { j = JSON.parse(x.responseText); } catch (e) {}
    if (x.status === 200) { $("pf").value = 100; $("mf").textContent = "Installé : le téléphone redémarre. Rouvre « Accès depuis l'ordinateur » dessus pour revenir ici (nouveau code)."; }
    else if (x.status === 403) verrouiller("Code incorrect.");
    else $("mf").textContent = "Refusé : " + (j.erreur || x.status);
  };
  x.onerror = () => { $("bf").disabled = false; $("mf").textContent = "Envoi interrompu."; };
  x.send(f);
};
if (code) entrer(code);
setInterval(() => { if (code && $("app").hidden === false) api("/api/etat").then(etat).catch(() => {}); }, 30000);
</script></body></html>)PAMK";

// ---------------------------------------------------------------------------
//  État
// ---------------------------------------------------------------------------
WebServer s_serveur(80);
// ArduinoOTA n'accepte son mot de passe qu'une fois (setPassword) : une instance neuve par
// session, construite sur place (pas de tas), pour que chaque nouveau code soit pris en compte.
alignas(ArduinoOTAClass) uint8_t s_otaMemoire[sizeof(ArduinoOTAClass)];
ArduinoOTAClass* s_ota = nullptr;

bool s_actif = false;
bool s_pret = false;
bool s_routes = false;
char s_code[5] = "";
char s_adresse[32] = "";
char s_etat[72] = "arrêté";
int s_pct = -1;
uint32_t s_tEssai = 0;
uint32_t s_tActivite = 0;
uint32_t s_redemarrerA = 0;        // millis() du redémarrage après une mise à jour (0 = aucun)
uint32_t s_messageJusqua = 0;
int s_echecs = 0;
uint32_t s_bloqueJusqua = 0;       // 0 = pas de pause
void (*s_rappel)() = nullptr;
// Envoi du firmware par la page
bool s_majCodeOk = false;
size_t s_majTotal = 0;
char s_majErreur[64] = "";
int s_indices[TACHES_MAX];

void texte(const char* t) {
  strncpy(s_etat, t ? t : "", sizeof(s_etat) - 1);
  s_etat[sizeof(s_etat) - 1] = 0;
}

void afficher() {
  if (s_rappel) s_rappel();
}

uint32_t dans(uint32_t ms) {       // échéance millis() jamais nulle
  const uint32_t t = millis() + ms;
  return t ? t : 1;
}

bool bloque() {
  if (s_bloqueJusqua && (int32_t)(millis() - s_bloqueJusqua) >= 0) s_bloqueJusqua = 0;
  return s_bloqueJusqua != 0;
}

void compterEchec() {
  if (++s_echecs < ECHECS_MAX) return;
  s_echecs = 0;
  s_bloqueJusqua = dans(PAUSE_ECHECS_MS);
  JOURNAL("[acces] %d codes faux : pause d'une minute", ECHECS_MAX);
}

// En-tête X-Code : 0 bon, 403 faux, 429 en pause après trop d'essais.
int verifierCode() {
  if (bloque()) return 429;
  const String c = s_serveur.header("X-Code");
  if (s_code[0] && c.length() == 4 && strcmp(c.c_str(), s_code) == 0) {
    s_echecs = 0;
    s_tActivite = millis();
    return 0;
  }
  compterEchec();
  return bloque() ? 429 : 403;
}

// --- Réponses -----------------------------------------------------------------
// Sortie JSON en morceaux (« chunked ») : les listes peuvent peser plusieurs centaines de Kio.
class SortieWeb : public Print {
 public:
  using Print::write;
  void commencer() { m_n = 0; }
  size_t write(uint8_t c) override {
    m_t[m_n++] = c;
    if (m_n == sizeof(m_t)) vider();
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    for (size_t i = 0; i < n; i++) write(b[i]);
    return n;
  }
  void vider() {
    if (m_n) s_serveur.sendContent((const char*)m_t, m_n);
    m_n = 0;
  }

 private:
  uint8_t m_t[1024];
  size_t m_n = 0;
};
SortieWeb s_sortie;

void ecrireTexte(Print& p, const char* s) {
  p.write('"');
  for (; s && *s; s++) {
    const uint8_t c = (uint8_t)*s;
    switch (c) {
      case '"':  p.print("\\\""); break;
      case '\\': p.print("\\\\"); break;
      case '\n': p.print("\\n"); break;
      case '\r': p.print("\\r"); break;
      case '\t': p.print("\\t"); break;
      default:
        if (c < 0x20) p.printf("\\u%04x", (unsigned)c);
        else p.write(c);
    }
  }
  p.write('"');
}

SortieWeb& commencerJson() {
  s_serveur.sendHeader("Cache-Control", "no-store");
  s_serveur.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_serveur.send(200, "application/json", "");
  s_sortie.commencer();
  return s_sortie;
}

void repondreOk() {
  s_serveur.sendHeader("Cache-Control", "no-store");
  s_serveur.send(200, "application/json", "{\"ok\":true}");
}

void repondreErreur(int code, const char* message) {
  String j = "{\"erreur\":";
  // message court et sans guillemets (textes de ce fichier, d'Update ou de Stockage)
  j += '"';
  for (const char* p = message; p && *p; p++) if (*p != '"' && *p != '\\' && (uint8_t)*p >= 0x20) j += *p;
  j += "\"}";
  s_serveur.send(code, "application/json", j);
}

bool autoriser() {   // sinon, la réponse d'erreur est déjà envoyée
  const int r = verifierCode();
  if (r == 0) return true;
  repondreErreur(r, r == 429 ? "trop de codes faux : attends une minute" : "code incorrect");
  return false;
}

// « 14:30 » -> 870 ; -1 si vide ; -2 si invalide
int lireHeure(const String& s) {
  if (!s.length()) return -1;
  if (s.length() < 5 || s[2] != ':' || !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
      !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) return -2;
  const int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
  return (h < 24 && m < 60) ? h * 60 + m : -2;
}

// « 2026-10-28 » -> 20261028 ; 0 si vide ; -1 si invalide
int lireDate(const String& s) {
  if (!s.length()) return 0;
  const int d = Horloge::dateDepuisISO(s.c_str());
  return d ? d : -1;
}

void texteQuand(char* b, size_t n, uint32_t utc) {
  if (!utc) { snprintf(b, n, "—"); return; }
  struct tm t;
  Horloge::localeDe(utc, t);
  snprintf(b, n, "le %02d/%02d à %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
}

// --- Mise à jour du firmware (page web et Arduino IDE) --------------------------
void noterErreurMaj() {   // s_majErreur en français pour les cas courants
  const char* t;
  switch (Update.getError()) {
    case UPDATE_ERROR_SIZE:
    case UPDATE_ERROR_SPACE:        t = "taille refusée (fichier trop gros ?)"; break;
    case UPDATE_ERROR_MAGIC_BYTE:   t = "ce n'est pas un firmware ESP32 (.bin)"; break;
    case UPDATE_ERROR_ACTIVATE:     t = "image refusée (autre puce ou fichier abîmé)"; break;
    case UPDATE_ERROR_NO_PARTITION: t = "pas de seconde partition (schéma de partition ?)"; break;
    case UPDATE_ERROR_WRITE:
    case UPDATE_ERROR_ERASE:
    case UPDATE_ERROR_READ:         t = "erreur d'écriture dans la flash"; break;
    default:                        t = Update.errorString(); break;
  }
  strncpy(s_majErreur, t, sizeof(s_majErreur) - 1);
  s_majErreur[sizeof(s_majErreur) - 1] = 0;
}

void debutMaj() {
  s_pct = 0;
  s_tActivite = millis();
  texte("mise à jour 0 %");
  JOURNAL("[acces] mise à jour du firmware…");
  afficher();
}

void progresser(size_t fait, size_t total) {
  s_tActivite = millis();
  if (!total) return;
  const int palier = (int)((uint64_t)fait * 100 / total) / 10 * 10;
  if (palier == s_pct || palier >= 100) return;   // 100 % : c'est finMajReussie() qui l'affiche
  s_pct = palier;
  snprintf(s_etat, sizeof(s_etat), "mise à jour %d %%", palier);
  afficher();
}

void finMajReussie() {
  s_pct = 100;
  texte("mise à jour réussie, redémarrage…");
  JOURNAL("[acces] mise à jour réussie, redémarrage");
  afficher();
  s_redemarrerA = dans(1500);
}

void echecMaj(const char* raison) {
  s_pct = -1;
  snprintf(s_etat, sizeof(s_etat), "mise à jour ratée : %s", raison);
  s_messageJusqua = dans(MESSAGE_MS);
  JOURNAL("[acces] mise à jour ratée : %s", raison);
  Systeme::redessiner();
}

// Le .bin arrive brut (Content-Type: application/octet-stream) : WebServer le passe par
// morceaux de 1436 octets, avec un délai maximal par octet (pas de blocage sans fin).
void recevoirMaj() {
  if (s_serveur.header("Content-Type").startsWith("multipart/")) return;   // pas de formulaire
  HTTPRaw& r = s_serveur.raw();
  switch (r.status) {
    case RAW_START: {
      s_majErreur[0] = 0;
      s_majTotal = 0;
      s_majCodeOk = verifierCode() == 0;
      if (!s_majCodeOk) return;
      const int taille = s_serveur.clientContentLength();
      if (taille <= 0) {
        strcpy(s_majErreur, "fichier vide");
        return;
      }
      if (!Update.begin((size_t)taille, U_FLASH)) {   // trop gros pour la partition…
        noterErreurMaj();
        return;
      }
      s_majTotal = (size_t)taille;
      debutMaj();
      break;
    }
    case RAW_WRITE:
      if (!s_majCodeOk || s_majErreur[0] || !Update.isRunning()) return;
      // Le premier morceau est vérifié (octet magique 0xE9) : un mauvais fichier s'arrête là.
      if (Update.write(r.buf, r.currentSize) != r.currentSize) {
        noterErreurMaj();
        Update.abort();
        return;
      }
      progresser(r.totalSize, s_majTotal);
      break;
    case RAW_END:
      if (!s_majCodeOk || s_majErreur[0] || !Update.isRunning()) return;
      // Vérifie la taille et l'image (somme de contrôle, puce) avant de la rendre amorçable.
      if (!Update.end()) noterErreurMaj();
      break;
    case RAW_ABORTED:
      if (Update.isRunning()) Update.abort();
      if (s_majCodeOk && s_majTotal) echecMaj("envoi interrompu");
      break;
  }
}

void routeFinMaj() {
  if (s_serveur.header("Content-Type").startsWith("multipart/")) {
    repondreErreur(400, "envoie le fichier .bin depuis la page");
    return;
  }
  if (!s_majCodeOk) {   // échec déjà compté dans recevoirMaj()
    const bool b = bloque();
    repondreErreur(b ? 429 : 403, b ? "trop de codes faux : attends une minute" : "code incorrect");
    return;
  }
  if (s_majErreur[0]) {
    echecMaj(s_majErreur);
    repondreErreur(400, s_majErreur);
    return;
  }
  repondreOk();
  finMajReussie();
}

// --- Routes ---------------------------------------------------------------------
void routePage() {
  s_serveur.sendHeader("Cache-Control", "no-store");
  s_serveur.send_P(200, "text/html; charset=utf-8", PAGE);
}

void routeEtat() {
  if (!autoriser()) return;
  SortieWeb& o = commencerJson();
  o.printf("{\"version\":\"%s\",\"taches\":%d,\"notes\":%d,\"agenda\":{\"configure\":%s",
           PAMK_VERSION, Stockage::nbTaches(), Stockage::nbNotes(), Agenda::configure() ? "true" : "false");
  // Seulement l'hôte de l'adresse iCal : le reste est secret.
  char hote[64] = "";
  const char* p = strstr(Param.icalUrl, "://");
  if (p) {
    p += 3;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n < sizeof(hote) - 1) n++;
    memcpy(hote, p, n);
    hote[n] = 0;
  }
  o.print(",\"hote\":");
  ecrireTexte(o, hote);
  char quand[32] = "";
  if (Agenda::derniereSynchro()) texteQuand(quand, sizeof(quand), Agenda::derniereSynchro());
  o.printf(",\"evenements\":%d,\"synchro\":", Agenda::nbEvenements());
  ecrireTexte(o, quand);
  o.print(",\"erreur\":");
  ecrireTexte(o, Agenda::derniereErreur());
  o.printf(",\"enCours\":%s}}", Reseau::agendaEnCours() ? "true" : "false");
  o.vider();
}

void routeTaches() {
  if (!autoriser()) return;
  SortieWeb& o = commencerJson();
  o.print("{\"reps\":[");
  for (int i = REP_AUCUNE; i <= REP_JOURS_OUVRES; i++) {
    char t[40] = "";
    Stockage::texteRepetition((uint8_t)i, t, sizeof(t));
    if (i != REP_AUCUNE) o.write(',');
    ecrireTexte(o, t);
  }
  o.print("],\"taches\":[");
  const int n = Stockage::listeTaches(TACHES_TOUTES, s_indices, TACHES_MAX);   // à faire d'abord
  for (int i = 0; i < n; i++) {
    const Tache* t = Stockage::tache(s_indices[i]);
    if (!t) continue;
    char iso[12] = "", h[8] = "";
    if (t->echeance) Horloge::texteISO(iso, sizeof(iso), t->echeance);
    if (t->heure >= 0) snprintf(h, sizeof(h), "%02d:%02d", t->heure / 60, t->heure % 60);
    o.print(i ? ",{\"id\":" : "{\"id\":");
    ecrireTexte(o, t->id);
    o.print(",\"titre\":");
    ecrireTexte(o, t->titre);
    o.printf(",\"faite\":%s,\"echeance\":", t->faite ? "true" : "false");
    ecrireTexte(o, iso);
    o.print(",\"heure\":");
    ecrireTexte(o, h);
    o.printf(",\"rep\":%u}", (unsigned)t->repetition);
  }
  o.print("]}");
  o.vider();
}

void routeNotes() {
  if (!autoriser()) return;
  SortieWeb& o = commencerJson();
  o.print("{\"notes\":[");
  for (int i = 0; i < Stockage::nbNotes(); i++) {   // la plus récente d'abord
    const Note* n = Stockage::note(i);
    if (!n) continue;
    char quand[32];
    texteQuand(quand, sizeof(quand), n->maj);
    o.print(i ? ",{\"id\":" : "{\"id\":");
    ecrireTexte(o, n->id);
    o.print(",\"texte\":");
    ecrireTexte(o, n->texte ? n->texte : "");
    o.print(",\"quand\":");
    ecrireTexte(o, quand);
    o.write('}');
  }
  o.print("]}");
  o.vider();
}

void routeTache() {
  if (!autoriser()) return;
  const String action = s_serveur.arg("action");
  const String id = s_serveur.arg("id");
  String titre = s_serveur.arg("titre");
  titre.trim();
  bool ok = false;
  if (action == "ajouter") {
    const int d = lireDate(s_serveur.arg("echeance"));
    if (!titre.length() || d < 0) { repondreErreur(400, d < 0 ? "date invalide" : "titre vide"); return; }
    ok = Stockage::ajouterTache(titre.c_str(), d) != nullptr;
  } else if (action == "renommer") {
    ok = titre.length() && Stockage::renommerTache(id.c_str(), titre.c_str());
  } else if (action == "cocher") {
    ok = Stockage::cocherTache(id.c_str(), s_serveur.arg("faite") == "1");
  } else if (action == "echeance") {
    const int d = lireDate(s_serveur.arg("echeance"));
    if (d < 0) { repondreErreur(400, "date invalide"); return; }
    ok = Stockage::echeanceTache(id.c_str(), d);
  } else if (action == "heure") {
    const int m = lireHeure(s_serveur.arg("heure"));
    if (m < -1) { repondreErreur(400, "heure invalide"); return; }
    ok = Stockage::heureTache(id.c_str(), m);
  } else if (action == "repetition") {
    const int r = s_serveur.arg("rep").toInt();
    if (r < REP_AUCUNE || r > REP_JOURS_OUVRES) { repondreErreur(400, "répétition inconnue"); return; }
    ok = Stockage::repetitionTache(id.c_str(), (uint8_t)r);
  } else if (action == "supprimer") {
    ok = Stockage::supprimerTache(id.c_str());
  } else {
    repondreErreur(400, "action inconnue");
    return;
  }
  if (!ok) { repondreErreur(400, "refusé (tâche introuvable, liste pleine ou titre vide)"); return; }
  Systeme::redessiner();
  repondreOk();
}

void routeNote() {
  if (!autoriser()) return;
  const String action = s_serveur.arg("action");
  const String id = s_serveur.arg("id");
  String t = s_serveur.arg("texte");
  t.replace("\r", "");
  bool ok = false;
  if (action == "ajouter") {
    String essai = t;
    essai.trim();
    ok = essai.length() && Stockage::ajouterNote(t.c_str()) != nullptr;
  } else if (action == "modifier") {
    ok = Stockage::modifierNote(id.c_str(), t.c_str());
  } else if (action == "supprimer") {
    ok = Stockage::supprimerNote(id.c_str());
  } else {
    repondreErreur(400, "action inconnue");
    return;
  }
  if (!ok) { repondreErreur(400, "refusé (note introuvable, vide ou mémoire pleine)"); return; }
  Systeme::redessiner();
  repondreOk();
}

void routeSauvegarde() {
  if (!autoriser()) return;
  s_serveur.sendHeader("Content-Disposition", "attachment; filename=\"pamk-phone-sauvegarde.json\"");
  SortieWeb& o = commencerJson();
  Stockage::exporter(o);
  o.vider();
}

void routeAgenda() {
  if (!autoriser()) return;
  const String action = s_serveur.arg("action");
  if (action == "enregistrer") {
    String u = s_serveur.arg("url");
    u.trim();
    if (u.substring(0, 9).equalsIgnoreCase("webcal://")) u = String("https://") + u.substring(9);
    if (!u.substring(0, 8).equalsIgnoreCase("https://") || u.length() < 12) {
      repondreErreur(400, "l'adresse doit commencer par https:// (ou webcal://)");
      return;
    }
    if (u.length() >= sizeof(Param.icalUrl)) {
      repondreErreur(400, "adresse trop longue");
      return;
    }
    if (strcmp(Param.icalUrl, u.c_str()) != 0) {   // nouvel agenda : l'ancien disparaît
      Agenda::effacer();
      strncpy(Param.icalUrl, u.c_str(), sizeof(Param.icalUrl) - 1);
      Param.icalUrl[sizeof(Param.icalUrl) - 1] = 0;
      sauverParametres();
      JOURNAL("[acces] nouvelle adresse d'agenda enregistrée");   // jamais l'adresse elle-même
    }
    Reseau::mettreAJourAgenda();   // téléchargée au prochain tour (après cette réponse)
  } else if (action == "synchroniser") {
    if (!Agenda::configure()) { repondreErreur(400, "pas d'agenda enregistré"); return; }
    Reseau::mettreAJourAgenda();
  } else if (action == "retirer") {
    Param.icalUrl[0] = 0;
    sauverParametres();
    Agenda::effacer();
  } else {
    repondreErreur(400, "action inconnue");
    return;
  }
  Systeme::redessiner();
  repondreOk();
}

void enregistrerRoutes() {
  static const char* ENTETES[] = {"X-Code", "Content-Type"};
  s_serveur.collectHeaders(ENTETES, 2);
  s_serveur.enableDelay(false);   // la boucle principale fait déjà ses pauses
  s_serveur.on("/", HTTP_GET, routePage);
  s_serveur.on("/favicon.ico", HTTP_GET, []() { s_serveur.send(204); });
  s_serveur.on("/api/etat", HTTP_GET, routeEtat);
  s_serveur.on("/api/taches", HTTP_GET, routeTaches);
  s_serveur.on("/api/notes", HTTP_GET, routeNotes);
  s_serveur.on("/api/sauvegarde", HTTP_GET, routeSauvegarde);
  s_serveur.on("/api/tache", HTTP_POST, routeTache);
  s_serveur.on("/api/note", HTTP_POST, routeNote);
  s_serveur.on("/api/agenda", HTTP_POST, routeAgenda);
  s_serveur.on("/api/firmware", HTTP_POST, routeFinMaj, recevoirMaj);
  s_serveur.onNotFound([]() { s_serveur.send(404, "text/plain", "introuvable"); });
}

// --- Serveurs -------------------------------------------------------------------
void demarrerOTA() {
  s_ota = new (s_otaMemoire) ArduinoOTAClass();
  s_ota->setHostname(NOM_RESEAU);
  s_ota->setPassword(s_code);
  s_ota->setRebootOnSuccess(false);   // on redémarre nous-mêmes, après l'avoir affiché
  s_ota->onStart([]() { debutMaj(); });
  s_ota->onProgress([](unsigned int fait, unsigned int total) { progresser(fait, total); });
  s_ota->onEnd([]() { finMajReussie(); });
  s_ota->onError([](ota_error_t e) {
    if (e == OTA_AUTH_ERROR) {   // mauvais mot de passe dans l'Arduino IDE
      compterEchec();
      JOURNAL("[acces] Arduino IDE : mot de passe refusé");
      return;
    }
    static const char* const RAISONS[] = {"mot de passe", "démarrage", "connexion", "réception", "vérification"};
    echecMaj((int)e >= 0 && (int)e < 5 ? RAISONS[(int)e] : "erreur");
  });
  s_ota->begin();                          // lance aussi mDNS (pamk-phone.local)
  MDNS.addService("http", "tcp", 80);
}

void arreterOTA() {
  if (!s_ota) return;
  s_ota->end();                            // arrête aussi mDNS
  s_ota->~ArduinoOTAClass();
  s_ota = nullptr;
}

void demarrerServeurs() {
  if (!s_code[0]) {   // un code par session, tiré quand la radio tourne (vrai hasard)
    const uint32_t n = esp_random() % 10000UL;
    s_code[0] = (char)('0' + n / 1000);
    s_code[1] = (char)('0' + n / 100 % 10);
    s_code[2] = (char)('0' + n / 10 % 10);
    s_code[3] = (char)('0' + n % 10);
    s_code[4] = 0;
  }
  snprintf(s_adresse, sizeof(s_adresse), "http://%s", WiFi.localIP().toString().c_str());
  if (!s_routes) {
    enregistrerRoutes();
    s_routes = true;
  }
  s_serveur.begin();
  demarrerOTA();
  s_pret = true;
  s_tActivite = millis();
  JOURNAL("[acces] prêt : %s (ou http://%s.local), Arduino IDE : port réseau « %s »", s_adresse, NOM_RESEAU, NOM_RESEAU);
}

void arreterServeurs() {
  if (!s_pret) return;
  s_serveur.stop();
  arreterOTA();
  s_pret = false;
}

// Texte d'état quand rien de particulier n'est en cours
void majTexte() {
  if (s_redemarrerA || s_pct >= 0) return;
  if (s_messageJusqua) {
    if ((int32_t)(millis() - s_messageJusqua) < 0) return;
    s_messageJusqua = 0;
  }
  if (!s_pret) {
    if (Reseau::occupe()) texte(Reseau::texteEtat());   // « Recherche des réseaux… », « Connexion à … »
    else if (Reseau::etat() == RESEAU_ECHEC) snprintf(s_etat, sizeof(s_etat), "%s (nouvel essai)", Reseau::texteEtat());
    else texte("connexion au Wi-Fi…");
    return;
  }
  texte(bloque() ? "trop de codes faux : pause d'une minute" : "prêt");
}

}  // namespace

namespace Acces {

bool demarrer() {
  if (s_actif) return true;
  if (Reseau::nbConnus() == 0 && Reseau::etat() != RESEAU_CONNECTE) {
    texte("aucun réseau Wi-Fi enregistré");
    return false;
  }
  s_actif = true;
  s_pret = false;
  s_code[0] = 0;
  s_adresse[0] = 0;
  s_echecs = 0;
  s_bloqueJusqua = 0;
  s_pct = -1;
  s_redemarrerA = 0;
  s_messageJusqua = 0;
  s_tActivite = s_tEssai = millis();
  texte("connexion au Wi-Fi…");
  Reseau::garderConnecte();
  JOURNAL("[acces] démarrage");
  return true;
}

void arreter() {
  if (!s_actif) return;
  arreterServeurs();
  s_actif = false;
  s_pct = -1;
  s_redemarrerA = 0;
  s_code[0] = 0;
  s_adresse[0] = 0;
  Reseau::liberer();
  texte("arrêté");
  JOURNAL("[acces] arrêté");
}

void boucle() {
  if (!s_actif) return;
  if (s_redemarrerA && (int32_t)(millis() - s_redemarrerA) >= 0) Systeme::redemarrer();   // ne revient pas
  // Filet de sécurité : écran quitté sans passer par retour() (retour direct à l'accueil…)
  App* a = Systeme::courante();
  if (a && a->id() != APP_ACCES && a->id() != APP_ALERTE && a->id() != APP_DIALOGUE) {
    arreter();
    return;
  }
  const bool connecte = Reseau::etat() == RESEAU_CONNECTE && WiFi.status() == WL_CONNECTED;
  if (s_pret && !connecte) {
    arreterServeurs();
    s_tEssai = millis();
    JOURNAL("[acces] Wi-Fi perdu");
  }
  if (!s_pret) {
    if (connecte) {
      demarrerServeurs();
    } else if (!Reseau::occupe() && millis() - s_tEssai > REESSAI_WIFI_MS) {
      s_tEssai = millis();
      Reseau::garderConnecte();
    }
  }
  if (s_pret) {
    s_serveur.handleClient();
    if (s_ota && !bloque()) s_ota->handle();
  }
  // Oublié ouvert : ni requête (la page en fait une toutes les 30 s) ni toucher depuis 10 min
  if (s_actif && !s_redemarrerA && millis() - s_tActivite > INACTIF_MAX_MS && Energie::inactifDepuisMs() > INACTIF_MAX_MS) {
    arreter();
    texte("arrêté après 10 min sans utilisation");
    return;
  }
  majTexte();
}

bool actif() { return s_actif; }
bool pret() { return s_pret; }
const char* adresse() { return s_adresse; }
const char* code() { return s_code; }
const char* etat() { return s_etat; }
void definirRappelAffichage(void (*rappel)()) { s_rappel = rappel; }
int progression() { return s_pct; }

}  // namespace Acces
