// SPDX-License-Identifier: GPL-3.0-or-later
// Fonctions pures de la page (sans DOM ni réseau), chargées avant app.js et testées sur ordinateur:
// node --test scripts/test_web_lib.mjs (make test-web). Une fonction ajoutée ici est ajoutée au test.
"use strict";

const LIBRARY_ROOT = "/content";
const TRASH_ROOT = "/content/.trash";

// Nom réseau: http://<nom>.local (mêmes règles que network_names côté boîte).
const HOSTNAME_PATTERN = /^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$/;

function esc(value) {
  return String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}

function baseName(path) {
  const parts = String(path || "").split("/").filter(Boolean);
  return parts.length ? parts[parts.length - 1] : "";
}

function relPath(path) {
  if (!path) return "";
  if (path === LIBRARY_ROOT) return "Tous les fichiers";
  const rel = path.startsWith(LIBRARY_ROOT + "/") ? path.slice(LIBRARY_ROOT.length + 1) : path.replace(/^\//, "");
  return rel.split("/").join(" / ");
}

function prettyName(path) {
  return baseName(path).replace(/\.[a-z0-9]{2,4}$/i, "").replace(/[_-]+/g, " ").trim();
}

function fmtClock(ms) {
  const total = Math.max(0, Math.floor((ms || 0) / 1000));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return h ? `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}` : `${m}:${String(s).padStart(2, "0")}`;
}

function fmtDuration(sec) {
  const total = Math.round(sec || 0);
  if (total < 60) return `${total} s`;
  const h = Math.floor(total / 3600);
  const m = Math.round((total % 3600) / 60);
  return h ? `${h} h ${String(m).padStart(2, "0")}` : `${m} min`;
}

function fmtBytes(bytes) {
  const value = Number(bytes || 0);
  if (value < 1024) return `${value} o`;
  const units = ["Ko", "Mo", "Go", "To"];
  let n = value / 1024;
  let i = 0;
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024;
    i += 1;
  }
  return `${n.toLocaleString("fr-FR", { maximumFractionDigits: n < 10 ? 1 : 0 })} ${units[i]}`;
}

function plural(n, one, many) {
  return `${n} ${n > 1 ? many : one}`;
}

function isAudioPath(path) {
  return /\.(mp3|wav|flac|aac|m4a|ogg|opus)$/i.test(path || "");
}

function parentDir(path) {
  const parent = String(path || "").slice(0, String(path || "").lastIndexOf("/"));
  return parent.startsWith(LIBRARY_ROOT) ? parent : LIBRARY_ROOT;
}

// Même règle que la boîte pour les noms de dossiers, accents retirés plutôt que remplacés.
function safeFolderName(name) {
  const safe = String(name)
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .replace(/[^A-Za-z0-9_\- ]/g, "_").trim().replace(/\.+$/, "");
  return safe || "dossier";
}

// Destination valide: pas l'élément lui-même, ni un de ses sous-dossiers (pour un dossier).
function canMoveInto(path, dir) {
  return dir !== path && !dir.startsWith(path + "/");
}

function allFolders(entries, out = []) {
  for (const entry of entries || []) {
    if (entry.type !== "directory") continue;
    out.push(entry.path);
    allFolders(entry.children, out);
  }
  return out;
}

function normalizeHostname(value) {
  return value.trim().toLowerCase();
}

// « Chambre de Léo » -> chambre-de-leo (adresse http://chambre-de-leo.local).
function hostnameFromName(name) {
  return name
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+/, "")
    .slice(0, 32)
    .replace(/-+$/, "");
}

