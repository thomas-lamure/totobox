#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Faux serveur de développement: sert la page de web/ et imite l'API JSON de la boîte (docs/api.md), sans matériel.
//
//   node scripts/mock-server.mjs [port]        (port 8080 par défaut)
//
// Puis ouvrir http://localhost:8080 pour la page, et http://localhost:8080/mock pour simuler la boîte: poser ou
// retirer une carte, état de la carte SD, lecture. Tout est en mémoire et repart de zéro au redémarrage.
// Node 18 ou plus, sans dépendance. Les secrets chiffrés par la page (nacl.box) sont déchiffrés avec la même
// bibliothèque (web/vendor/nacl.min.js). Ce serveur ne vérifie ni l'hôte ni l'origine des requêtes
// (request_guard): il n'écoute que sur localhost.
//
// Le comportement suit src/web_admin.cpp; à mettre à jour quand une route change.

import { createServer } from "node:http";
import { createRequire } from "node:module";
import { randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { dirname, extname, join, normalize, sep } from "node:path";
import { fileURLToPath } from "node:url";

const WEB = join(dirname(fileURLToPath(import.meta.url)), "..", "web");
const nacl = createRequire(import.meta.url)(join(WEB, "vendor", "nacl.min.js"));
const PORT = Number(process.argv[2] || 8080);
// Même version que le firmware (include/version.h).
let firmwareVersion =
  readFileSync(join(WEB, "..", "include", "version.h"), "utf8").match(/kFirmwareVersion = "([^"]+)"/)?.[1] || "?";

const LIBRARY_ROOT = "/content";
const TRASH_ROOT = "/content/.trash";
const AUDIO = /\.(mp3|wav|m4a|aac|flac|ogg|opus)$/i;
const SD_TOTAL = 32 * 1024 ** 3;
const PIN_ATTEMPTS = 5;
const PIN_LOCKOUT_MS = 60_000;

// ---------- État simulé ----------

const boxKeys = nacl.box.keyPair();
let challenge = randomBytes(16);

const config = {
  volumeMin: 10,
  volumeMax: 70,
  currentVolume: 30,
  wifiSsid: "",
  apSsid: "TotoBox-1A2B",
  hostname: "totobox",
  lockEnabled: false,
  lockCards: [],
  pin: "",
  setupDone: false,
};

let bindings = [
  { uid: "04A1B2C3", label: "Le Petit Prince", mediaPath: "/content/stories/petit-prince", mode: "sd_local", resumeTrackIndex: 1, resumeMs: 83000, resumeByte: 0 },
  { uid: "04D4E5F6", label: "Comptines", mediaPath: "/content/music/comptines", mode: "sd_local", resumeTrackIndex: 0, resumeMs: 0, resumeByte: 0 },
];

const runtime = {
  state: "ready",
  sdState: "ready",
  unlocked: false,
  activeCardUid: "",
  activeTrackIndex: 0,
  activeTrackCount: 0,
  activePositionMs: 0,
  playingSince: 0,
  lastUnknownCardUid: "",
  pinFailures: 0,
  pinBlockedUntil: 0,
  networkAttempt: { id: 0, state: "", ssid: "", ip: "", error: "", apClosesInSec: 0 },
};

// Bibliothèque: chemin -> { type: "file" | "directory", size }
const files = new Map();
let backup = null;

const parentOf = (path) => path.slice(0, path.lastIndexOf("/")) || "/";

function ensureDir(path) {
  const parts = path.split("/").filter(Boolean);
  for (let i = 1; i <= parts.length; i++) {
    const dir = "/" + parts.slice(0, i).join("/");
    if (!files.has(dir)) files.set(dir, { type: "directory", size: 0 });
  }
}

function addFile(path, size) {
  ensureDir(parentOf(path));
  files.set(path, { type: "file", size });
}

function seedLibrary() {
  files.clear();
  files.set(LIBRARY_ROOT, { type: "directory", size: 0 });
  files.set(TRASH_ROOT, { type: "directory", size: 0 });
  addFile("/content/stories/petit-prince/01-introduction.mp3", 2_400_000);
  addFile("/content/stories/petit-prince/02-chapitre-01.mp3", 5_100_000);
  addFile("/content/stories/petit-prince/03-chapitre-02.mp3", 4_800_000);
  addFile("/content/music/comptines/01-au-clair-de-la-lune.mp3", 1_900_000);
  addFile("/content/music/comptines/02-meunier-tu-dors.mp3", 2_100_000);
  addFile("/content/music/berceuse.mp3", 3_000_000);
}
seedLibrary();

