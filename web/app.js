// SPDX-License-Identifier: GPL-3.0-or-later
"use strict";

const POLL_MS = 1000;
// Une requête de statut sans réponse dans ce délai compte comme un échec (boîte éteinte, hors de portée).
const STATUS_TIMEOUT_MS = 2500;
// Échecs consécutifs avant de considérer la connexion perdue: un raté isolé du Wi-Fi ne ferme pas la gestion.
const OFFLINE_AFTER_FAILURES = 2;

const STATE_INFO = {
  booting: { label: "Démarrage", tone: "tone-blue" },
  ready: { label: "Prête", tone: "tone-blue" },
  playing: { label: "En lecture", tone: "tone-green" },
  paused: { label: "En pause", tone: "tone-amber" },
  unknown_card: { label: "Carte inconnue", tone: "tone-red" },
  error: { label: "Erreur", tone: "tone-red" },
};

const ERRORS = {
  busy_playing: "La boîte lit une histoire. Réessaie quand elle est à l'arrêt.",
  sd_unavailable: "Carte SD absente ou illisible.",
  file_exists: "Un fichier porte déjà ce nom.",
  invalid_file_name: "Ce type de fichier n'est pas pris en charge.",
  invalid_target_path: "Dossier de destination invalide.",
  invalid_folder_request: "Nom de dossier invalide.",
  invalid_move_target: "Un dossier ne peut pas aller dans lui-même.",
  confirm_required: "Tape EFFACER pour confirmer.",
  nothing_to_save: "Aucune carte associée à sauvegarder.",
  format_failed: "Formatage impossible: essaie sur un ordinateur, ou avec une autre carte.",
  sd_not_unreadable: "La carte n'a plus besoin d'être formatée.",
  forbidden_origin: "Requête refusée: elle ne vient pas de la page de la boîte.",
  forbidden_host: "Requête refusée: ouvre la page par le nom de la boîte (.local) ou son adresse IP.",
  json_required: "Requête refusée: format inattendu.",
  sealed_invalid: "Échange chiffré refusé par la boîte: recharge la page.",
  stale_challenge: "Échange chiffré périmé: réessaie.",
  backup_failed: "Sauvegarde sur la carte SD impossible.",
  no_backup: "Aucune sauvegarde sur la carte SD.",
  move_failed: "Impossible de déplacer.",
  not_found: "Introuvable sur la carte SD.",
  mkdir_failed: "Impossible de créer le dossier.",
  rename_failed: "Impossible de renommer.",
  trash_failed: "Impossible de mettre à la corbeille.",
  restore_failed: "Impossible de restaurer.",
  purge_failed: "Impossible de vider la corbeille.",
  write_failed: "Écriture sur la carte SD impossible.",
  upload_aborted: "Envoi interrompu.",
  network: "Boîte injoignable.",
  locked: "Réglages verrouillés.",
  wrong_pin: "Code incorrect.",
  too_many_attempts: "Trop d'essais. Réessaie dans une minute.",
  invalid_pin: "Le code doit faire 6 chiffres.",
  invalid_card: "Carte maîtresse manquante.",
  card_in_use: "Cette carte lance déjà une histoire. Utilise une carte dédiée.",
  lock_card: "Une carte maîtresse ne peut pas lancer d'histoire.",
  card_already_master: "Cette carte est déjà une carte maîtresse.",
  too_many_cards: "Nombre maximum de cartes maîtresses atteint.",
  last_card: "Il faut garder au moins une carte maîtresse. Désactive plutôt le verrouillage.",
  lock_disabled: "Le verrouillage n'est pas activé.",
  invalid_wifi_ssid: "Nom du Wi-Fi invalide (32 caractères maximum).",
  invalid_wifi_password: "Le mot de passe du Wi-Fi doit faire de 8 à 63 caractères.",
  invalid_ap_ssid: "Nom du point d'accès invalide (1 à 32 caractères).",
  invalid_ap_password: "Le mot de passe du point d'accès doit faire de 8 à 63 caractères.",
  invalid_hostname: "Nom réseau invalide: lettres, chiffres et tirets (sans tiret au début ni à la fin).",
};

const $ = (id) => document.getElementById(id);

const app = {
  status: null,
  config: { bindings: [] },
  library: null,
  trash: null,
  online: true,
  view: "home",
  filesMode: "files",
  currentDir: LIBRARY_ROOT,
  dismissedUid: "",
  lastStatusKey: "",
  // Dernière IP connue de la boîte sur le Wi-Fi maison (repli quand le nom .local ne répond plus).
  stationIp: "",
  ipFallbackTried: false,
  // Requêtes longues en cours (envoi de fichiers, formatage): la boîte ne répond à rien d'autre.
  busyRequests: 0,
  // Requêtes de statut échouées d'affilée.
  statusFailures: 0,
};

// ---------- Utilitaires ----------

// Ne remplace le contenu que s'il a changé: le polling ne doit pas recréer les boutons sous le doigt.
function setHtml(node, html) {
  if (node.dataset.html === html) return;
  node.dataset.html = html;
  node.innerHTML = html;
}

function icon(name) {
  return `<svg aria-hidden="true"><use href="#i-${name}" /></svg>`;
}

function errorText(code) {
  return ERRORS[code] || `Erreur : ${code || "inconnue"}.`;
}

let toastTimer = 0;
function toast(message, isError = false) {
  const node = $("toast");
  node.textContent = message;
  node.classList.toggle("error", isError);
  node.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove("show"), isError ? 5000 : 3000);
}

async function api(path, options = {}) {
  const { timeout, ...init } = options;
  if (init.json !== undefined) {
    init.method = init.method || "POST";
    init.headers = { "Content-Type": "application/json" };
    init.body = JSON.stringify(init.json);
    delete init.json;
  }
  let response;
  const controller = timeout ? new AbortController() : null;
  const timer = controller ? setTimeout(() => controller.abort(), timeout) : null;
  if (controller) init.signal = controller.signal;
  try {
    response = await fetch(path, init);
  } catch (error) {
    return { ok: false, error: "network", data: {} };
  } finally {
    clearTimeout(timer);
  }
  let data = {};
  try {
    data = await response.json();
  } catch (error) {
    data = {};
  }
  const ok = response.ok && data.ok !== false;
  if (!ok && data.error === "locked") onLockedResponse();
  return { ok, error: ok ? "" : data.error || `http_${response.status}`, data };
}

// ---------- Secrets chiffrés ----------
// Mots de passe et code de secours ne circulent pas en clair sur le réseau local (la page est en HTTP):
// nacl.box avec une paire de clés éphémère vers la clé publique de la boîte (status.boxKey). Le défi de la
// boîte (status.challenge) est inclus pour qu'une requête enregistrée ne puisse pas être rejouée. Protège
// d'une écoute passive, pas de quelqu'un qui s'interpose (il pourrait remplacer la clé, ou la page).

function toBase64(bytes) {
  return btoa(String.fromCharCode(...bytes));
}

function fromBase64(text) {
  return Uint8Array.from(atob(text), (c) => c.charCodeAt(0));
}

function sealSecrets(secrets) {
  const status = app.status;
  if (!status || !status.boxKey || !status.challenge || !window.nacl) return null;
  const ephemeral = nacl.box.keyPair();
  const nonce = nacl.randomBytes(nacl.box.nonceLength);
  const message = new TextEncoder().encode(JSON.stringify({ challenge: status.challenge, ...secrets }));
  const box = nacl.box(message, nonce, fromBase64(status.boxKey), ephemeral.secretKey);
  return { epk: toBase64(ephemeral.publicKey), nonce: toBase64(nonce), box: toBase64(box) };
}

// Comme api(), avec `secrets` chiffrés dans "sealed" (en clair seulement si la boîte ne publie pas de clé:
// ancien firmware). Défi périmé (autre onglet, statut pas encore relu): on relit le statut et on renvoie.
async function apiWithSecrets(path, json, secrets) {
  for (let attempt = 0; ; attempt++) {
    const sealed = sealSecrets(secrets);
    const result = await api(path, { json: sealed ? { ...json, sealed } : { ...json, ...secrets } });
    if (result.error !== "stale_challenge" || attempt > 0) return result;
    await loadStatus();
  }
}

// ---------- Dialogues ----------

// Entrée dans un champ d'une modale: le navigateur "soumettrait" le formulaire avec son premier bouton,
// la croix de fermeture. On déclenche à la place l'action principale visible (Enregistrer, Continuer, OK...),
// ou le bouton indiqué par data-enter ("none": rien, pour les champs de recherche).
document.addEventListener("keydown", (event) => {
  if (event.key !== "Enter" || event.isComposing || event.shiftKey || event.altKey || event.ctrlKey || event.metaKey) return;
  const field = event.target;
  if (!(field instanceof HTMLInputElement) || ["button", "submit", "checkbox", "radio", "range", "file"].includes(field.type)) return;
  const dialog = field.closest("dialog");
  if (!dialog) return;
  event.preventDefault();
  const visible = (button) => button && !button.disabled && button.getClientRects().length > 0;
  const target = field.dataset.enter;
  if (target === "none") return;
  if (target) {
    if (visible($(target))) $(target).click();
    return;
  }
  const primary = Array.from(dialog.querySelectorAll(".btn.primary")).filter(visible).pop();
  if (primary) primary.click();
});

function openDialog(dialog) {
  if (!dialog.open) dialog.showModal();
}

document.querySelectorAll("dialog").forEach((dialog) => {
  // Fermer en touchant le fond.
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) dialog.close("cancel");
  });
});

// Renvoie null si annulé, "alt" pour le bouton secondaire, true (ou le texte saisi) pour OK.
function ask({ title, message = "", value = null, okLabel = "OK", danger = false, validate = null, altLabel = "", extraLabel = "" }) {
  const dialog = $("ask-dialog");
  $("ask-title").textContent = title;
  $("ask-message").textContent = message;
  $("ask-message").hidden = !message;
  const input = $("ask-input");
  input.hidden = value === null;
  input.value = value ?? "";
  $("ask-error").hidden = true;
  const ok = $("ask-ok");
  ok.textContent = okLabel;
  ok.classList.toggle("danger", danger);
  ok.classList.toggle("primary", !danger);
  $("ask-alt").textContent = altLabel;
  $("ask-alt").hidden = !altLabel;
  $("ask-extra").textContent = extraLabel;
  $("ask-extra").hidden = !extraLabel;
  return new Promise((resolve) => {
    const onClick = (event) => {
      if (value !== null && validate) {
        const problem = validate(input.value.trim());
        if (problem) {
          event.preventDefault();
          $("ask-error").textContent = problem;
          $("ask-error").hidden = false;
        }
      }
    };
    ok.addEventListener("click", onClick);
    dialog.addEventListener(
      "close",
      () => {
        ok.removeEventListener("click", onClick);
        if (dialog.returnValue === "alt" && altLabel) return resolve("alt");
        if (dialog.returnValue === "extra" && extraLabel) return resolve("extra");
        if (dialog.returnValue !== "ok") return resolve(null);
        resolve(value === null ? true : input.value.trim());
      },
      { once: true }
    );
    dialog.returnValue = "";
    openDialog(dialog);
    if (value !== null) setTimeout(() => input.select(), 50);
  });
}

// ---------- Données ----------

async function loadStatus() {
  const result = await api("/api/status", { timeout: STATUS_TIMEOUT_MS });
  app.statusFailures = result.error === "network" ? app.statusFailures + 1 : 0;
  app.online = app.statusFailures < OFFLINE_AFTER_FAILURES;
  if (!result.ok) return false;
  app.status = result.data;
  return true;
}

async function loadConfig() {
  if (isLocked()) {
    app.config = { bindings: [] };
    return false;
  }
  const result = await api("/api/config");
  if (result.ok) app.config = result.data;
  return result.ok;
}

// ---------- Verrouillage ----------

// Verrouillée par défaut: tant que la boîte n'a pas répondu (démarrage, connexion perdue), on ne sait pas si
// la gestion est permise, donc on ne montre ni cartes ni réglages.
function isLocked() {
  return !app.online || !app.status || Boolean(app.status.locked);
}

// Verrouillage confirmé par la boîte (et non simple absence de réponse): propose la carte maîtresse ou le code.
function isKnownLocked() {
  return app.online && Boolean(app.status && app.status.locked);
}

const MANAGE_DIALOGS = ["assign-dialog", "card-dialog", "picker-dialog", "settings-dialog", "lock-dialog", "move-dialog", "ask-dialog"];

// La boîte vient d'être verrouillée (bouton, autre téléphone, redémarrage) ou ne répond plus: on masque
// toute la gestion.
function enterLockedView() {
  app.config = { bindings: [] };
  // Force le rechargement des associations au prochain statut reçu.
  app.lastStatusKey = "";
  app.library = null;
  app.trash = null;
  MANAGE_DIALOGS.forEach((id) => {
    if ($(id).open) $(id).close("cancel");
  });
  if (app.view !== "home") showView("home");
}

function onLockedResponse() {
  if (app.status) app.status.locked = true;
  enterLockedView();
  renderHome();
  toast(errorText("locked"), true);
}