// Retire les tags ID3 (pochette, titre...) d'un MP3 avant l'envoi pour réduire la taille à transférer.
// Seuls l'en-tête et les 128 derniers octets sont lus; slice() n'en copie pas le contenu.
// La boîte refait la même vérification à la réception si le navigateur ne l'a pas fait.
async function stripId3(file) {
  if (!file.name.toLowerCase().endsWith(".mp3")) {
    return file;
  }

  let start = 0;
  let end = file.size;
  while (end - start >= 10) {
    const header = new Uint8Array(await file.slice(start, start + 10).arrayBuffer());
    const isId3v2 = header[0] === 0x49 && header[1] === 0x44 && header[2] === 0x33 &&
      header[6] < 0x80 && header[7] < 0x80 && header[8] < 0x80 && header[9] < 0x80;
    if (!isId3v2) {
      break;
    }
    const tagSize = (header[6] << 21) | (header[7] << 14) | (header[8] << 7) | header[9];
    const footerSize = (header[5] & 0x10) ? 10 : 0;
    start = Math.min(end, start + 10 + tagSize + footerSize);
  }

  if (end - start >= 128) {
    const tail = new Uint8Array(await file.slice(end - 128, end - 125).arrayBuffer());
    if (tail[0] === 0x54 && tail[1] === 0x41 && tail[2] === 0x47) {
      end -= 128;
    }
  }

  if (start === 0 && end === file.size) {
    return file;
  }
  return file.slice(start, end, file.type);
}

// Cartes associées à un fichier ou à un dossier renommé ou déplacé: leur chemin suit. Vrai si une carte a changé.
function retargetMediaPaths(list, oldPath, newPath) {
  if (!newPath || oldPath === newPath) return false;
  let changed = false;
  for (const binding of list) {
    if (binding.mediaPath === oldPath || binding.mediaPath.startsWith(oldPath + "/")) {
      binding.mediaPath = newPath + binding.mediaPath.slice(oldPath.length);
      changed = true;
    }
  }
  return changed;
}

// ---------- Mise à jour en ligne ----------

// Versions X.Y.Z: négatif si a < b, 0 si égales, positif si a > b; null si l'une n'est pas au format.
function compareVersions(a, b) {
  const parse = (value) => {
    const match = /^v?(\d+)\.(\d+)\.(\d+)$/.exec(String(value || "").trim());
    return match ? match.slice(1).map(Number) : null;
  };
  const left = parse(a);
  const right = parse(b);
  if (!left || !right) return null;
  for (let i = 0; i < 3; i++) {
    if (left[i] !== right[i]) return left[i] - right[i];
  }
  return 0;
}

const UPDATE_BLOCK = /<!--\s*totobox-update\s+(\{[\s\S]*?\})\s*-->/;

// Dernière version publiée (réponse de l'API GitHub /releases/latest) -> { version, url, notes, update }.
// `update` décrit ce que la boîte doit télécharger (POST /api/update): fichiers de la Release (adresse, taille)
// et empreintes du bloc « totobox-update » des notes; null si la Release ne permet pas la mise à jour en ligne.
function parseRelease(release) {
  const version = String((release && release.tag_name) || "").replace(/^v/, "");
  if (compareVersions(version, version) === null) return null;
  const body = String(release.body || "");
  const notes = body.replace(UPDATE_BLOCK, "").split(/^### Installer/m)[0].trim();
  let update = null;
  const match = UPDATE_BLOCK.exec(body);
  if (match) {
    try {
      const info = JSON.parse(match[1]);
      const assets = release.assets || [];
      const asset = (file) => {
        const found = file && assets.find((item) => item.name === file.name && item.size === file.size);
        return found && /^[0-9a-f]{64}$/i.test(file.sha256) ? { url: found.browser_download_url, sha256: file.sha256, size: file.size } : null;
      };
      const firmware = asset(info.firmware);
      const page = asset(info.page);
      if (info.version === version && info.uiVersion && firmware && page) {
        update = { version, uiVersion: info.uiVersion, firmware, page };
      }
    } catch (error) {
      update = null;
    }
  }
  return { version, url: release.html_url || "", notes, update };
}

// Notes de version (Markdown du CHANGELOG) en texte simple: titres sans "#", puces "•", sans `code` ni **gras**.
function plainNotes(markdown) {
  return String(markdown || "")
    .split("\n")
    .map((line) => line.replace(/^#{1,6}\s+/, "").replace(/^(\s*)[-*]\s+/, "$1• "))
    .join("\n")
    .replace(/`([^`]*)`/g, "$1")
    .replace(/\*\*([^*]+)\*\*/g, "$1")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}