const isLocked = () => config.lockEnabled && !runtime.unlocked;
const audioPlaying = () => runtime.state === "playing";
const estimateSec = (size) => Math.round(size / 16000); // 128 kbit/s, comme la boîte
const baseName = (path) => path.slice(path.lastIndexOf("/") + 1);

function children(dir) {
  return [...files.keys()].filter((path) => path !== dir && parentOf(path) === dir);
}

function tree(dir, includeTrash) {
  const summary = { trackCount: 0, durationEstimateSec: 0, totalSize: 0 };
  const entries = children(dir)
    .filter((path) => includeTrash || (path !== TRASH_ROOT && !path.startsWith(TRASH_ROOT + "/")))
    .filter((path) => files.get(path).type === "directory" || AUDIO.test(path))
    .sort((a, b) => {
      const da = files.get(a).type === "directory", db = files.get(b).type === "directory";
      return da !== db ? (da ? -1 : 1) : a.toLowerCase().localeCompare(b.toLowerCase());
    })
    .map((path) => {
      const node = files.get(path);
      if (node.type === "directory") {
        const sub = tree(path, includeTrash);
        summary.trackCount += sub.summary.trackCount;
        summary.durationEstimateSec += sub.summary.durationEstimateSec;
        summary.totalSize += sub.summary.totalSize;
        return { path, type: "directory", children: sub.entries, ...sub.summary };
      }
      summary.trackCount += 1;
      summary.durationEstimateSec += estimateSec(node.size);
      summary.totalSize += node.size;
      return { path, type: "file", trackCount: 1, durationEstimateSec: estimateSec(node.size), totalSize: node.size };
    });
  return { entries, summary };
}

function sdUsed() {
  let used = 0;
  for (const node of files.values()) used += node.size;
  return used;
}

// Même règle que la boîte: suffixe -1, -2… si le nom est déjà pris.
function freePath(dir, name) {
  let candidate = `${dir}/${name}`;
  const dot = name.lastIndexOf(".");
  const [stem, ext] = dot > 0 ? [name.slice(0, dot), name.slice(dot)] : [name, ""];
  for (let i = 1; files.has(candidate); i++) candidate = `${dir}/${stem}-${i}${ext}`;
  return candidate;
}

function movePrefix(from, to) {
  for (const path of [...files.keys()]) {
    if (path === from || path.startsWith(from + "/")) {
      const node = files.get(path);
      files.delete(path);
      files.set(to + path.slice(from.length), node);
    }
  }
  // Comme la boîte, les cartes ne sont pas modifiées ici: la page renvoie les associations mises à jour.
}

function playlist(mediaPath) {
  const node = files.get(mediaPath);
  if (!node) return [];
  if (node.type === "file") return [mediaPath];
  return children(mediaPath).filter((path) => files.get(path).type === "file" && AUDIO.test(path)).sort();
}

// ---------- Mise à jour en ligne simulée (src/update_service.cpp) ----------
// Page puis firmware téléchargés en quelques secondes, redémarrage (la boîte ne répond plus), nouvelle version.

const update = { phase: "idle", version: "", startedAt: 0, page: 0, firmware: 0, error: "" };
let rebootingUntil = 0;
const UPDATE_PAGE_MS = 2000;
const UPDATE_FIRMWARE_MS = 5000;
const UPDATE_RESTART_MS = 3000;

function updateStatus() {
  const elapsed = Date.now() - update.startedAt;
  if (update.phase === "page" && elapsed >= UPDATE_PAGE_MS) update.phase = "firmware";
  if (update.phase === "firmware" && elapsed >= UPDATE_PAGE_MS + UPDATE_FIRMWARE_MS) {
    update.phase = "restarting";
    setTimeout(() => {
      rebootingUntil = Date.now() + UPDATE_RESTART_MS;
      firmwareVersion = update.version;
      Object.assign(update, { phase: "idle", page: 0, firmware: 0 });
      runtime.unlocked = false;  // redémarrage: verrouillée si le verrouillage est actif
    }, 1500);
  }
  const part = (start, length, total) => Math.max(0, Math.min(total, Math.round(((elapsed - start) / length) * total)));
  const done = update.phase === "page" ? part(0, UPDATE_PAGE_MS, update.page) : update.phase === "firmware" ? part(UPDATE_PAGE_MS, UPDATE_FIRMWARE_MS, update.firmware) : 0;
  const total = update.phase === "page" ? update.page : update.phase === "firmware" ? update.firmware : 0;
  return { phase: update.phase, version: update.version, done, total, error: update.error };
}