function renderLock() {
  const status = app.status;
  const locked = isLocked();
  const enabled = Boolean(status && status.lockEnabled);
  document.querySelectorAll("[data-manage]").forEach((node) => (node.hidden = locked));
  $("open-settings").hidden = locked;
  $("locked-card").hidden = !isKnownLocked();
  $("lock-toggle").hidden = !enabled || locked;
  $("lock-toggle").setAttribute("aria-label", "Verrouiller les réglages");
  $("lock-toggle-icon").setAttribute("href", "#i-unlock");
}

$("lock-toggle").addEventListener("click", async () => {
  const result = await api("/api/lock", { method: "POST" });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  if (app.status) app.status.locked = result.data.locked;
  enterLockedView();
  renderHome();
  toast("Réglages verrouillés.");
});

function openUnlock() {
  $("unlock-pin").value = "";
  $("unlock-error").hidden = true;
  openDialog($("unlock-dialog"));
  setTimeout(() => $("unlock-pin").focus(), 50);
}

async function submitUnlock() {
  const pin = $("unlock-pin").value.trim();
  const error = $("unlock-error");
  if (!/^\d{6}$/.test(pin)) {
    error.textContent = errorText("invalid_pin");
    error.hidden = false;
    return;
  }
  $("unlock-submit").disabled = true;
  const result = await apiWithSecrets("/api/unlock", {}, { pin });
  $("unlock-submit").disabled = false;
  if (!result.ok) {
    const d = result.data;
    error.textContent =
      result.error === "wrong_pin" && d.attemptsLeft
        ? `Code incorrect. ${plural(d.attemptsLeft, "essai restant", "essais restants")}.`
        : result.error === "too_many_attempts" && d.retryInSec
          ? `Trop d'essais. Réessaie dans ${d.retryInSec} s.`
          : errorText(result.error);
    error.hidden = false;
    $("unlock-pin").value = "";
    return;
  }
  $("unlock-dialog").close();
  toast("Réglages déverrouillés.");
  refreshStatus();
}

$("open-unlock").addEventListener("click", openUnlock);
$("unlock-submit").addEventListener("click", submitUnlock);
$("unlock-pin").addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    submitUnlock();
  }
});

// Configuration en 3 modes: "enable" (carte puis nom + code), "add" (carte puis nom), "pin" (code seul).
const LOCK_MODES = {
  enable: { title: "Activer le verrouillage", steps: ["card", "details"], save: "Activer", card: true, pin: true },
  add: { title: "Nouvelle carte maîtresse", steps: ["card", "details"], save: "Ajouter", card: true, pin: false },
  pin: { title: "Changer le code", steps: ["details"], save: "Enregistrer", card: false, pin: true },
};
const lockSetup = { mode: "enable", step: "", uid: "", rejectedUid: "" };

function masterCards() {
  return Array.isArray(app.config.lockCards) ? app.config.lockCards : [];
}

function setLockStep(step) {
  const mode = LOCK_MODES[lockSetup.mode];
  const index = mode.steps.indexOf(step);
  lockSetup.step = step;
  document.querySelectorAll("#lock-dialog [data-lock-panel]").forEach((panel) => (panel.hidden = panel.dataset.lockPanel !== step));
  document.querySelectorAll("#lock-steps span").forEach((bar, i) => {
    bar.hidden = i >= mode.steps.length;
    bar.classList.toggle("on", i <= index);
  });
  $("lock-steps").hidden = mode.steps.length < 2;
  $("lock-step-label").textContent = mode.steps.length > 1 ? `${index + 1}/${mode.steps.length}` : "";
  $("lock-foot").hidden = step !== "details";
  $("lock-prev").hidden = !mode.card;
  $("lock-save").textContent = mode.save;
  $("lock-error").hidden = true;
  document.querySelector('[data-lock-field="card"]').hidden = !mode.card;
  document.querySelector('[data-lock-field="pin"]').hidden = !mode.pin;
  $("lock-pin-label").textContent = lockSetup.mode === "pin" ? "Nouveau code (6 chiffres)" : "Code de secours (6 chiffres)";
  if (step === "details") {
    $("lock-card-uid").textContent = lockSetup.uid;
    $("lock-pin").value = "";
    $("lock-pin-confirm").value = "";
    setTimeout(() => (mode.card ? $("lock-card-label") : $("lock-pin")).focus(), 50);
  }
}

function acceptLockUid(uid) {
  const clean = uid.replace(/[^0-9a-f]/gi, "").toUpperCase();
  if (!clean) return;
  const bound = findBinding(clean);
  const master = masterCards().find((card) => card.uid === clean);
  if (bound || master) {
    if (lockSetup.rejectedUid === clean) return;
    lockSetup.rejectedUid = clean;
    toast(
      bound
        ? `Cette carte lance « ${bound.label || prettyName(bound.mediaPath)} ». Utilise une carte dédiée.`
        : errorText("card_already_master"),
      true
    );
    return;
  }
  lockSetup.uid = clean;
  setLockStep("details");
}

function openLockSetup(mode) {
  lockSetup.mode = mode;
  lockSetup.uid = "";
  lockSetup.rejectedUid = "";
  $("lock-title").textContent = LOCK_MODES[mode].title;
  $("lock-manual-uid").value = "";
  $("lock-card-label").value = "";
  setLockStep(LOCK_MODES[mode].steps[0]);
  openDialog($("lock-dialog"));
}

$("lock-manual-ok").addEventListener("click", () => acceptLockUid($("lock-manual-uid").value));
$("lock-prev").addEventListener("click", () => setLockStep("card"));
$("lock-dialog").addEventListener("close", () => (lockSetup.step = ""));

$("lock-save").addEventListener("click", async () => {
  const mode = LOCK_MODES[lockSetup.mode];
  const error = $("lock-error");
  const fail = (message) => {
    error.textContent = message;
    error.hidden = false;
  };
  const pin = $("lock-pin").value.trim();
  if (mode.pin) {
    if (!/^\d{6}$/.test(pin)) return fail(errorText("invalid_pin"));
    if (pin !== $("lock-pin-confirm").value.trim()) return fail("Les deux codes sont différents.");
  }
  const card = { cardUid: lockSetup.uid, cardLabel: $("lock-card-label").value.trim() };
  const request =
    lockSetup.mode === "enable"
      ? apiWithSecrets("/api/lock/setup", { enabled: true, ...card }, { pin })
      : lockSetup.mode === "add"
        ? api("/api/lock/cards", { json: { action: "add", ...card } })
        : apiWithSecrets("/api/lock/pin", {}, { pin });
  $("lock-save").disabled = true;
  const result = await request;
  $("lock-save").disabled = false;
  if (!result.ok) return fail(errorText(result.error));
  $("lock-dialog").close();
  await loadConfig();
  renderSettingsLock();
  if (app.view === "setup") renderSetup();
  toast(
    lockSetup.mode === "enable"
      ? "Verrouillage activé. Appuie sur le cadenas pour verrouiller."
      : lockSetup.mode === "add"
        ? "Carte maîtresse ajoutée."
        : "Code modifié."
  );
  refreshStatus();
});

function renderSettingsLock() {
  const enabled = Boolean(app.config.lockEnabled);
  const cards = masterCards();
  $("lock-summary").textContent = enabled
    ? "Verrouillée, la page n'affiche que la lecture en cours et le volume. Chaque carte ci-dessous, ou le code de secours, déverrouille."
    : "Protège le Wi-Fi, les limites de volume, les cartes et les fichiers. Une ou plusieurs cartes maîtresses (une par parent) et un code de secours les déverrouillent.";
  $("lock-enable").hidden = enabled;
  $("lock-add-card").hidden = !enabled;
  $("lock-change-pin").hidden = !enabled;
  $("lock-disable").hidden = !enabled;
  $("lock-cards").hidden = !enabled;
  setHtml(
    $("lock-cards"),
    cards
      .map(
        (card) => `
          <div class="row">
            ${icon("nfc")}
            <div class="row-body">
              <div class="row-title">${esc(card.label || "Carte maîtresse")}</div>
              <div class="small mono">${esc(card.uid)}</div>
            </div>
            ${cards.length > 1 ? `<button class="btn" type="button" data-remove-master="${esc(card.uid)}">Retirer</button>` : ""}
          </div>`
      )
      .join("")
  );
}