// ---------- Cartes NFC simulées ----------

function placeCard(uid) {
  removeCard();
  uid = uid.toUpperCase();
  if (config.lockCards.some((card) => card.uid === uid)) {
    runtime.unlocked = true;
    return;
  }
  const binding = bindings.find((item) => item.uid === uid);
  runtime.activeCardUid = uid;
  if (!binding) {
    runtime.state = "unknown_card";
    runtime.lastUnknownCardUid = uid;
    return;
  }
  const tracks = playlist(binding.mediaPath);
  if (runtime.sdState !== "ready" || tracks.length === 0) {
    runtime.state = "error";
    return;
  }
  runtime.state = "playing";
  runtime.activeTrackCount = tracks.length;
  runtime.activeTrackIndex = Math.min(binding.resumeTrackIndex, tracks.length - 1);
  runtime.activePositionMs = binding.resumeMs;
  runtime.playingSince = Date.now() - binding.resumeMs;
}

function removeCard() {
  if (audioPlaying()) {
    const binding = bindings.find((item) => item.uid === runtime.activeCardUid);
    if (binding) {
      binding.resumeTrackIndex = runtime.activeTrackIndex;
      binding.resumeMs = Date.now() - runtime.playingSince;
    }
  }
  runtime.state = runtime.sdState === "ready" ? "ready" : "error";
  runtime.activeCardUid = "";
  runtime.activeTrackIndex = 0;
  runtime.activeTrackCount = 0;
  runtime.activePositionMs = 0;
}

function setSdState(state) {
  if (state !== "ready" && audioPlaying()) removeCard();
  runtime.sdState = state;
  if (!runtime.activeCardUid) runtime.state = state === "ready" ? "ready" : "error";
}

// ---------- Secrets chiffrés (src/secret_channel.cpp) ----------

function unseal(body) {
  if (!body.sealed || typeof body.sealed !== "object") return null;
  try {
    const epk = Buffer.from(body.sealed.epk, "base64");
    const nonce = Buffer.from(body.sealed.nonce, "base64");
    const box = Buffer.from(body.sealed.box, "base64");
    const opened = nacl.box.open(new Uint8Array(box), new Uint8Array(nonce), new Uint8Array(epk), boxKeys.secretKey);
    if (!opened) return "sealed_invalid";
    const secrets = JSON.parse(Buffer.from(opened).toString("utf8"));
    if (Buffer.from(secrets.challenge || "", "base64").compare(challenge) !== 0) return "stale_challenge";
    challenge = randomBytes(16);
    delete secrets.challenge;
    Object.assign(body, secrets);
    delete body.sealed;
    return null;
  } catch {
    return "sealed_invalid";
  }
}

// ---------- Réponses ----------