$("lock-enable").addEventListener("click", () => openLockSetup("enable"));
$("lock-add-card").addEventListener("click", () => openLockSetup("add"));
$("lock-change-pin").addEventListener("click", () => openLockSetup("pin"));
$("lock-cards").addEventListener("click", async (event) => {
  const button = event.target.closest("[data-remove-master]");
  if (!button) return;
  const uid = button.dataset.removeMaster;
  const card = masterCards().find((item) => item.uid === uid);
  const confirmed = await ask({
    title: "Retirer cette carte maîtresse ?",
    message: `« ${(card && card.label) || uid} » ne déverrouillera plus les réglages.`,
    okLabel: "Retirer",
    danger: true,
  });
  if (!confirmed) return;
  const result = await api("/api/lock/cards", { json: { action: "remove", cardUid: uid } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  await loadConfig();
  renderSettingsLock();
  toast("Carte maîtresse retirée.");
});
$("lock-disable").addEventListener("click", async () => {
  const confirmed = await ask({
    title: "Désactiver le verrouillage ?",
    message: "Tout le monde pourra modifier les réglages, les cartes et les fichiers depuis la page.",
    okLabel: "Désactiver",
    danger: true,
  });
  if (!confirmed) return;
  const result = await api("/api/lock/setup", { json: { enabled: false } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  await loadConfig();
  renderSettingsLock();
  toast("Verrouillage désactivé.");
  refreshStatus();
});

async function loadLibrary() {
  const result = await api("/api/library");
  if (result.ok) app.library = result.data;
  return result;
}

async function loadTrash() {
  const result = await api("/api/trash");
  if (result.ok) app.trash = result.data;
  return result;
}

function isPlaying() {
  return Boolean(app.status && app.status.audioPlaying);
}

function bindings() {
  return Array.isArray(app.config.bindings) ? app.config.bindings : [];
}

function findBinding(uid) {
  return bindings().find((binding) => binding.uid === uid);
}

// Recharge la liste juste avant d'écrire pour ne pas écraser un marque-page enregistré entre-temps par la boîte.
async function saveBindings(mutate) {
  await loadConfig();
  const next = bindings().map((binding) => ({ ...binding }));
  mutate(next);
  const result = await api("/api/bindings", { json: next });
  if (result.ok) {
    app.config = result.data;
    renderHome();
  }
  return result;
}

function findNode(entries, path) {
  for (const entry of entries || []) {
    if (entry.path === path) return entry;
    if (entry.type === "directory" && path.startsWith(entry.path + "/")) {
      const found = findNode(entry.children, path);
      if (found) return found;
    }
  }
  return null;
}

function listDir(path) {
  if (!app.library) return [];
  if (path === LIBRARY_ROOT) return app.library.entries || [];
  const node = findNode(app.library.entries, path);
  return node && node.children ? node.children : [];
}

function allEntries() {
  const out = [];
  const walk = (entries) => {
    for (const entry of entries || []) {
      out.push(entry);
      if (entry.type === "directory") walk(entry.children);
    }
  };
  walk(app.library && app.library.entries);
  return out;
}

// ---------- Version de la page ----------

// Version de cette page, posée à la construction de l'image; reste "__UI_VERSION__" si la page est ouverte
// depuis les sources de web/ (faux serveur, make mock): pas de comparaison dans ce cas.
const UI_VERSION = document.querySelector('meta[name="ui-version"]').content;

function uiOutdated() {
  const served = app.status && app.status.uiVersion;
  return /^[0-9a-f]+$/.test(UI_VERSION) && Boolean(served) && served !== UI_VERSION;
}

$("update-reload").addEventListener("click", () => location.reload());

// ---------- Rendu accueil ----------

function renderHome() {
  const status = app.status;
  const pill = $("state-pill");
  $("offline-banner").hidden = app.online;
  // Bandeau carte SD: affiché même verrouillé (simple information); le formatage reste réservé au mode déverrouillé.
  const sdProblem = app.online && status && status.sdReady === false;
  $("sd-banner").hidden = !sdProblem;
  if (sdProblem) {
    const unreadable = sdUnreadable();
    $("sd-banner-title").textContent = unreadable ? "Carte SD illisible" : "Pas de carte SD";
    $("sd-banner-absent").hidden = unreadable;
    $("sd-banner-unreadable").hidden = !unreadable;
    $("sd-banner-locked").hidden = !(unreadable && isLocked());
    $("sd-banner-actions").hidden = !(unreadable && !isLocked());
  }
  $("update-banner").hidden = !app.online || !uiOutdated();

  if (!status) {
    pill.className = "pill tone-grey";
    pill.textContent = app.online ? "Connexion…" : "Hors ligne";
  } else {
    const info = STATE_INFO[status.state] || { label: status.state, tone: "tone-grey" };
    pill.className = `pill ${app.online ? info.tone : "tone-grey"}`;
    // En erreur, la pastille dit la cause plutôt qu'un simple « Erreur ».
    let label = info.label;
    if (status.state === "error" && status.sdReady === false) label = sdUnreadable() ? "Carte SD illisible" : "Carte SD absente";
    else if (status.state === "error" && status.nfcReady === false) label = "Lecteur NFC absent";
    pill.textContent = app.online ? label : "Hors ligne";
  }

  // Carte inconnue: posée maintenant, ou dernière posée si elle a été retirée entre-temps.
  const unknownUid = status ? (status.state === "unknown_card" ? status.activeCardUid : status.lastUnknownCardUid) : "";
  // Sans carte SD lisible, impossible de choisir un dossier ou un fichier: pas d'association proposée.
  const sdMissing = Boolean(status && status.sdReady === false);
  const showUnknown = !isLocked() && !sdMissing && Boolean(unknownUid) && !findBinding(unknownUid) && unknownUid !== app.dismissedUid;
  $("unknown-banner").hidden = !showUnknown;
  $("unknown-uid").textContent = unknownUid || "";

  renderLock();
  // Cartes et fichiers: inutiles sans carte SD lisible (le bandeau du haut l'explique).
  if (sdMissing) $("cards-section").hidden = true;
  renderNowPlaying();
  renderVolume();
  renderTiles();

  // Carte SD absente ou illisible: rien à gérer; le bandeau du haut le dit et propose le formatage.
  $("open-files").hidden = Boolean(status && status.sdReady === false);
  if (status && status.sdTotalBytes) {
    $("sd-summary").textContent = `${fmtBytes(status.sdFreeBytes)} libres sur ${fmtBytes(status.sdTotalBytes)}`;
  } else {
    $("sd-summary").textContent = "";
  }
}

function renderNowPlaying() {
  const status = app.status;
  const active = status && status.activeCardUid && (status.state === "playing" || status.state === "paused");
  $("now-card").hidden = !active;
  if (!active) return;

  const binding = findBinding(status.activeCardUid);
  const playing = status.state === "playing";
  $("now-cover").className = `cover ${playing ? "tone-green" : "tone-amber"}`;
  $("now-cover").innerHTML = icon(playing ? "play" : "pause");
  $("now-state").textContent = playing ? "En lecture" : "En pause";
  $("now-title").textContent =
    status.activeCardLabel || (binding && binding.label) || prettyName(binding ? binding.mediaPath : status.activeMediaPath) || "Histoire";
  const track = status.activeTrackCount > 1 ? `Piste ${status.activeTrackIndex + 1}/${status.activeTrackCount} · ` : "";
  $("now-sub").textContent = `${track}${prettyName(status.activeMediaPath)}`;
  $("now-position").textContent = fmtClock(status.activePositionMs);
}

// ---------- Volume ----------

const VOLUME_SEND_INTERVAL_MS = 250;
// Pendant un réglage, le polling ne doit pas faire sauter le curseur vers l'ancienne valeur.
const VOLUME_HOLD_MS = 1500;
const volume = { sending: false, queued: null, lastSent: 0, holdUntil: 0 };

function renderVolume() {
  const status = app.status;
  $("volume-card").hidden = !status || !app.online;
  if (!status) return;
  const range = $("volume-range");
  range.min = status.volumeMin ?? 0;
  range.max = status.volumeMax ?? 100;
  if (Date.now() < volume.holdUntil) return;
  range.value = status.currentVolume ?? range.min;
  $("volume-value").textContent = `${range.value} %`;
}

async function flushVolume() {
  if (volume.sending || volume.queued === null) return;
  const value = volume.queued;
  volume.queued = null;
  volume.sending = true;
  volume.lastSent = Date.now();
  const result = await api("/api/volume", { json: { volume: value } });
  volume.sending = false;
  volume.holdUntil = Date.now() + VOLUME_HOLD_MS;
  if (!result.ok) {
    toast(errorText(result.error), true);
  } else if (app.status) {
    app.status.currentVolume = result.data.currentVolume;
  }
  if (volume.queued !== null) {
    setTimeout(flushVolume, Math.max(0, VOLUME_SEND_INTERVAL_MS - (Date.now() - volume.lastSent)));
  }
}

// Envoie au plus une requête tous les 250 ms pendant le glissement, puis la valeur finale.
function setVolume(value) {
  const range = $("volume-range");
  const clamped = Math.min(Number(range.max), Math.max(Number(range.min), Math.round(value)));
  range.value = clamped;
  $("volume-value").textContent = `${clamped} %`;
  volume.queued = clamped;
  volume.holdUntil = Date.now() + VOLUME_HOLD_MS;
  const wait = VOLUME_SEND_INTERVAL_MS - (Date.now() - volume.lastSent);
  if (wait <= 0) {
    flushVolume();
  } else {
    setTimeout(flushVolume, wait);
  }
}

$("volume-range").addEventListener("input", () => setVolume(Number($("volume-range").value)));
$("volume-card").addEventListener("click", (event) => {
  const step = event.target.closest("[data-volume-step]");
  if (!step) return;
  setVolume(Number($("volume-range").value) + Number(step.dataset.volumeStep));
});

function bookmarkText(binding) {
  if (!binding.resumeMs && !binding.resumeTrackIndex) return "Commence au début";
  const track = binding.resumeTrackIndex ? `piste ${binding.resumeTrackIndex + 1}, ` : "";
  return `Reprend ${track}à ${fmtClock(binding.resumeMs)}`;
}

function renderTiles() {
  const list = bindings();
  const activeUid = app.status && app.status.activeCardUid;
  $("cards-count").textContent = list.length ? plural(list.length, "carte", "cartes") : "";

  const tiles = list.map((binding, index) => {
    const playing = binding.uid === activeUid && app.status.state === "playing";
    const title = binding.label || prettyName(binding.mediaPath) || binding.uid;
    return `
      <button class="tile${playing ? " playing" : ""}" type="button" data-card="${index}">
        <span class="tile-icon ${playing ? "tone-green" : "tone-blue"}">${icon(playing ? "play" : "book")}</span>
        <span class="tile-title">${esc(title)}</span>
        <span class="small">${playing ? "En lecture" : esc(bookmarkText(binding))}</span>
      </button>`;
  });
  tiles.push(`<button class="tile add" type="button" data-add-card>${icon("plus")}Ajouter une carte</button>`);
  setHtml($("tiles"), tiles.join(""));
}

$("tiles").addEventListener("click", (event) => {
  const tile = event.target.closest("[data-card], [data-add-card]");
  if (!tile) return;
  if (tile.hasAttribute("data-add-card")) {
    startAssign("");
  } else {
    openCard(Number(tile.dataset.card));
  }
});

$("assign-unknown").addEventListener("click", () => startAssign($("unknown-uid").textContent));
$("dismiss-unknown").addEventListener("click", () => {
  app.dismissedUid = $("unknown-uid").textContent;
  renderHome();
});

// ---------- Sélecteur de contenu (dossier entier ou fichier seul) ----------

// picker = { dir, query, selected }. Un appui sur un dossier l'ouvre; "Tout le dossier" ou un fichier le sélectionne.
function newPicker(selected) {
  const dir = !selected ? LIBRARY_ROOT : isAudioPath(selected) ? parentDir(selected) : selected;
  return { dir: findNode(app.library && app.library.entries, dir) || dir === LIBRARY_ROOT ? dir : LIBRARY_ROOT, query: "", selected };
}

function pickerRow(entry, picker, label) {
  const isDir = entry.type === "directory";
  const selected = entry.path === picker.selected;
  const details = isDir
    ? entry.trackCount
      ? `${plural(entry.trackCount, "piste", "pistes")} · ≈ ${fmtDuration(entry.durationEstimateSec)}`
      : "Vide"
    : `${fmtBytes(entry.totalSize)} · ≈ ${fmtDuration(entry.durationEstimateSec)}`;
  const attr = isDir ? `data-enter="${esc(entry.path)}"` : `data-pick="${esc(entry.path)}"`;
  return `
    <button class="row${selected ? " selected" : ""}" type="button" ${attr}>
      ${icon(isDir ? "folder" : "music")}
      <span class="row-body">
        <span class="row-title" style="display:block">${esc(label)}</span>
        <span class="small">${esc(details)}</span>
      </span>
      ${selected ? icon("check") : isDir ? icon("chevron") : ""}
    </button>`;
}

function renderPicker(container, picker, emptyHint) {
  if (!app.library) {
    setHtml(container, `<div class="empty">Chargement…</div>`);
    return;
  }
  const query = picker.query.trim().toLowerCase();
  if (query) {
    const matches = allEntries().filter((entry) => relPath(entry.path).toLowerCase().includes(query));
    setHtml(
      container,
      matches.length
        ? matches.map((entry) => pickerRow(entry, picker, relPath(entry.path))).join("")
        : `<div class="empty">Rien ne correspond.</div>`
    );
    return;
  }

  const rows = [];
  if (picker.dir !== LIBRARY_ROOT) {
    const node = findNode(app.library.entries, picker.dir);
    const parent = parentDir(picker.dir);
    rows.push(`
      <button class="row" type="button" data-enter="${esc(parent)}">
        ${icon("back")}
        <span class="row-body"><span class="row-title" style="display:block">${esc(parent === LIBRARY_ROOT ? "Tous les fichiers" : baseName(parent))}</span></span>
      </button>`);
    if (node && node.trackCount) {
      const selected = picker.dir === picker.selected;
      rows.push(`
        <button class="row${selected ? " selected" : ""}" type="button" data-pick="${esc(picker.dir)}">
          ${icon("folder")}
          <span class="row-body">
            <span class="row-title" style="display:block">Tout le dossier « ${esc(baseName(picker.dir))} »</span>
            <span class="small">${esc(plural(node.trackCount, "piste", "pistes"))} à la suite · ≈ ${esc(fmtDuration(node.durationEstimateSec))}</span>
          </span>
          ${selected ? icon("check") : ""}
        </button>`);
    }
  }
  const entries = listDir(picker.dir).filter((entry) => entry.type === "directory" || isAudioPath(entry.path));
  entries.forEach((entry) => rows.push(pickerRow(entry, picker, baseName(entry.path))));
  if (!entries.length) {
    rows.push(`<div class="empty">${esc(picker.dir === LIBRARY_ROOT ? emptyHint : "Ce dossier est vide.")}</div>`);
  }
  setHtml(container, rows.join(""));
}

// Gère les appuis dans un sélecteur. Renvoie le chemin choisi, ou "" si l'appui a seulement navigué.
function handlePickerClick(event, picker, searchInput) {
  const enter = event.target.closest("[data-enter]");
  if (enter) {
    picker.dir = enter.dataset.enter;
    picker.query = "";
    searchInput.value = "";
    return "";
  }
  const pick = event.target.closest("[data-pick]");
  return pick ? pick.dataset.pick : "";
}

async function ensureLibraryForPicker(busyNode) {
  busyNode.hidden = true;
  if (isPlaying() && !app.library) {
    busyNode.hidden = false;
    return;
  }
  if (!isPlaying()) {
    const result = await loadLibrary();
    if (!result.ok && result.error === "busy_playing") busyNode.hidden = false;
  }
}

// ---------- Assistant d'association ----------

const assign = { step: "card", uid: "", folder: "", label: "", picker: newPicker("") };
const ASSIGN_EMPTY = "La carte SD est vide. Crée un dossier avec tes fichiers.";

function setAssignStep(step) {
  assign.step = step;
  const order = ["card", "story", "name"];
  const index = order.indexOf(step);
  document.querySelectorAll("#assign-dialog [data-panel]").forEach((panel) => {
    panel.hidden = panel.dataset.panel !== step;
  });
  document.querySelectorAll("#assign-dialog .steps span").forEach((bar, i) => bar.classList.toggle("on", i <= index));
  $("assign-step-label").textContent = `${index + 1}/3`;
  $("assign-prev").hidden = step === "card" || (step === "story" && assign.lockedUid);
  $("assign-next").hidden = step === "card";
  document.querySelector("#assign-dialog .sheet-foot").hidden = step === "card";
  $("assign-next").textContent = step === "name" ? "Associer" : "Continuer";
  $("assign-next").disabled = step === "story" && !assign.folder;

  if (step === "story") {
    assign.picker.selected = assign.folder;
    renderPicker($("assign-folders"), assign.picker, ASSIGN_EMPTY);
    ensureLibraryForPicker($("assign-busy")).then(() => renderPicker($("assign-folders"), assign.picker, ASSIGN_EMPTY));
  }
  if (step === "name") {
    if (!assign.label) assign.label = prettyName(assign.folder);
    $("assign-label").value = assign.label;
    $("assign-summary-uid").textContent = assign.uid;
    $("assign-summary-folder").textContent = relPath(assign.folder);
  }
}

function startAssign(uid) {
  assign.uid = uid;
  assign.lockedUid = Boolean(uid);
  assign.rejectedUid = "";
  assign.folder = "";
  assign.label = "";
  assign.picker = newPicker("");
  $("assign-search").value = "";
  $("assign-manual-uid").value = "";
  $("assign-progress").hidden = true;
  $("assign-title").textContent = "Nouvelle carte";
  setAssignStep(uid ? "story" : "card");
  openDialog($("assign-dialog"));
}

function acceptAssignUid(uid) {
  const clean = uid.replace(/[^0-9a-f]/gi, "").toUpperCase();
  if (!clean) return;
  const existing = findBinding(clean);
  if (existing) {
    // Le polling repropose la même carte à chaque statut: on ne prévient qu'une fois.
    if (assign.rejectedUid === clean) return;
    assign.rejectedUid = clean;
    toast(`Cette carte est déjà associée à « ${existing.label || prettyName(existing.mediaPath)} ».`, true);
    return;
  }
  assign.uid = clean;
  setAssignStep("story");
}

$("assign-manual-ok").addEventListener("click", () => acceptAssignUid($("assign-manual-uid").value));
$("assign-search").addEventListener("input", () => {
  assign.picker.query = $("assign-search").value;
  renderPicker($("assign-folders"), assign.picker, ASSIGN_EMPTY);
});
$("assign-folders").addEventListener("click", (event) => {
  const path = handlePickerClick(event, assign.picker, $("assign-search"));
  if (path) {
    assign.folder = path;
    assign.label = "";
    setAssignStep("story");
    return;
  }
  renderPicker($("assign-folders"), assign.picker, ASSIGN_EMPTY);
});
$("assign-prev").addEventListener("click", () => setAssignStep(assign.step === "name" ? "story" : "card"));
$("assign-next").addEventListener("click", async () => {
  if (assign.step === "story" && assign.folder) {
    setAssignStep("name");
    return;
  }
  if (assign.step !== "name") return;
  const label = $("assign-label").value.trim() || prettyName(assign.folder);
  $("assign-next").disabled = true;
  const result = await saveBindings((list) => {
    list.push({ uid: assign.uid, label, mediaPath: assign.folder, mode: "sd_local", resumeTrackIndex: 0, resumeMs: 0, resumeByte: 0 });
  });
  $("assign-next").disabled = false;
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  $("assign-dialog").close();
  toast(`Carte « ${label} » prête.`);
  refreshStatus();
});

$("assign-new-folder").addEventListener("click", async () => {
  if (isPlaying()) {
    toast(errorText("busy_playing"), true);
    return;
  }
  // Les fichiers d'abord, tant qu'on est dans l'appui de l'utilisateur, puis le nom du dossier.
  const files = await pickFiles();
  if (!files.length) return;
  const name = await ask({
    title: "Nouveau dossier",
    message: `${plural(files.length, "fichier sera rangé", "fichiers seront rangés")} dans ce nouveau dossier.`,
    value: "",
    okLabel: "Créer et envoyer",
    validate: (v) => (v ? "" : "Saisis un nom."),
  });
  if (!name) return;
  const folder = await createFolder(LIBRARY_ROOT, name);
  if (!folder) return;
  const ok = await uploadWithProgress(files, folder, $("assign-progress"), $("assign-progress-label"), $("assign-progress-bar"));
  await loadLibrary();
  if (ok) {
    assign.folder = folder;
    assign.label = name;
    assign.picker.dir = folder;
    setAssignStep("name");
  } else {
    setAssignStep("story");
  }
});

$("assign-dialog").addEventListener("close", () => {
  assign.step = "";
});

// ---------- Fiche carte ----------

const editing = { index: -1, folder: "", resetBookmark: false };

function openCard(index) {
  const binding = bindings()[index];
  if (!binding) return;
  editing.index = index;
  editing.uid = binding.uid;
  editing.folder = binding.mediaPath;
  editing.resetBookmark = false;
  $("card-label").value = binding.label || "";
  $("card-label").placeholder = prettyName(binding.mediaPath);
  $("card-uid").textContent = binding.uid;
  renderCardDialog(binding);
  openDialog($("card-dialog"));
}

function renderCardDialog(binding) {
  $("card-folder").textContent = relPath(editing.folder) || "Aucun";
  $("card-folder-icon").setAttribute("href", isAudioPath(editing.folder) ? "#i-music" : "#i-folder");
  const resettable = !editing.resetBookmark && (binding.resumeMs || binding.resumeTrackIndex);
  $("card-bookmark").textContent = editing.resetBookmark ? "Recommencera au début" : bookmarkText(binding);
  $("card-reset-bookmark").hidden = !resettable;
}

$("card-reset-bookmark").addEventListener("click", () => {
  editing.resetBookmark = true;
  renderCardDialog(findBinding(editing.uid) || {});
});

$("card-change-folder").addEventListener("click", async () => {
  const folder = await pickFolder(editing.folder);
  if (!folder) return;
  if (folder !== editing.folder) {
    editing.folder = folder;
    editing.resetBookmark = true;
  }
  renderCardDialog(findBinding(editing.uid) || {});
});

$("card-save").addEventListener("click", async () => {
  const label = $("card-label").value.trim();
  const result = await saveBindings((list) => {
    const binding = list.find((item) => item.uid === editing.uid);
    if (!binding) return;
    binding.label = label;
    binding.mediaPath = editing.folder;
    if (editing.resetBookmark) {
      binding.resumeTrackIndex = 0;
      binding.resumeMs = 0;
      binding.resumeByte = 0;
    }
  });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  $("card-dialog").close();
  toast("Carte enregistrée.");
});

$("card-delete").addEventListener("click", async () => {
  const binding = findBinding(editing.uid);
  const name = (binding && (binding.label || prettyName(binding.mediaPath))) || editing.uid;
  const confirmed = await ask({
    title: "Oublier cette carte ?",
    message: `« ${name} » ne lancera plus rien. Les fichiers restent sur la carte SD.`,
    okLabel: "Oublier",
    danger: true,
  });
  if (!confirmed) return;
  const result = await saveBindings((list) => {
    const index = list.findIndex((item) => item.uid === editing.uid);
    if (index >= 0) list.splice(index, 1);
  });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  $("card-dialog").close();
  toast("Carte oubliée.");
});

function pickFolder(current) {
  const dialog = $("picker-dialog");
  const picker = newPicker(current);
  $("picker-search").value = "";
  const render = () => renderPicker($("picker-folders"), picker, "La carte SD est vide.");
  render();
  ensureLibraryForPicker($("picker-busy")).then(() => {
    if (picker.dir === LIBRARY_ROOT) Object.assign(picker, newPicker(current));
    render();
  });
  return new Promise((resolve) => {
    let chosen = "";
    const onClick = (event) => {
      const path = handlePickerClick(event, picker, $("picker-search"));
      if (path) {
        chosen = path;
        dialog.close();
        return;
      }
      render();
    };
    $("picker-folders").addEventListener("click", onClick);
    $("picker-search").oninput = () => {
      picker.query = $("picker-search").value;
      render();
    };
    dialog.addEventListener(
      "close",
      () => {
        $("picker-folders").removeEventListener("click", onClick);
        resolve(chosen);
      },
      { once: true }
    );
    openDialog(dialog);
  });
}

// ---------- Envoi de fichiers ----------

// WebView Android (fenêtre de connexion Wi-Fi, navigateurs intégrés aux applis): pas de sélecteur de fichiers
// sauf si l'application l'a prévu, ce que la fenêtre de connexion Wi-Fi ne fait pas.
function isAndroidWebView() {
  return /Android/.test(navigator.userAgent) && /; wv\)/.test(navigator.userAgent);
}

// Les navigateurs intégrés des portails captifs (Android, iOS) ne savent souvent pas choisir de fichiers.
function canUploadFiles() {
  if (isAndroidWebView()) return false;
  try {
    const input = document.createElement("input");
    input.type = "file";
    return (
      input.type === "file" &&
      !input.disabled &&
      typeof FormData === "function" &&
      typeof File === "function" &&
      typeof Blob.prototype.slice === "function" &&
      "upload" in new XMLHttpRequest()
    );
  } catch (error) {
    return false;
  }
}

function portalUrl() {
  const ip = app.status && app.status.wifiIp;
  return ip ? `http://${ip}/` : `${location.origin}/`;
}

function showUploadHelp() {
  document.querySelectorAll(".upload-help").forEach((node) => {
    node.querySelector(".portal-url").textContent = portalUrl();
    node.hidden = false;
  });
}

document.addEventListener("click", async (event) => {
  if (!event.target.closest("[data-copy-url]")) return;
  const url = portalUrl();
  let copied = false;
  try {
    await navigator.clipboard.writeText(url);
    copied = true;
  } catch (error) {
    // Pas d'API presse-papiers en HTTP: copie via une zone de texte temporaire.
    const area = document.createElement("textarea");
    area.value = url;
    document.body.appendChild(area);
    area.select();
    copied = document.execCommand("copy");
    area.remove();
  }
  toast(copied ? "Adresse copiée." : `Copie impossible : note ${url}`, !copied);
});

const AUDIO_ACCEPT = ".mp3,.wav,.flac,.aac,.m4a,.ogg,.opus,audio/*";
const PICKER_OPEN_TIMEOUT_MS = 1500;
// Une annulation plus rapide que ça, sans que la page perde le focus, vient du navigateur et non de l'utilisateur.
const PICKER_INSTANT_CANCEL_MS = 800;
let pendingPicker = null;

// Doit être appelé directement dans le gestionnaire d'appui: sinon Safari refuse d'ouvrir le sélecteur.
// Un champ neuf à chaque fois: un ancien sélecteur fermé sans événement "cancel" ne doit pas déclencher
// un second envoi au choix suivant.
function pickFiles() {
  if (!canUploadFiles()) {
    showUploadHelp();
    return Promise.resolve([]);
  }
  if (pendingPicker) pendingPicker.remove();
  const input = document.createElement("input");
  input.type = "file";
  input.multiple = true;
  input.accept = AUDIO_ACCEPT;
  input.hidden = true;
  document.body.appendChild(input);
  pendingPicker = input;

  // Quand le sélecteur s'ouvre, la page perd le focus ou passe en arrière-plan. Si rien de tout ça
  // n'arrive, le navigateur (fenêtre de connexion Wi-Fi d'Android notamment) a ignoré l'appui.
  let opened = false;
  const markOpened = () => {
    opened = true;
  };
  window.addEventListener("blur", markOpened);
  document.addEventListener("visibilitychange", markOpened);
  const watchdog = setTimeout(() => {
    if (!opened && input.isConnected) {
      showUploadHelp();
      toast("Le sélecteur de fichiers ne s'ouvre pas dans ce navigateur.", true);
    }
  }, PICKER_OPEN_TIMEOUT_MS);

  const startedAt = Date.now();
  return new Promise((resolve) => {
    const done = (files) => {
      clearTimeout(watchdog);
      window.removeEventListener("blur", markOpened);
      document.removeEventListener("visibilitychange", markOpened);
      input.remove();
      if (pendingPicker === input) pendingPicker = null;
      resolve(files);
    };
    input.addEventListener("change", () => done(Array.from(input.files || [])), { once: true });
    input.addEventListener(
      "cancel",
      () => {
        // Chromium (dont la WebView Android) annule aussitôt quand aucun sélecteur n'est disponible.
        if (!opened && Date.now() - startedAt < PICKER_INSTANT_CANCEL_MS) {
          showUploadHelp();
          toast("Le sélecteur de fichiers ne s'ouvre pas dans ce navigateur.", true);
        }
        done([]);
      },
      { once: true }
    );
    input.click();
  });
}

async function uploadFiles(files, targetDir, onProgress) {
  const formData = new FormData();
  formData.append("targetPath", targetDir);
  formData.append("conflictMode", "rename");
  let strippedBytes = 0;
  for (const file of files) {
    let payloadFile = file;
    try {
      payloadFile = await stripId3(file);
    } catch (error) {
      payloadFile = file;
    }
    strippedBytes += file.size - payloadFile.size;
    formData.append("files", payloadFile, file.name);
  }

  app.busyRequests += 1;
  return new Promise((resolve) => {
    const xhr = new XMLHttpRequest();
    xhr.addEventListener("loadend", () => (app.busyRequests -= 1));
    xhr.open("POST", "/api/upload");
    xhr.upload.addEventListener("progress", (event) => {
      if (event.lengthComputable) onProgress(event.loaded / event.total);
    });
    xhr.addEventListener("load", () => {
      let data = {};
      try {
        data = JSON.parse(xhr.responseText || "{}");
      } catch (error) {
        data = {};
      }
      const ok = xhr.status >= 200 && xhr.status < 300 && data.ok !== false;
      resolve({ ok, error: ok ? "" : data.error || `http_${xhr.status}`, data, strippedBytes });
    });
    xhr.addEventListener("error", () => resolve({ ok: false, error: "network", data: {}, strippedBytes }));
    xhr.addEventListener("abort", () => resolve({ ok: false, error: "upload_aborted", data: {}, strippedBytes }));
    xhr.send(formData);
  });
}

async function uploadWithProgress(files, targetDir, wrap, label, bar) {
  wrap.hidden = false;
  bar.style.width = "0%";
  label.textContent = `Envoi de ${plural(files.length, "fichier", "fichiers")}…`;
  const result = await uploadFiles(files, targetDir, (ratio) => {
    bar.style.width = `${Math.round(ratio * 100)}%`;
    label.textContent = `Envoi de ${plural(files.length, "fichier", "fichiers")}… ${Math.round(ratio * 100)} %`;
  });
  wrap.hidden = true;
  if (!result.ok) {
    toast(errorText(result.error), true);
    // Échec réseau pendant l'envoi: typique d'un navigateur de portail captif qui coupe les gros envois.
    if (result.error === "network" || result.error === "upload_aborted" || result.error === "http_0") {
      showUploadHelp();
    }
    return false;
  }
  const saved = result.strippedBytes + (result.data.metadataBytesStripped || 0);
  toast(`${plural(result.data.filesUploaded || files.length, "fichier ajouté", "fichiers ajoutés")}${saved ? ` (${fmtBytes(saved)} de pochettes et tags retirés)` : ""}.`);
  return true;
}

async function createFolder(parentPath, name) {
  const result = await api("/api/folder", { json: { parentPath, folderName: name } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return "";
  }
  return result.data.path;
}

// ---------- Écran fichiers ----------

function showView(view) {
  app.view = view;
  $("view-home").hidden = view !== "home";
  $("view-files").hidden = view !== "files";
  $("view-setup").hidden = view !== "setup";
  window.scrollTo(0, 0);
}

async function openFiles(mode = "files") {
  app.filesMode = mode;
  showView("files");
  renderFiles();
  if (isPlaying()) return;
  if (mode === "trash") {
    await loadTrash();
  } else {
    await loadLibrary();
  }
  renderFiles();
}

function renderFiles() {
  const trashMode = app.filesMode === "trash";
  const busy = isPlaying();
  $("files-title").textContent = trashMode ? "Corbeille" : "Fichiers";
  $("files-busy").hidden = !busy;
  $("files-main").hidden = trashMode;
  $("trash-main").hidden = !trashMode;
  $("open-trash").hidden = trashMode;
  ["files-upload", "files-mkdir", "trash-purge"].forEach((id) => ($(id).disabled = busy));

  const lib = app.library;
  if (lib && lib.sdTotalBytes) {
    const used = Number(lib.sdUsedBytes || 0);
    const total = Number(lib.sdTotalBytes || 1);
    $("sd-meter-label").textContent = `${fmtBytes(used)} utilisés sur ${fmtBytes(total)}`;
    $("sd-meter").style.width = `${Math.min(100, Math.round((used / total) * 100))}%`;
    $("sd-meter-wrap").hidden = false;
  } else {
    $("sd-meter-wrap").hidden = true;
  }

  if (trashMode) {
    renderTrash();
    return;
  }

  // Fil d'Ariane
  const parts = app.currentDir === LIBRARY_ROOT ? [] : app.currentDir.slice(LIBRARY_ROOT.length + 1).split("/");
  const crumbs = [`<button type="button" data-dir="${LIBRARY_ROOT}">Tous</button>`];
  parts.forEach((part, index) => {
    const path = `${LIBRARY_ROOT}/${parts.slice(0, index + 1).join("/")}`;
    crumbs.push(`<span>/</span><button type="button" data-dir="${esc(path)}">${esc(part)}</button>`);
  });
  setHtml($("crumbs"), crumbs.join(""));

  // Carte SD retirée ou illisible pendant que l'écran est ouvert: la bibliothèque n'est plus lisible (503).
  // Le formatage est proposé par le bandeau de l'accueil.
  if (app.status && app.status.sdReady === false) {
    setHtml($("files-list"), `<div class="empty">${sdUnreadable() ? "Carte SD illisible : voir l'accueil pour la formater." : "Carte SD absente."}</div>`);
    return;
  }
  if (!lib) {
    setHtml($("files-list"), `<div class="empty">${busy ? "Disponible quand la boîte est à l'arrêt." : "Chargement…"}</div>`);
    return;
  }

  const query = $("files-search").value.trim().toLowerCase();
  const entries = listDir(app.currentDir).filter((entry) => !query || baseName(entry.path).toLowerCase().includes(query));
  const usedBy = new Map(bindings().map((binding) => [binding.mediaPath, binding.label || binding.uid]));

  if (!entries.length) {
    setHtml($("files-list"), `<div class="empty">${query ? "Rien ne correspond." : "Dossier vide. Ajoute des fichiers audio."}</div>`);
    return;
  }

  setHtml($("files-list"), entries
    .map((entry) => {
      const isDir = entry.type === "directory";
      const card = usedBy.get(entry.path);
      const details = isDir
        ? entry.trackCount
          ? `${plural(entry.trackCount, "piste", "pistes")} · ≈ ${fmtDuration(entry.durationEstimateSec)}`
          : "Vide"
        : `${fmtBytes(entry.totalSize)} · ≈ ${fmtDuration(entry.durationEstimateSec)}`;
      return `
        <div class="row" ${busy ? "" : `draggable="true" data-path="${esc(entry.path)}"`}>
          ${icon(isDir ? "folder" : "music")}
          <button class="row-body" type="button" style="border:0;background:none;padding:0;text-align:left" ${isDir ? `data-dir="${esc(entry.path)}"` : ""}>
            <span class="row-title" style="display:block">${esc(baseName(entry.path))}</span>
            <span class="small">${esc(details)}${card ? ` · carte « ${esc(card)} »` : ""}</span>
          </button>
          <button class="icon-btn" type="button" aria-label="Actions" data-actions="${esc(entry.path)}" data-type="${entry.type}" ${busy ? "disabled" : ""}>${icon("dots")}</button>
        </div>`;
    })
    .join(""));
}

function renderTrash() {
  const trash = app.trash;
  const entries = (trash && trash.entries) || [];
  $("trash-purge").hidden = !entries.length;
  if (!trash) {
    setHtml($("trash-list"), `<div class="empty">${isPlaying() ? "Disponible quand la boîte est à l'arrêt." : "Chargement…"}</div>`);
    return;
  }
  if (!entries.length) {
    setHtml($("trash-list"), `<div class="empty">La corbeille est vide.</div>`);
    return;
  }
  setHtml($("trash-list"), entries
    .map((entry) => {
      const isDir = entry.type === "directory";
      const original = LIBRARY_ROOT + entry.path.slice(TRASH_ROOT.length);
      const parent = original.slice(0, original.lastIndexOf("/"));
      const origin = parent === LIBRARY_ROOT ? "Depuis la racine" : `Depuis ${relPath(parent)}`;
      return `
        <div class="row">
          ${icon(isDir ? "folder" : "music")}
          <div class="row-body">
            <div class="row-title">${esc(baseName(entry.path))}</div>
            <div class="small">${esc(origin)} · ${fmtBytes(entry.totalSize)}</div>
          </div>
          <button class="btn" type="button" data-restore="${esc(entry.path)}" ${isPlaying() ? "disabled" : ""}>Restaurer</button>
        </div>`;
    })
    .join(""));
}

$("open-files").addEventListener("click", () => {
  app.currentDir = LIBRARY_ROOT;
  $("files-search").value = "";
  openFiles("files");
});
$("open-trash").addEventListener("click", () => openFiles("trash"));
$("files-back").addEventListener("click", () => {
  if (app.filesMode === "trash") {
    openFiles("files");
    return;
  }
  if (app.currentDir !== LIBRARY_ROOT) {
    app.currentDir = app.currentDir.slice(0, app.currentDir.lastIndexOf("/"));
    renderFiles();
    return;
  }
  showView("home");
});
$("files-search").addEventListener("input", renderFiles);

document.addEventListener("click", (event) => {
  const dir = event.target.closest("#view-files [data-dir]");
  if (!dir) return;
  app.currentDir = dir.dataset.dir;
  $("files-search").value = "";
  renderFiles();
});

$("files-upload").addEventListener("click", async () => {
  const files = await pickFiles();
  if (!files.length) return;
  await uploadWithProgress(files, app.currentDir, $("files-progress"), $("files-progress-label"), $("files-progress-bar"));
  await loadLibrary();
  renderFiles();
});

// ---------- Glisser-déposer ----------
// Fichiers et dossiers déposés sur l'écran Fichiers: dans le dossier courant, ou dans le dossier survolé.
// Un dossier déposé est recréé sur la boîte (un envoi par dossier), les fichiers non audio (pochettes,
// playlists...) sont ignorés: la boîte refuserait tout l'envoi.

const DEFAULT_DROP_HINT = $("files-drop-hint").textContent;
let dropTargetRow = null;

function hasDraggedFiles(event) {
  return Boolean(event.dataTransfer) && Array.from(event.dataTransfer.types || []).includes("Files");
}

function canDropFiles() {
  return app.filesMode !== "trash" && !$("view-files").hidden;
}

function setDropTarget(row) {
  if (dropTargetRow === row) return;
  if (dropTargetRow) dropTargetRow.classList.remove("drop-target");
  dropTargetRow = row;
  if (row) row.classList.add("drop-target");
  const dir = row ? row.querySelector("[data-dir]").dataset.dir : app.currentDir;
  $("files-drop-hint").textContent = `Dépose pour ajouter dans « ${dir === LIBRARY_ROOT ? "Tous" : baseName(dir)} ».`;
}

function clearDrop() {
  $("files-main").classList.remove("drop-active");
  if (dropTargetRow) dropTargetRow.classList.remove("drop-target");
  dropTargetRow = null;
  $("files-drop-hint").textContent = DEFAULT_DROP_HINT;
}

function readAllEntries(reader) {
  // readEntries() rend les entrées par paquets (100 sous Chrome): on lit jusqu'à un paquet vide.
  return new Promise((resolve, reject) => {
    const all = [];
    const next = () =>
      reader.readEntries((batch) => {
        if (!batch.length) return resolve(all);
        all.push(...batch);
        next();
      }, reject);
    next();
  });
}

async function collectEntry(entry, dir, out) {
  if (entry.isFile) {
    const file = await new Promise((resolve, reject) => entry.file(resolve, reject));
    out.push({ file, dir });
    return;
  }
  if (entry.isDirectory) {
    const sub = dir ? `${dir}/${safeFolderName(entry.name)}` : safeFolderName(entry.name);
    for (const child of await readAllEntries(entry.createReader())) {
      await collectEntry(child, sub, out);
    }
  }
}

async function uploadDropped(entries, files, baseDir) {
  let dropped = [];
  try {
    if (entries.length && entries.every(Boolean)) {
      for (const entry of entries) await collectEntry(entry, "", dropped);
    } else {
      dropped = files.map((file) => ({ file, dir: "" }));
    }
  } catch (error) {
    toast("Impossible de lire les fichiers déposés.", true);
    return;
  }

  const audio = dropped.filter((item) => isAudioPath(item.file.name));
  const ignored = dropped.length - audio.length;
  if (!audio.length) {
    toast("Aucun fichier audio dans ce qui a été déposé.", true);
    return;
  }

  // Un envoi par dossier de destination: la boîte reçoit un seul dossier cible par requête.
  const groups = new Map();
  for (const item of audio) {
    const target = item.dir ? `${baseDir}/${item.dir}` : baseDir;
    if (!groups.has(target)) groups.set(target, []);
    groups.get(target).push(item.file);
  }
  for (const [target, groupFiles] of groups) {
    const ok = await uploadWithProgress(groupFiles, target, $("files-progress"), $("files-progress-label"), $("files-progress-bar"));
    if (!ok) break;
  }
  if (ignored) toast(`${plural(ignored, "fichier non audio ignoré", "fichiers non audio ignorés")}.`);
  await loadLibrary();
  renderFiles();
}

$("view-files").addEventListener("dragover", (event) => {
  if (!hasDraggedFiles(event) || !canDropFiles()) return;
  event.preventDefault();
  const blocked = isPlaying() || $("files-main").hidden;
  event.dataTransfer.dropEffect = blocked ? "none" : "copy";
  if (blocked) return;
  $("files-main").classList.add("drop-active");
  const row = event.target.closest("#files-list .row");
  setDropTarget(row && row.querySelector("[data-dir]") ? row : null);
});

$("view-files").addEventListener("dragleave", (event) => {
  if (!$("view-files").contains(event.relatedTarget)) clearDrop();
});

$("view-files").addEventListener("drop", (event) => {
  if (!hasDraggedFiles(event) || !canDropFiles()) return;
  event.preventDefault();
  const baseDir = dropTargetRow ? dropTargetRow.querySelector("[data-dir]").dataset.dir : app.currentDir;
  clearDrop();
  if (isPlaying()) {
    toast(errorText("busy_playing"), true);
    return;
  }
  // Les entrées ne sont lisibles que pendant l'événement: on les relève avant tout await.
  const items = Array.from(event.dataTransfer.items || []).filter((item) => item.kind === "file");
  const entries = items.map((item) => (item.webkitGetAsEntry ? item.webkitGetAsEntry() : null));
  uploadDropped(entries, Array.from(event.dataTransfer.files || []), baseDir);
});

// Un fichier lâché à côté de la zone ne doit pas faire quitter la page pour l'ouvrir.
["dragover", "drop"].forEach((type) =>
  window.addEventListener(type, (event) => {
    if (hasDraggedFiles(event)) event.preventDefault();
  }),
);

$("files-mkdir").addEventListener("click", async () => {
  const name = await ask({ title: "Nouveau dossier", value: "", okLabel: "Créer", validate: (v) => (v ? "" : "Saisis un nom.") });
  if (!name) return;
  const path = await createFolder(app.currentDir, name);
  if (!path) return;
  await loadLibrary();
  app.currentDir = path;
  renderFiles();
});

$("files-list").addEventListener("click", async (event) => {
  const button = event.target.closest("[data-actions]");
  if (!button) return;
  const path = button.dataset.actions;
  const name = baseName(path);
  const choice = await ask({
    title: "Renommer, déplacer ou supprimer",
    value: name,
    okLabel: "Renommer",
    altLabel: "Corbeille",
    extraLabel: "Déplacer dans un autre dossier…",
    validate: (v) => (v ? "" : "Saisis un nom."),
  });
  if (choice === "alt") {
    trashPath(path);
    return;
  }
  if (choice === "extra") {
    const target = await chooseMoveTarget(path);
    if (target) await moveEntry(path, target);
    return;
  }
  if (choice === null || choice === name) return;
  const result = await api("/api/rename", { json: { path, name: choice, conflictMode: "rename" } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  await relinkBindings(path, result.data.renamedPath);
  toast("Renommé.");
  await loadLibrary();
  renderFiles();
});

// ---------- Déplacement ----------

function chooseMoveTarget(path) {
  const dialog = $("move-dialog");
  const current = parentDir(path);
  const folders = [LIBRARY_ROOT, ...allFolders(app.library && app.library.entries)].filter((dir) => canMoveInto(path, dir));
  $("move-title").textContent = `Déplacer « ${baseName(path)} »`;
  $("move-search").value = "";
  const render = () => {
    const query = $("move-search").value.trim().toLowerCase();
    const rows = folders
      .filter((dir) => !query || relPath(dir).toLowerCase().includes(query))
      .map((dir) => {
        const here = dir === current;
        return `
          <button class="row" type="button" data-move-to="${esc(dir)}" ${here ? "disabled" : ""}>
            ${icon("folder")}
            <span class="row-body">
              <span class="row-title" style="display:block">${esc(relPath(dir))}</span>
              ${here ? `<span class="small">Dossier actuel</span>` : ""}
            </span>
            ${here ? "" : icon("chevron")}
          </button>`;
      });
    setHtml($("move-folders"), rows.join("") || `<div class="empty">Aucun dossier ne correspond.</div>`);
  };
  render();
  return new Promise((resolve) => {
    let chosen = null;
    const onClick = (event) => {
      const row = event.target.closest("[data-move-to]");
      if (!row || row.disabled) return;
      chosen = row.dataset.moveTo;
      dialog.close("ok");
    };
    $("move-folders").addEventListener("click", onClick);
    $("move-search").addEventListener("input", render);
    dialog.addEventListener(
      "close",
      () => {
        $("move-folders").removeEventListener("click", onClick);
        $("move-search").removeEventListener("input", render);
        resolve(chosen);
      },
      { once: true }
    );
    openDialog(dialog);
  });
}

async function moveEntry(path, targetDir) {
  if (!canMoveInto(path, targetDir) || parentDir(path) === targetDir) return;
  const result = await api("/api/move", { json: { path, targetDir } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  // Les cartes associées suivent le fichier ou le dossier déplacé.
  await relinkBindings(path, result.data.movedPath);
  toast(`Déplacé dans « ${relPath(targetDir)} ».`);
  await loadLibrary();
  renderFiles();
}

// Sur ordinateur: une ligne glissée sur un dossier de la liste ou du fil d'Ariane y est déplacée.
const MOVE_MIME = "application/x-totobox-path";
let movingPath = "";
let moveTarget = null;

function setMoveTarget(element) {
  if (moveTarget === element) return;
  if (moveTarget) moveTarget.classList.remove("drop-target");
  moveTarget = element;
  if (element) element.classList.add("drop-target");
}

function moveTargetDir(event) {
  const crumb = event.target.closest("#crumbs [data-dir]");
  if (crumb) return { element: crumb, dir: crumb.dataset.dir };
  const row = event.target.closest("#files-list .row");
  const dirButton = row && row.querySelector("[data-dir]");
  return dirButton ? { element: row, dir: dirButton.dataset.dir } : null;
}

$("files-list").addEventListener("dragstart", (event) => {
  const row = event.target.closest(".row[data-path]");
  if (!row) return;
  movingPath = row.dataset.path;
  event.dataTransfer.setData(MOVE_MIME, movingPath);
  event.dataTransfer.effectAllowed = "move";
  row.classList.add("dragging");
});

$("files-list").addEventListener("dragend", (event) => {
  const row = event.target.closest(".row");
  if (row) row.classList.remove("dragging");
  movingPath = "";
  setMoveTarget(null);
});

$("view-files").addEventListener("dragover", (event) => {
  if (!movingPath || !Array.from(event.dataTransfer.types || []).includes(MOVE_MIME)) return;
  const target = moveTargetDir(event);
  if (!target || !canMoveInto(movingPath, target.dir) || parentDir(movingPath) === target.dir) {
    setMoveTarget(null);
    return;
  }
  event.preventDefault();
  event.dataTransfer.dropEffect = "move";
  setMoveTarget(target.element);
});

$("view-files").addEventListener("drop", (event) => {
  if (!movingPath || !Array.from(event.dataTransfer.types || []).includes(MOVE_MIME)) return;
  event.preventDefault();
  const target = moveTargetDir(event);
  const path = movingPath;
  movingPath = "";
  setMoveTarget(null);
  if (target) moveEntry(path, target.dir);
});

async function trashPath(path) {
  const confirmed = await ask({
    title: "Mettre à la corbeille ?",
    message: `« ${baseName(path)} » pourra être restauré depuis la corbeille.`,
    okLabel: "Mettre à la corbeille",
    danger: true,
  });
  if (!confirmed) return;
  const result = await api("/api/delete", { json: { path } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  toast("Mis à la corbeille.");
  await loadLibrary();
  renderFiles();
}

// Un dossier renommé reste associé à ses cartes.
async function relinkBindings(oldPath, newPath) {
  // Essai sur une copie: rien à envoyer si aucune carte n'est concernée.
  if (!retargetMediaPaths(bindings().map((b) => ({ ...b })), oldPath, newPath)) return;
  await saveBindings((list) => retargetMediaPaths(list, oldPath, newPath));
}

$("trash-list").addEventListener("click", async (event) => {
  const button = event.target.closest("[data-restore]");
  if (!button) return;
  const result = await api("/api/restore", { json: { path: button.dataset.restore } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  toast(`Restauré dans ${relPath(result.data.restoredPath.slice(0, result.data.restoredPath.lastIndexOf("/")))}.`);
  await loadTrash();
  app.library = null;
  renderFiles();
});

$("trash-purge").addEventListener("click", async () => {
  const confirmed = await ask({
    title: "Vider la corbeille ?",
    message: "Les fichiers seront supprimés définitivement de la carte SD.",
    okLabel: "Supprimer définitivement",
    danger: true,
  });
  if (!confirmed) return;
  const result = await api("/api/trash/purge", { method: "POST" });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  toast("Corbeille vidée.");
  await loadTrash();
  renderFiles();
});

// ---------- Réglages ----------

function syncVolumeOutputs() {
  $("vol-min-out").textContent = $("vol-min").value;
  $("vol-max-out").textContent = $("vol-max").value;
}

$("vol-min").addEventListener("input", () => {
  if (Number($("vol-min").value) > Number($("vol-max").value)) $("vol-max").value = $("vol-min").value;
  syncVolumeOutputs();
});
$("vol-max").addEventListener("input", () => {
  if (Number($("vol-max").value) < Number($("vol-min").value)) $("vol-min").value = $("vol-max").value;
  syncVolumeOutputs();
});

function renderDeviceInfo() {
  const s = app.status || {};
  const rows = [
    ["Réseau", s.wifiMode === "ap" ? "Point d'accès" : s.wifiMode === "sta" ? (s.wifiConnected ? "Wi-Fi maison" : "Wi-Fi maison (reconnexion…)") : s.wifiMode === "connecting" ? "Connexion en cours…" : "—"],
    ["Adresse", s.wifiIp || "—"],
    ["Nom", s.hostname ? `http://${s.hostname}` : "—"],
    ["Carte SD", s.sdReady ? `${fmtBytes(s.sdFreeBytes)} libres / ${fmtBytes(s.sdTotalBytes)}` : "Absente"],
    ["Lecteur NFC", s.nfcReady ? "OK" : "Non détecté"],
    ["Cartes associées", String(s.bindingsCount ?? bindings().length)],
    // Version et commit compilé: à donner dans un signalement de problème.
    ["Firmware", s.firmwareVersion ? `${s.firmwareVersion} (${s.firmwareBuild || "?"})` : "—"],
  ];
  $("device-info").innerHTML = rows.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join("");
}

$("open-settings").addEventListener("click", async () => {
  await loadConfig();
  const c = app.config;
  $("vol-min").value = c.volumeMin ?? 20;
  $("vol-max").value = c.volumeMax ?? 85;
  $("vol-current").textContent = c.currentVolume ?? "?";
  syncVolumeOutputs();
  $("wifi-ssid").value = c.wifiSsid || "";
  $("wifi-forget").hidden = !c.wifiSsid;
  $("wifi-password").value = "";
  $("ap-ssid").value = c.apSsid || "";
  $("ap-password").value = "";
  $("hostname").value = c.hostname || "totobox";
  renderHostnamePreview();
  $("settings-error").hidden = true;
  renderSettingsLock();
  renderDeviceInfo();
  openDialog($("settings-dialog"));
  refreshBackupInfo();
});


function byteLength(value) {
  return new TextEncoder().encode(value).length;
}

function renderHostnamePreview() {
  const name = normalizeHostname($("hostname").value);
  $("hostname-preview").textContent = HOSTNAME_PATTERN.test(name) ? `http://${name}.local` : "nom invalide";
}

$("hostname").addEventListener("input", renderHostnamePreview);

$("settings-save").addEventListener("click", async () => {
  const error = $("settings-error");
  const fail = (message) => {
    error.textContent = message;
    error.hidden = false;
  };
  const wifiSsid = $("wifi-ssid").value.trim();
  const wifiPassword = $("wifi-password").value;
  const apSsid = $("ap-ssid").value.trim() || app.config.apSsid;
  const apPassword = $("ap-password").value;
  const hostname = normalizeHostname($("hostname").value);

  if (byteLength(wifiSsid) > 32) return fail(errorText("invalid_wifi_ssid"));
  if (wifiPassword && (wifiPassword.length < 8 || wifiPassword.length > 63)) return fail(errorText("invalid_wifi_password"));
  if (byteLength(apSsid) > 32) return fail(errorText("invalid_ap_ssid"));
  if (apPassword && (apPassword.length < 8 || apPassword.length > 63)) return fail(errorText("invalid_ap_password"));
  if (!HOSTNAME_PATTERN.test(hostname)) return fail(errorText("invalid_hostname"));

  const before = app.config;
  const previousAttemptId = app.status && app.status.networkAttempt ? app.status.networkAttempt.id : 0;
  const payload = {
    volumeMin: Number($("vol-min").value),
    volumeMax: Number($("vol-max").value),
    wifiSsid,
    apSsid,
    hostname,
  };
  // Champ vide = mot de passe inchangé (la boîte ne renvoie jamais les mots de passe).
  const secrets = {};
  if (wifiPassword) secrets.wifiPassword = wifiPassword;
  if (apPassword) secrets.apPassword = apPassword;
  const networkChanged =
    wifiSsid !== (before.wifiSsid || "") || Boolean(wifiPassword) || apSsid !== before.apSsid || Boolean(apPassword);
  const hostnameChanged = hostname !== before.hostname;

  const result = await apiWithSecrets("/api/config", payload, secrets);
  if (!result.ok) return fail(errorText(result.error));
  app.config = result.data;
  $("settings-dialog").close();
  if (networkChanged) {
    openNetworkProgress(wifiSsid, apSsid, previousAttemptId);
  } else {
    toast(hostnameChanged ? `Réglages enregistrés. Nouvelle adresse: http://${hostname}.local` : "Réglages enregistrés.");
  }
  refreshStatus();
});

// Efface le Wi-Fi maison (nom et mot de passe): la boîte repasse sur son point d'accès.
$("wifi-forget").addEventListener("click", async () => {
  const ssid = app.config.wifiSsid;
  const apSsid = app.config.apSsid;
  const confirmed = await ask({
    title: "Oublier ce réseau ?",
    message: `La boîte va quitter le Wi-Fi « ${ssid} » et créer son point d'accès « ${apSsid} ». Pour la retrouver, connecte ton téléphone à ce point d'accès.`,
    okLabel: "Oublier",
    danger: true,
  });
  if (!confirmed) return;
  const previousAttemptId = app.status && app.status.networkAttempt ? app.status.networkAttempt.id : 0;
  // Mot de passe envoyé vide: sinon la boîte garderait l'ancien en mémoire.
  const result = await apiWithSecrets("/api/config", { wifiSsid: "" }, { wifiPassword: "" });
  if (!result.ok) {
    $("settings-error").textContent = errorText(result.error);
    $("settings-error").hidden = false;
    return;
  }
  app.config = result.data;
  $("settings-dialog").close();
  openNetworkProgress("", apSsid, previousAttemptId);
});

// ---------- Carte SD illisible ----------
// Présente mais pas en FAT16/FAT32 (exFAT des cartes de 64 Go et plus, NTFS, carte vierge): la boîte ne peut
// rien en faire. Formatage par la boîte sur demande explicite, ou sur un ordinateur.

const SD_HELP = `La carte est bien insérée, mais la boîte ne lit que les cartes formatées en FAT32 (les cartes de 64 Go et plus sont souvent livrées en exFAT).
Sur un Mac : Utilitaire de disque › la carte › Effacer › format « MS-DOS (FAT) ».
Sur Windows : Windows ne propose pas FAT32 au-delà de 32 Go ; utilise l'outil gratuit « SD Card Formatter », ou formate-la depuis la boîte.
Le formatage efface tout le contenu de la carte.`;
document.querySelectorAll(".sd-help").forEach((node) => {
  node.style.whiteSpace = "pre-line";
  node.textContent = SD_HELP;
});

function sdUnreadable() {
  return Boolean(app.status && app.status.sdState === "unreadable");
}

let formattingSd = false;

async function formatSdCard() {
  if (formattingSd) return;
  const confirm = await ask({
    title: "Formater la carte SD ?",
    message: "Tout son contenu sera effacé et elle sera formatée en FAT32 pour la boîte. Jusqu'à une minute pour une grande carte : ne la retire pas pendant ce temps. Tape FORMATER pour confirmer.",
    value: "",
    okLabel: "Formater",
    danger: true,
    validate: (v) => (v === "FORMATER" ? "" : "Tape FORMATER pour confirmer."),
  });
  if (confirm !== "FORMATER") return;
  formattingSd = true;
  document.querySelectorAll("[data-format-sd]").forEach((button) => {
    button.disabled = true;
    button.textContent = "Formatage en cours…";
  });
  toast("Formatage en cours… ne retire pas la carte.");
  app.busyRequests += 1;
  const result = await api("/api/sd/format", { json: { confirm } });
  app.busyRequests -= 1;
  formattingSd = false;
  document.querySelectorAll("[data-format-sd]").forEach((button) => {
    button.disabled = false;
    button.textContent = "Formater la carte…";
  });
  toast(result.ok ? "Carte formatée : elle est prête." : errorText(result.error), !result.ok);
  await refreshStatus();
  if (result.ok && app.view === "files") openFiles(app.filesMode);
}

document.addEventListener("click", (event) => {
  if (event.target.closest("[data-format-sd]")) formatSdCard();
});

// ---------- Sauvegarde des cartes sur la carte SD ----------

// { exists, count, savedAt } de la dernière lecture, null si inconnue.
let cardBackup = null;

function fmtSavedAt(savedAt) {
  const date = savedAt ? new Date(savedAt) : null;
  return date && !Number.isNaN(date.getTime())
    ? ` du ${date.toLocaleDateString("fr-FR")} à ${date.toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" })}`
    : "";
}

function renderBackupInfo(message = "") {
  const busy = isPlaying();
  let text = message;
  if (!text) {
    if (busy) text = "Disponible quand la boîte est à l'arrêt.";
    else if (!cardBackup) text = "Lecture de la carte SD…";
    else if (!cardBackup.exists) text = "Aucune sauvegarde sur la carte SD.";
    else text = `Sauvegarde${fmtSavedAt(cardBackup.savedAt)} : ${plural(cardBackup.count, "carte", "cartes")}. Une nouvelle sauvegarde la remplace.`;
  }
  $("backup-info").textContent = text;
  $("backup-save").disabled = busy || !bindings().length;
  $("backup-restore").disabled = busy || !(cardBackup && cardBackup.exists);
}

async function refreshBackupInfo() {
  cardBackup = null;
  renderBackupInfo();
  if (isPlaying()) return;
  const result = await api("/api/cards/backup");
  if (!result.ok) {
    renderBackupInfo(errorText(result.error));
    return;
  }
  cardBackup = result.data;
  renderBackupInfo();
}

$("backup-save").addEventListener("click", async () => {
  const result = await api("/api/cards/backup", { json: { savedAt: new Date().toISOString() } });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  cardBackup = result.data;
  renderBackupInfo();
  toast(`${plural(result.data.count, "carte sauvegardée", "cartes sauvegardées")} sur la carte SD.`);
});

$("backup-restore").addEventListener("click", async () => {
  if (!cardBackup || !cardBackup.exists) return;
  const current = bindings().length;
  const confirmed = await ask({
    title: "Restaurer les cartes ?",
    message: `La sauvegarde${fmtSavedAt(cardBackup.savedAt)} (${plural(cardBackup.count, "carte", "cartes")}) remplacera ${current ? `les cartes associées actuelles (${current})` : "la liste actuelle, vide"}. Les marque-pages sont restaurés aussi.`,
    okLabel: "Restaurer",
  });
  if (!confirmed) return;
  const result = await api("/api/cards/restore", { method: "POST" });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  await loadConfig();
  renderHome();
  renderBackupInfo();
  const skipped = result.data.skipped ? ` (${plural(result.data.skipped, "carte maîtresse ignorée", "cartes maîtresses ignorées")})` : "";
  toast(`${plural(result.data.count, "carte restaurée", "cartes restaurées")}${skipped}.`);
});

// Remise à zéro: la boîte efface tout sauf les fichiers de la carte SD et redémarre en point d'accès.
$("factory-reset").addEventListener("click", async () => {
  const confirm = await ask({
    title: "Réinitialiser la boîte ?",
    message:
      "Réglages, Wi-Fi, verrouillage, cartes associées et marque-pages seront effacés. Les cartes associées sont d'abord sauvegardées sur la carte SD (Réglages > Restaurer pour les retrouver), les histoires restent. Tape EFFACER pour confirmer.",
    value: "",
    okLabel: "Réinitialiser",
    danger: true,
    validate: (v) => (v === "EFFACER" ? "" : errorText("confirm_required")),
  });
  if (confirm !== "EFFACER") return;
  const result = await api("/api/factory-reset", { json: { confirm, savedAt: new Date().toISOString() } });
  if (!result.ok) {
    $("settings-error").textContent = errorText(result.error);
    $("settings-error").hidden = false;
    return;
  }
  $("settings-dialog").close();
  const password = result.data.factoryPassword ? "le mot de passe imprimé sur l'étiquette" : "le mot de passe affiché dans la console série de la boîte";
  const backup = result.data.backupSaved
    ? ` ${plural(result.data.backupCount, "carte a été sauvegardée", "cartes ont été sauvegardées")} sur la carte SD : Réglages > Restaurer pour les retrouver.`
    : "";
  $("reset-message").textContent = `Elle redémarre sur son point d'accès « ${result.data.apSsid} ». Connecte ton téléphone à ce Wi-Fi avec ${password} pour la configurer.${backup}`;
  openDialog($("reset-dialog"));
});

// ---------- Assistant de première connexion ----------
// Étapes: carte SD (si absente), nom, cartes maîtresses, restauration (si une sauvegarde existe), Wi-Fi, fin.
// Le Wi-Fi vient en dernier: une fois la boîte sur le réseau de la maison, son point d'accès se coupe et le
// téléphone la perd; l'assistant est donc marqué terminé juste avant de l'appliquer.

const SETUP_TITLES = {
  sd: "Carte SD",
  name: "Nom de la boîte",
  lock: "Cartes maîtresses",
  restore: "Restaurer les cartes",
  wifi: "Wi-Fi de la maison",
  done: "C'est prêt !",
};
const setup = { step: "", skipSd: false, backup: null, backupLoading: false, restored: 0, wifiSsid: "" };

function setupSteps() {
  const steps = [];
  if (!(app.status && app.status.sdReady) && !setup.skipSd) steps.push("sd");
  steps.push("name", "lock");
  if (setup.backup && setup.backup.exists) steps.push("restore");
  steps.push("wifi", "done");
  return steps;
}

async function loadSetupBackup() {
  if (setup.backup || setup.backupLoading || !(app.status && app.status.sdReady) || isPlaying()) return;
  setup.backupLoading = true;
  const result = await api("/api/cards/backup");
  setup.backupLoading = false;
  if (result.ok) {
    setup.backup = result.data;
    renderSetup();
  }
}

function openSetup() {
  const steps = setupSteps();
  if (!steps.includes(setup.step)) setup.step = steps[0];
  const hostname = app.config.hostname;
  if (!$("setup-name").value && hostname && hostname !== "totobox") $("setup-name").value = hostname;
  showView("setup");
  renderSetup();
}

function goSetup(step) {
  setup.step = step;
  renderSetup();
  window.scrollTo(0, 0);
}

function renderSetup() {
  const steps = setupSteps();
  // Carte SD détectée pendant l'étape: on passe à la suite.
  if (!steps.includes(setup.step)) setup.step = steps[0];
  const index = steps.indexOf(setup.step);
  const step = setup.step;
  loadSetupBackup();

  $("setup-title").textContent = SETUP_TITLES[step];
  $("setup-step-label").textContent = step === "done" ? "" : `${index + 1}/${steps.length - 1}`;
  setHtml($("setup-steps"), steps.slice(0, -1).map((_, i) => `<span class="${i <= index ? "on" : ""}"></span>`).join(""));
  document.querySelectorAll("[data-setup-panel]").forEach((panel) => (panel.hidden = panel.dataset.setupPanel !== step));

  $("setup-prev").hidden = index === 0 || step === "done";
  $("setup-next").hidden = step === "sd";
  $("setup-later").hidden = step === "done";
  const nextLabels = { lock: masterCards().length ? "Continuer" : "Passer", restore: "Ne pas restaurer", wifi: "Connecter", done: "Aller à l'accueil" };
  $("setup-next").textContent = nextLabels[step] || "Continuer";
  $("setup-next").classList.toggle("primary", step !== "restore");

  if (step === "sd") {
    $("setup-sd-missing").hidden = sdUnreadable();
    $("setup-sd-unreadable").hidden = !sdUnreadable();
    $("setup-title").textContent = sdUnreadable() ? "Carte SD illisible" : SETUP_TITLES.sd;
  }
  if (step === "name") {
    const hostname = hostnameFromName($("setup-name").value) || "totobox";
    $("setup-name-preview").textContent = `http://${hostname}.local`;
  }
  if (step === "lock") {
    const cards = masterCards();
    $("setup-lock-cards").hidden = !cards.length;
    setHtml(
      $("setup-lock-cards"),
      cards.map((card) => `<div class="row">${icon("nfc")}<div class="row-body"><div class="row-title">${esc(card.label || "Carte maîtresse")}</div><div class="small mono">${esc(card.uid)}</div></div></div>`).join("")
    );
    $("setup-lock-add").lastChild.textContent = cards.length ? "Ajouter une autre carte" : "Ajouter une carte maîtresse";
  }
  if (step === "restore" && setup.backup) {
    $("setup-restore-info").textContent = `La carte SD contient une sauvegarde${fmtSavedAt(setup.backup.savedAt)} avec ${plural(setup.backup.count, "carte associée", "cartes associées")} et leurs marque-pages.`;
    $("setup-restore").disabled = isPlaying();
  }
  if (step === "wifi" && !$("setup-wifi-ssid").value && app.config.wifiSsid) $("setup-wifi-ssid").value = app.config.wifiSsid;
  if (step === "done") {
    const hostname = app.config.hostname || "totobox";
    const rows = [
      ["Adresse", setup.wifiSsid ? `http://${hostname}.local` : "http://192.168.4.1"],
      ["Wi-Fi", setup.wifiSsid ? `« ${setup.wifiSsid} »` : `point d'accès « ${app.config.apSsid} »`],
      ["Cartes maîtresses", masterCards().length ? String(masterCards().length) : "aucune (réglages non protégés)"],
      ["Cartes associées", String(bindings().length)],
    ];
    setHtml($("setup-summary"), rows.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join(""));
  }
}

async function completeSetup() {
  const result = await api("/api/setup/complete", { method: "POST" });
  if (result.ok && app.status) app.status.setupDone = true;
  return result.ok;
}

$("setup-name").addEventListener("input", renderSetup);
$("setup-skip-sd").addEventListener("click", () => {
  setup.skipSd = true;
  goSetup("name");
});
$("setup-lock-add").addEventListener("click", () => openLockSetup(app.config.lockEnabled ? "add" : "enable"));
$("setup-prev").addEventListener("click", () => {
  const steps = setupSteps();
  goSetup(steps[Math.max(0, steps.indexOf(setup.step) - 1)]);
});

$("setup-restore").addEventListener("click", async () => {
  const result = await api("/api/cards/restore", { method: "POST" });
  if (!result.ok) {
    toast(errorText(result.error), true);
    return;
  }
  setup.restored = result.data.count;
  await loadConfig();
  toast(`${plural(result.data.count, "carte restaurée", "cartes restaurées")}.`);
  goSetup("wifi");
});

$("setup-wifi-skip").addEventListener("click", async () => {
  if (!(await completeSetup())) return toast(errorText("network"), true);
  setup.wifiSsid = "";
  goSetup("done");
});

$("setup-later").addEventListener("click", async () => {
  if (!(await completeSetup())) return toast(errorText("network"), true);
  showView("home");
  toast("Tout reste réglable depuis les réglages (roue dentée).");
});

$("setup-next").addEventListener("click", async () => {
  const steps = setupSteps();
  const next = steps[steps.indexOf(setup.step) + 1];
  if (setup.step === "name") {
    const hostname = hostnameFromName($("setup-name").value) || "totobox";
    if (!HOSTNAME_PATTERN.test(hostname)) {
      $("setup-name-error").textContent = errorText("invalid_hostname");
      $("setup-name-error").hidden = false;
      return;
    }
    $("setup-name-error").hidden = true;
    if (hostname !== app.config.hostname) {
      const result = await api("/api/config", { json: { hostname } });
      if (!result.ok) {
        $("setup-name-error").textContent = errorText(result.error);
        $("setup-name-error").hidden = false;
        return;
      }
      app.config = result.data;
    }
  }
  if (setup.step === "wifi") {
    const ssid = $("setup-wifi-ssid").value.trim();
    const password = $("setup-wifi-password").value;
    const fail = (message) => {
      $("setup-wifi-error").textContent = message;
      $("setup-wifi-error").hidden = false;
    };
    if (!ssid) return fail("Saisis le nom du Wi-Fi, ou reste sur le point d'accès de la boîte.");
    if (byteLength(ssid) > 32) return fail(errorText("invalid_wifi_ssid"));
    if (password && (password.length < 8 || password.length > 63)) return fail(errorText("invalid_wifi_password"));
    $("setup-wifi-error").hidden = true;
    // Terminé avant d'appliquer le Wi-Fi: la page peut perdre la boîte dès qu'elle rejoint le réseau.
    if (!(await completeSetup())) return fail(errorText("network"));
    const previousAttemptId = app.status && app.status.networkAttempt ? app.status.networkAttempt.id : 0;
    const result = await apiWithSecrets("/api/config", { wifiSsid: ssid }, { wifiPassword: password });
    if (!result.ok) return fail(errorText(result.error));
    app.config = result.data;
    setup.wifiSsid = ssid;
    goSetup("done");
    openNetworkProgress(ssid, app.config.apSsid, previousAttemptId);
    return;
  }
  if (setup.step === "done") {
    showView("home");
    return;
  }
  if (next) goSetup(next);
});

// ---------- Suivi d'une connexion Wi-Fi ----------

const NETWORK_POLL_MS = 2000;
const NETWORK_ERRORS = {
  wrong_password: "Mot de passe incorrect.",
  network_not_found: "Réseau introuvable: vérifie son nom et que la box est à portée.",
  connection_failed: "Connexion impossible.",
};
const networkProgress = { ssid: "", apSsid: "", fromAp: false, startedAt: 0, previousId: 0, timer: 0 };

// previousId: numéro de l'essai connu avant l'enregistrement; seul un essai plus récent décrit le nouveau.
function openNetworkProgress(ssid, apSsid, previousId) {
  const mode = app.status && app.status.wifiMode;
  Object.assign(networkProgress, { ssid, apSsid, fromAp: mode === "ap", startedAt: Date.now(), previousId });
  renderNetworkProgress();
  openDialog($("network-dialog"));
  clearInterval(networkProgress.timer);
  networkProgress.timer = setInterval(async () => {
    await loadStatus();
    renderNetworkProgress();
  }, NETWORK_POLL_MS);
}

// L'événement "close" arrive après coup: ne pas arrêter le suivi d'une fenêtre rouverte entre-temps.
$("network-dialog").addEventListener("close", () => {
  if (!$("network-dialog").open) clearInterval(networkProgress.timer);
});
$("network-edit").addEventListener("click", () => {
  $("network-dialog").close();
  $("open-settings").click();
});

function renderNetworkProgress() {
  const { ssid, apSsid, fromAp, startedAt, previousId } = networkProgress;
  const status = app.status || {};
  const current = status.networkAttempt || {};
  const attempt = current.id !== undefined && current.id !== previousId ? current : {};
  const address = `http://${(app.config.hostname || "totobox")}.local`;
  let state = "trying";
  let title = "";
  let message = "";
  let showAddress = false;

  if (!ssid) {
    state = "done";
    title = "Réseau oublié";
    message = fromAp
      ? `La boîte reste sur son point d'accès « ${apSsid} ».`
      : `La boîte passe sur son point d'accès « ${apSsid} ». Connecte ton téléphone à ce Wi-Fi: sa page s'ouvrira d'elle-même, sinon ouvre http://192.168.4.1.`;
  } else if (!fromAp) {
    // Depuis le Wi-Fi de la maison: la page perd la boîte si le téléphone n'est pas sur le nouveau réseau.
    if (attempt.state === "connected" && app.online) {
      state = "done";
      title = `Connectée à « ${ssid} »`;
      message = "La boîte est joignable à cette adresse:";
      showAddress = true;
    } else if (attempt.state === "failed" || (!app.online && Date.now() - startedAt > 20000)) {
      state = "done";
      title = `La boîte rejoint « ${ssid} »`;
      message = `Connecte ton téléphone à ce Wi-Fi puis ouvre l'adresse ci-dessous. Si la connexion a échoué, la boîte a recréé son point d'accès « ${apSsid} ».`;
      showAddress = true;
    } else {
      title = `Connexion à « ${ssid} »…`;
      message = "Jusqu'à 15 secondes. Si ton téléphone n'est pas sur ce Wi-Fi, la page ne verra plus la boîte: retrouve-la ensuite à l'adresse ci-dessous.";
      showAddress = true;
    }
  } else if (!app.online && attempt.state === "connected") {
    // Le point d'accès s'est coupé: la page ne joint plus la boîte depuis le téléphone.
    state = "done";
    title = "Point d'accès coupé";
    message = `Connecte ton téléphone au Wi-Fi « ${ssid} » puis ouvre:`;
    showAddress = true;
  } else if (attempt.state === "connected") {
    state = "done";
    title = `Connectée à « ${ssid} »`;
    message = attempt.apClosesInSec > 0
      ? `Le point d'accès « ${apSsid} » se coupe dans ${attempt.apClosesInSec} s. Connecte ton téléphone au Wi-Fi « ${ssid} » puis ouvre:`
      : `Connecte ton téléphone au Wi-Fi « ${ssid} » puis ouvre:`;
    showAddress = true;
  } else if (attempt.state === "failed") {
    state = "failed";
    title = `Impossible de rejoindre « ${ssid} »`;
    message = `${NETWORK_ERRORS[attempt.error] || NETWORK_ERRORS.connection_failed} La boîte reste sur son point d'accès: corrige le réglage et réessaie.`;
  } else if (!app.online && Date.now() - startedAt > NETWORK_POLL_MS) {
    state = "done";
    title = "Point d'accès coupé";
    message = `Connecte ton téléphone au Wi-Fi « ${ssid} » puis ouvre:`;
    showAddress = true;
  } else {
    title = `Connexion à « ${ssid} »…`;
    message = `Jusqu'à 15 secondes. Ton téléphone reste connecté au point d'accès « ${apSsid} ».`;
  }

  $("network-waiting").hidden = state !== "trying";
  $("network-title").textContent = title;
  $("network-message").textContent = message;
  $("network-address").textContent = attempt.ip ? `${address} (${attempt.ip})` : address;
  $("network-address").hidden = !showAddress;
  $("network-edit").hidden = state !== "failed";
  if (state !== "trying") clearInterval(networkProgress.timer);
  // Point d'accès qui va se couper: on continue d'afficher le décompte.
  if (state === "done" && attempt.state === "connected" && attempt.apClosesInSec > 0 && fromAp) {
    clearInterval(networkProgress.timer);
    networkProgress.timer = setInterval(async () => {
      await loadStatus();
      renderNetworkProgress();
    }, NETWORK_POLL_MS);
  }
}

// ---------- Polling ----------

let pollTimer = 0;
let polling = false;

// Constaté sur la carte: le nom <nom>.local cesse d'être résolu ~2 min après l'ouverture de la page
// (durée de vie mDNS de 120 s, puis la box ne fait plus suivre les requêtes multicast), alors que la boîte
// répond toujours par son IP. Si la page a été ouverte par le nom et perd la boîte, on vérifie que l'IP
// répond puis on recharge la page sur l'IP.
// Un seul essai par coupure: si l'IP ne répond pas non plus, ce n'est pas le nom mais la boîte (veille,
// redémarrage, hors de portée); la page reste sur le nom et l'essai n'est refait qu'après une reconnexion.
// Sinon, au réveil, l'IP répondait parfois avant que le nom soit de nouveau annoncé et la page basculait.
async function fallbackToIpIfNameFails() {
  const ip = app.stationIp;
  if (app.ipFallbackTried || !ip || !location.hostname.endsWith(".local") || location.hostname === ip) return;
  app.ipFallbackTried = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3000);
  try {
    // Requête vers une autre origine: "no-cors" suffit à savoir si l'IP répond, sans lire la réponse.
    await fetch(`http://${ip}/api/status`, { mode: "no-cors", cache: "no-store", signal: controller.signal });
  } catch (error) {
    return;
  } finally {
    clearTimeout(timeout);
  }
  toast(`Le nom ${location.hostname} ne répond plus: passage à l'adresse ${ip}.`);
  setTimeout(() => location.replace(`http://${ip}${location.pathname}${location.hash}`), 1200);
}

// Inverse du repli: page ouverte par l'IP (repli après un raté du nom, ou adresse saisie), on vérifie de
// temps en temps que le nom répond et on y retourne. Seulement sur le Wi-Fi maison (en point d'accès, le
// DNS du portail captif répond à tous les noms) et quand la page est au repos: le rechargement ne doit pas
// interrompre une saisie, un envoi ou une fenêtre ouverte. L'intervalle limite les allers-retours si le
// nom reste instable.
const NAME_RETRY_MS = 5 * 60 * 1000;
const NAME_FIRST_TRY_MS = 30 * 1000;
let lastNameProbeMs = Date.now() - NAME_RETRY_MS + NAME_FIRST_TRY_MS;

async function returnToNameIfItAnswers() {
  const name = app.status && app.status.hostname;
  if (!name || app.status.wifiMode !== "sta" || !/^\d+\.\d+\.\d+\.\d+$/.test(location.hostname)) return;
  if (Date.now() - lastNameProbeMs < NAME_RETRY_MS) return;
  if (app.view !== "home" || app.busyRequests > 0 || document.querySelector("dialog[open]")) return;
  lastNameProbeMs = Date.now();
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3000);
  try {
    await fetch(`http://${name}/api/status`, { mode: "no-cors", cache: "no-store", signal: controller.signal });
  } catch (error) {
    return;  // le nom ne répond toujours pas: on reste sur l'IP
  } finally {
    clearTimeout(timeout);
  }
  toast(`Le nom ${name} répond de nouveau: retour à cette adresse.`);
  setTimeout(() => location.replace(`http://${name}${location.pathname}${location.hash}`), 1200);
}

async function refreshStatus() {
  // Pendant un envoi ou un formatage, la boîte ne répond à rien d'autre: ce silence n'est pas une perte de connexion.
  if (polling || app.busyRequests > 0) return;
  polling = true;
  const wasPlaying = isPlaying();
  const wasLocked = isLocked();
  const wasOnline = app.online;
  const ok = await loadStatus();
  // Boîte retrouvée: une prochaine coupure pourra de nouveau tester l'IP.
  if (ok) {
    app.ipFallbackTried = false;
    returnToNameIfItAnswers();
  }
  if (!ok && !app.online) {
    // Connexion perdue: la gestion (fichiers, réglages, cartes) se ferme, rien ne doit agir à l'aveugle.
    if (wasOnline) enterLockedView();
    await fallbackToIpIfNameFails();
  }
  if (ok) {
    const s = app.status;
    if (s.wifiMode === "sta" && s.wifiIp) app.stationIp = s.wifiIp;
    // Les marque-pages et associations changent quand une carte est posée/retirée.
    if (isLocked() && !wasLocked) enterLockedView();
    // Déverrouillée par la carte maîtresse pendant que la fenêtre du code est ouverte.
    if (!isLocked() && wasLocked && $("unlock-dialog").open) {
      $("unlock-dialog").close();
      toast("Réglages déverrouillés.");
    }
    const key = [s.state, s.activeCardUid, s.bindingsCount, s.lastUnknownCardUid, s.locked].join("|");
    if (key !== app.lastStatusKey) {
      app.lastStatusKey = key;
      await loadConfig();
    }
    // L'assistant attend une carte: on prend la première carte inconnue posée.
    if (assign.step === "card" && $("assign-dialog").open && s.state === "unknown_card" && s.activeCardUid) {
      acceptAssignUid(s.activeCardUid);
    }
    // La configuration du verrouillage attend la future carte maîtresse.
    if (lockSetup.step === "card" && $("lock-dialog").open && s.state === "unknown_card" && s.activeCardUid) {
      acceptLockUid(s.activeCardUid);
    }
    // Fin de lecture: les fichiers redeviennent accessibles.
    if (wasPlaying && !isPlaying() && app.view === "files") {
      openFiles(app.filesMode);
    }
  }
  renderHome();
  if (app.view === "files") renderFiles();
  if (app.online && app.status && !isLocked()) {
    if (!app.status.setupDone && app.view === "home") openSetup();
    else if (app.view === "setup") renderSetup();
  }
  polling = false;
}

function startPolling() {
  clearInterval(pollTimer);
  refreshStatus();
  pollTimer = setInterval(refreshStatus, POLL_MS);
}

document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    clearInterval(pollTimer);
  } else {
    startPolling();
  }
});

(async () => {
  if (!canUploadFiles()) showUploadHelp();
  renderHome();
  await loadConfig();
  startPolling();
})();