function status() {
  if (audioPlaying()) runtime.activePositionMs = Date.now() - runtime.playingSince;
  const active = bindings.find((item) => item.uid === runtime.activeCardUid);
  const used = runtime.sdState === "ready" ? sdUsed() : 0;
  const total = runtime.sdState === "ready" ? SD_TOTAL : 0;
  return {
    state: runtime.state,
    firmwareVersion,
    firmwareBuild: "simulée",
    uiVersion: "", // page servie depuis les sources: pas de version à comparer
    updateRepo: "thomas-lamure/totobox",
    update: updateStatus(),
    setupDone: config.setupDone,
    boxKey: Buffer.from(boxKeys.publicKey).toString("base64"),
    challenge: challenge.toString("base64"),
    sdReady: runtime.sdState === "ready",
    sdState: runtime.sdState,
    nfcReady: true,
    wifiConnected: config.wifiSsid !== "",
    wifiMode: config.wifiSsid ? "sta" : "ap",
    wifiIp: config.wifiSsid ? "127.0.0.1" : "192.168.4.1",
    hostname: `${config.hostname}.local`,
    wifiSsid: config.wifiSsid,
    networkAttempt: runtime.networkAttempt,
    activeCardUid: runtime.activeCardUid,
    activeMediaPath: active ? active.mediaPath : "",
    activeTrackIndex: runtime.activeTrackIndex,
    activePositionMs: runtime.activePositionMs,
    activeTrackCount: runtime.activeTrackCount,
    audioPlaying: audioPlaying(),
    lockEnabled: config.lockEnabled,
    locked: isLocked(),
    lastUnknownCardUid: isLocked() ? "" : runtime.lastUnknownCardUid,
    ...(active ? { activeCardLabel: active.label } : {}),
    currentVolume: config.currentVolume,
    volumeMin: config.volumeMin,
    volumeMax: config.volumeMax,
    bindingsCount: bindings.length,
    sdTotalBytes: total,
    sdUsedBytes: used,
    sdFreeBytes: total - used,
  };
}

function configJson() {
  const { pin, setupDone, ...visible } = config;
  return { ...visible, lockCards: config.lockCards.map(({ uid, label }) => ({ uid, label })), bindings };
}

const lockConfig = () => ({ ok: true, lockEnabled: config.lockEnabled, lockCards: config.lockCards });

function validateMasterCard(uid) {
  if (!uid) return "invalid_card";
  if (bindings.some((item) => item.uid === uid)) return "card_in_use";
  if (config.lockCards.some((card) => card.uid === uid)) return "card_already_master";
  return null;
}

function forgetUnknownCard(uid) {
  if (runtime.lastUnknownCardUid === uid) runtime.lastUnknownCardUid = "";
  if (runtime.activeCardUid === uid && runtime.state === "unknown_card") runtime.state = "ready";
}

// Routes: [méthode, chemin, verrouillée interdite, refusée pendant la lecture, gestionnaire(body, request)]
const routes = [
  ["GET", "/api/status", false, false, () => [200, status()]],
  ["GET", "/api/config", true, false, () => [200, configJson()]],
  ["POST", "/api/config", true, false, (body) => {
    for (const key of ["wifiSsid", "apSsid"]) if (typeof body[key] === "string") config[key] = body[key].trim();
    if (typeof body.hostname === "string") config.hostname = body.hostname.trim().toLowerCase() || "totobox";
    if (Number.isInteger(body.volumeMin)) config.volumeMin = Math.max(0, Math.min(100, body.volumeMin));
    if (Number.isInteger(body.volumeMax)) config.volumeMax = Math.max(config.volumeMin, Math.min(100, body.volumeMax));
    config.currentVolume = Math.max(config.volumeMin, Math.min(config.volumeMax, body.currentVolume ?? config.currentVolume));
    if (body.wifiSsid !== undefined || body.wifiPassword !== undefined) {
      const id = runtime.networkAttempt.id + 1;
      runtime.networkAttempt = config.wifiSsid
        ? { id, state: "connected", ssid: config.wifiSsid, ip: "127.0.0.1", error: "", apClosesInSec: 60 }
        : { id, state: "", ssid: "", ip: "", error: "", apClosesInSec: 0 };
    }
    return [200, configJson()];
  }],
  ["POST", "/api/volume", false, false, (body) => {
    if (!Number.isInteger(body.volume)) return [400, { ok: false, error: "invalid_volume" }];
    config.currentVolume = Math.max(config.volumeMin, Math.min(config.volumeMax, body.volume));
    return [200, { ok: true, currentVolume: config.currentVolume, volumeMin: config.volumeMin, volumeMax: config.volumeMax }];
  }],
  ["POST", "/api/unlock", false, false, (body) => {
    if (!isLocked()) return [200, { ok: true }];
    const retryInSec = Math.ceil(Math.max(0, runtime.pinBlockedUntil - Date.now()) / 1000);
    if (retryInSec > 0) return [429, { ok: false, error: "too_many_attempts", retryInSec }];
    if (body.pin !== config.pin) {
      runtime.pinFailures += 1;
      const attemptsLeft = Math.max(0, PIN_ATTEMPTS - runtime.pinFailures);
      if (attemptsLeft === 0) {
        runtime.pinBlockedUntil = Date.now() + PIN_LOCKOUT_MS;
        runtime.pinFailures = 0;
      }
      const retry = Math.ceil(Math.max(0, runtime.pinBlockedUntil - Date.now()) / 1000);
      return [attemptsLeft === 0 ? 429 : 403, { ok: false, error: attemptsLeft === 0 ? "too_many_attempts" : "wrong_pin", attemptsLeft, retryInSec: retry }];
    }
    runtime.pinFailures = 0;
    runtime.unlocked = true;
    return [200, { ok: true }];
  }],
  ["POST", "/api/lock", false, false, () => {
    runtime.unlocked = false;
    return [200, { ok: true, locked: isLocked() }];
  }],
  ["POST", "/api/lock/setup", true, false, (body) => {
    if (!body.enabled) {
      Object.assign(config, { lockEnabled: false, lockCards: [], pin: "" });
      return [200, lockConfig()];
    }
    const uid = String(body.cardUid || "").trim().toUpperCase();
    const error = /^\d{6}$/.test(body.pin || "") ? validateMasterCard(uid) : "invalid_pin";
    if (error) return [400, { ok: false, error }];
    Object.assign(config, { lockEnabled: true, lockCards: [{ uid, label: String(body.cardLabel || "").trim() }], pin: body.pin });
    runtime.unlocked = true;
    forgetUnknownCard(uid);
    return [200, lockConfig()];
  }],
  ["POST", "/api/lock/pin", true, false, (body) => {
    if (!config.lockEnabled) return [400, { ok: false, error: "lock_disabled" }];
    if (!/^\d{6}$/.test(body.pin || "")) return [400, { ok: false, error: "invalid_pin" }];
    config.pin = body.pin;
    return [200, lockConfig()];
  }],
  ["POST", "/api/lock/cards", true, false, (body) => {
    if (!config.lockEnabled) return [400, { ok: false, error: "lock_disabled" }];
    const uid = String(body.cardUid || "").trim().toUpperCase();
    if (body.action === "add") {
      const error = validateMasterCard(uid) || (config.lockCards.length >= 8 ? "too_many_cards" : null);
      if (error) return [400, { ok: false, error }];
      config.lockCards.push({ uid, label: String(body.cardLabel || "").trim() });
      forgetUnknownCard(uid);
    } else if (body.action === "remove") {
      const index = config.lockCards.findIndex((card) => card.uid === uid);
      if (index < 0 || config.lockCards.length <= 1) {
        return [400, { ok: false, error: config.lockCards.length <= 1 ? "last_card" : "invalid_card" }];
      }
      config.lockCards.splice(index, 1);
    } else {
      return [400, { ok: false, error: "invalid_action" }];
    }
    return [200, lockConfig()];
  }],
  ["POST", "/api/bindings", true, false, (body) => {
    if (!Array.isArray(body)) return [400, { error: "invalid_bindings" }];
    if (body.some((item) => config.lockCards.some((card) => card.uid === item.uid))) return [400, { ok: false, error: "lock_card" }];
    bindings = body.filter((item) => item.uid).map((item) => ({
      uid: item.uid, label: item.label || "", mediaPath: item.mediaPath || "", mode: item.mode || "sd_local",
      resumeTrackIndex: item.resumeTrackIndex || 0, resumeMs: item.resumeMs || 0, resumeByte: item.resumeByte || 0,
    }));
    if (bindings.some((item) => item.uid === runtime.lastUnknownCardUid)) runtime.lastUnknownCardUid = "";
    return [200, configJson()];
  }],
  ["GET", "/api/library", true, true, () => {
    const ready = runtime.sdState === "ready";
    const base = { sdReady: ready, rootPath: LIBRARY_ROOT, trashPath: TRASH_ROOT, sort: "type_then_name" };
    if (!ready) return [503, { ...base, entries: [], rootAvailable: false }];
    const { entries, summary } = tree(LIBRARY_ROOT, false);
    const used = sdUsed();
    return [200, { ...base, entries, rootAvailable: true, ...summary, sdTotalBytes: SD_TOTAL, sdUsedBytes: used, sdFreeBytes: SD_TOTAL - used }];
  }],
  ["POST", "/api/folder", true, true, (body) => {
    const parent = body.parentPath || LIBRARY_ROOT;
    const name = String(body.folderName || "").trim().replace(/[\\/:*?"<>|]/g, "");
    if (!name || !files.has(parent)) return [400, { ok: false, error: "invalid_folder_request" }];
    const path = freePath(parent, name);
    files.set(path, { type: "directory", size: 0 });
    return [200, { ok: true, path }];
  }],
  ["POST", "/api/delete", true, true, (body) => {
    const path = body.path;
    if (!files.has(path) || path === LIBRARY_ROOT) return [400, { ok: false, error: "not_found" }];
    const relative = path.slice(LIBRARY_ROOT.length);
    const trashParent = TRASH_ROOT + parentOf(relative).replace(/^\/$/, "");
    ensureDir(trashParent);
    const trashedPath = freePath(trashParent, baseName(path));
    movePrefix(path, trashedPath);
    return [200, { ok: true, path, trashedPath }];
  }],
  ["GET", "/api/trash", true, true, () => {
    const { entries, summary } = tree(TRASH_ROOT, true);
    return [200, { sdReady: true, trashPath: TRASH_ROOT, entries, trashAvailable: true, ...summary }];
  }],
  ["POST", "/api/restore", true, true, (body) => {
    const path = body.path || "";
    if (!path.startsWith(TRASH_ROOT + "/") || !files.has(path)) return [400, { ok: false, error: "invalid_trash_path" }];
    const original = LIBRARY_ROOT + path.slice(TRASH_ROOT.length);
    const parent = parentOf(original);
    ensureDir(parent);
    const restoredPath = freePath(parent, baseName(original));
    movePrefix(path, restoredPath);
    return [200, { ok: true, path, restoredPath }];
  }],
  ["POST", "/api/trash/purge", true, true, (body) => {
    const path = body.path || TRASH_ROOT;
    if (!(path === TRASH_ROOT || path.startsWith(TRASH_ROOT + "/"))) return [400, { ok: false, error: "invalid_trash_path" }];
    for (const key of [...files.keys()]) if (key.startsWith(path + "/") || (key === path && path !== TRASH_ROOT)) files.delete(key);
    return [200, { ok: true, path }];
  }],
  ["POST", "/api/rename", true, true, (body) => {
    const path = body.path;
    const name = String(body.name || "").trim();
    if (!files.has(path) || !name || name.includes("/") || [".", "..", ".trash", "trash", "content"].includes(name.toLowerCase())) {
      return [400, { ok: false, error: "rename_failed", path }];
    }
    const renamedPath = freePath(parentOf(path), name);
    movePrefix(path, renamedPath);
    return [200, { ok: true, path, renamedPath }];
  }],
  ["POST", "/api/move", true, true, (body) => {
    const { path, targetDir } = body;
    if (!files.has(path) || files.get(targetDir)?.type !== "directory") return [400, { ok: false, error: "not_found", path }];
    if (targetDir === path || targetDir.startsWith(path + "/")) return [400, { ok: false, error: "invalid_move_target", path }];
    if (parentOf(path) === targetDir) return [200, { ok: true, path, movedPath: path }];
    const movedPath = freePath(targetDir, baseName(path));
    movePrefix(path, movedPath);
    return [200, { ok: true, path, movedPath }];
  }],
  ["POST", "/api/factory-reset", true, true, (body) => {
    if (body.confirm !== "EFFACER") return [400, { ok: false, error: "confirm_required" }];
    const backupSaved = bindings.length > 0;
    if (backupSaved) backup = { savedAt: body.savedAt || "", bindings: structuredClone(bindings) };
    const count = bindings.length;
    bindings = [];
    Object.assign(config, { wifiSsid: "", hostname: "totobox", lockEnabled: false, lockCards: [], pin: "", setupDone: false, currentVolume: 30, volumeMin: 10, volumeMax: 70 });
    runtime.unlocked = false;
    return [200, { ok: true, apSsid: config.apSsid, factoryPassword: true, backupSaved, backupCount: backupSaved ? count : 0 }];
  }],
  ["POST", "/api/sd/format", true, false, (body) => {
    if (body.confirm !== "FORMATER") return [400, { ok: false, error: "confirm_required" }];
    if (runtime.sdState !== "unreadable") return [409, { ok: false, error: "sd_not_unreadable" }];
    files.clear();
    files.set(LIBRARY_ROOT, { type: "directory", size: 0 });
    files.set(TRASH_ROOT, { type: "directory", size: 0 });
    setSdState("ready");
    return [200, { ok: true }];
  }],
  ["POST", "/api/update", true, true, (body) => {
    const parse = (v) => (/^(\d+)\.(\d+)\.(\d+)$/.exec(v || "") || []).slice(1).map(Number);
    const [next, now] = [parse(body.version), parse(firmwareVersion)];
    if (next.length !== 3) return [409, { ok: false, error: "invalid_version" }];
    const newer = next[0] - now[0] || next[1] - now[1] || next[2] - now[2];
    if (newer <= 0) return [409, { ok: false, error: "not_newer" }];
    if (!config.wifiSsid) return [409, { ok: false, error: "no_internet" }];
    if (["page", "firmware", "restarting"].includes(update.phase)) return [409, { ok: false, error: "update_running" }];
    Object.assign(update, { phase: "page", version: body.version, startedAt: Date.now(), page: body.page?.size || 47000, firmware: body.firmware?.size || 1500000, error: "" });
    return [200, { ok: true }];
  }],
  ["POST", "/api/setup/complete", true, false, () => {
    config.setupDone = true;
    return [200, { ok: true }];
  }],
  ["GET", "/api/cards/backup", true, true, () => [200, { ok: true, exists: !!backup, count: backup ? backup.bindings.length : 0, savedAt: backup ? backup.savedAt : "" }]],
  ["POST", "/api/cards/backup", true, true, (body) => {
    if (bindings.length === 0) return [400, { ok: false, error: "nothing_to_save" }];
    backup = { savedAt: body.savedAt || "", bindings: structuredClone(bindings) };
    return [200, { ok: true, exists: true, count: bindings.length, savedAt: backup.savedAt }];
  }],
  ["POST", "/api/cards/restore", true, true, () => {
    if (!backup) return [404, { ok: false, error: "no_backup" }];
    const kept = backup.bindings.filter((item) => !config.lockCards.some((card) => card.uid === item.uid));
    bindings = structuredClone(kept);
    return [200, { ok: true, count: kept.length, skipped: backup.bindings.length - kept.length }];
  }],
];

// Envoi de fichiers (multipart): champs targetPath, conflictMode et files.
async function handleUpload(request, raw) {
  if (runtime.sdState !== "ready") return [503, { ok: false, error: "sd_unavailable" }];
  if (isLocked()) return [400, { ok: false, error: "locked" }];
  if (audioPlaying()) return [400, { ok: false, error: "busy_playing" }];
  const form = await new Request("http://localhost/", { method: "POST", headers: { "content-type": request.headers["content-type"] }, body: raw }).formData();
  const target = form.get("targetPath") || LIBRARY_ROOT;
  const conflictMode = form.get("conflictMode") || "rename";
  ensureDir(target);
  const uploadedPaths = [];
  let bytesWritten = 0;
  for (const file of form.getAll("files")) {
    const name = file.name.normalize("NFD").replace(/[̀-ͯ]/g, "").replace(/[\\/:*?"<>|]/g, "_");
    if (!AUDIO.test(name)) return [400, { ok: false, error: "invalid_file_name" }];
    const path = conflictMode === "replace" ? `${target}/${name}` : freePath(target, name);
    addFile(path, file.size);
    uploadedPaths.push(path);
    bytesWritten += file.size;
  }
  return [200, {
    ok: true, targetPath: target, conflictMode, lastResolvedPath: uploadedPaths.at(-1) || "", bytesWritten,
    metadataBytesStripped: 0, filesUploaded: uploadedPaths.length, filesSkipped: 0, uploadedPaths,
  }];
}

// ---------- Page de simulation ----------

function mockPage() {
  const cards = bindings.map((item) => `<button data-card="${item.uid}">Poser « ${item.label} »</button>`).join(" ");
  const masters = config.lockCards.map((card) => `<button data-card="${card.uid}">Poser la carte maîtresse « ${card.label} »</button>`).join(" ");
  return `<!DOCTYPE html><html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>TotoBox simulée</title>
<style>body{font:16px system-ui;margin:24px;max-width:720px}button{margin:4px 0;padding:8px 12px}section{margin:20px 0}</style>
<h1>TotoBox simulée</h1>
<p>État: <b>${runtime.state}</b>, carte SD: <b>${runtime.sdState}</b>, carte posée: <b>${runtime.activeCardUid || "aucune"}</b>.
<a href="/" target="totobox">Ouvrir la page de la boîte</a></p>
<section><h2>Cartes NFC</h2>${cards} ${masters}
<p><button data-card="new">Poser une carte inconnue</button> <button data-action="remove">Retirer la carte</button></p></section>
<section><h2>Carte SD</h2><button data-sd="ready">Prête</button> <button data-sd="unreadable">Illisible (exFAT)</button> <button data-sd="absent">Absente</button></section>
<section><h2>Boîte</h2><button data-action="reset-library">Bibliothèque d'exemple</button></section>
<script>
document.addEventListener("click", async (event) => {
  const b = event.target.closest("button"); if (!b) return;
  let body = b.dataset.card ? { action: "place", uid: b.dataset.card } : b.dataset.sd ? { action: "sd", state: b.dataset.sd } : { action: b.dataset.action };
  await fetch("/mock", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  location.reload();
});
</script></html>`;
}

function handleMock(body) {
  if (body.action === "place") placeCard(body.uid === "new" ? randomBytes(4).toString("hex").toUpperCase() : body.uid);
  else if (body.action === "remove") removeCard();
  else if (body.action === "sd") setSdState(body.state);
  else if (body.action === "reset-library") seedLibrary();
}

// ---------- Serveur ----------

const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml" };

function send(response, code, data) {
  response.writeHead(code, { "Content-Type": "application/json", "Cache-Control": "no-store" });
  response.end(JSON.stringify(data));
}

async function readBody(request) {
  const chunks = [];
  for await (const chunk of request) chunks.push(chunk);
  return Buffer.concat(chunks);
}

createServer(async (request, response) => {
  // Redémarrage après une mise à jour: la boîte ne répond plus pendant quelques secondes.
  if (Date.now() < rebootingUntil) return request.socket.destroy();
  const url = new URL(request.url, "http://localhost");
  const raw = await readBody(request);
  try {
    if (url.pathname === "/mock") {
      if (request.method === "POST") {
        handleMock(JSON.parse(raw.toString() || "{}"));
        return send(response, 200, { ok: true });
      }
      response.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return response.end(mockPage());
    }
    if (url.pathname === "/api/upload" && request.method === "POST") {
      const [code, data] = await handleUpload(request, raw);
      return send(response, code, data);
    }
    if (url.pathname.startsWith("/api/")) {
      const route = routes.find(([method, path]) => method === request.method && path === url.pathname);
      if (!route) return send(response, 404, { ok: false, error: "not_found" });
      const [method, , lockable, sdRoute, handler] = route;
      if (method === "POST" && raw.length > 0 && !String(request.headers["content-type"] || "").startsWith("application/json")) {
        return send(response, 415, { ok: false, error: "json_required" });
      }
      if (lockable && isLocked()) return send(response, 403, { ok: false, error: "locked" });
      if (sdRoute && audioPlaying()) return send(response, 503, { ok: false, error: "busy_playing" });
      const body = raw.length ? JSON.parse(raw.toString()) : {};
      const sealError = unseal(body);
      if (sealError) return send(response, sealError === "stale_challenge" ? 409 : 400, { ok: false, error: sealError });
      const [code, data] = handler(body, request);
      return send(response, code, data);
    }
    // Fichiers statiques de web/
    const relative = url.pathname === "/" ? "index.html" : decodeURIComponent(url.pathname.slice(1));
    const path = normalize(join(WEB, relative));
    if (!path.startsWith(WEB + sep)) return send(response, 403, { ok: false });
    const content = await readFile(path);
    response.writeHead(200, { "Content-Type": TYPES[extname(path)] || "application/octet-stream", "Cache-Control": "no-store" });
    response.end(content);
  } catch (error) {
    if (error.code === "ENOENT") return send(response, 404, { ok: false, error: "not_found" });
    console.error(error);
    send(response, 500, { ok: false, error: String(error.message || error) });
  }
}).listen(PORT, "127.0.0.1", () => {
  console.log(`TotoBox simulée: page http://localhost:${PORT}/, simulation http://localhost:${PORT}/mock`);
});
