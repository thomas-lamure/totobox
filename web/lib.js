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

// Fichiers d'un dossier de la bibliothèque à recopier sur l'ordinateur: chemin sur la carte, dossiers à créer
// (en commençant par celui du dossier téléchargé, "totobox" pour la racine) et nom. Même arborescence que
// l'archive ZIP de la boîte (zip_stream::entryName).
function folderDownloadPlan(entries, folderPath) {
  const find = (list) => {
    for (const entry of list || []) {
      if (entry.path === folderPath) return entry;
      if (entry.type === "directory" && folderPath.startsWith(entry.path + "/")) return find(entry.children);
    }
    return null;
  };
  const root = folderPath === LIBRARY_ROOT ? { children: entries } : find(entries);
  const label = folderPath === LIBRARY_ROOT ? "totobox" : baseName(folderPath);
  const plan = [];
  const walk = (list) => {
    for (const entry of list || []) {
      if (entry.type === "directory") {
        if (!isInTrashPath(entry.path)) walk(entry.children);
      } else if (isAudioPath(entry.path)) {
        const parts = [label, ...entry.path.slice(folderPath.length + 1).split("/")];
        plan.push({ path: entry.path, dirs: parts.slice(0, -1), name: parts.at(-1), size: entry.totalSize || 0 });
      }
    }
  };
  if (root) walk(root.children);
  return plan;
}

function isInTrashPath(path) {
  return path === TRASH_ROOT || path.startsWith(TRASH_ROOT + "/");
}

// Emplacement d'origine d'un élément de la corbeille (même arborescence sous /content/.trash).
function originalPath(trashPath) {
  return LIBRARY_ROOT + trashPath.slice(TRASH_ROOT.length);
}

// Éléments supprimés, à lister et restaurer un par un. La corbeille reprend l'arborescence de la bibliothèque:
// un dossier de la corbeille dont l'original existe encore n'est là que pour ranger ce qui y a été supprimé
// (on liste son contenu); sinon, c'est un dossier supprimé avec tout ce qu'il contenait.
function trashItems(trashEntries, libraryEntries) {
  const folders = new Set(allFolders(libraryEntries));
  const items = [];
  const hasFile = (entry) => entry.type !== "directory" || (entry.children || []).some(hasFile);
  const walk = (entries) => {
    for (const entry of entries || []) {
      // Dossier sans aucun fichier (reste d'une ancienne restauration): rien à restaurer, vidé avec la corbeille.
      if (!hasFile(entry)) continue;
      if (entry.type === "directory" && folders.has(originalPath(entry.path))) {
        walk(entry.children);
      } else {
        items.push(entry);
      }
    }
  };
  walk(trashEntries);
  return items;
}

// ---------- Apparence des cartes ----------

// Palette proposée (classes tone-*), la même que ConfigStore::cardColor. Vide = bleu.
const CARD_COLORS = [
  ["blue", "Bleu"], ["green", "Vert"], ["amber", "Orange"], ["red", "Rouge"],
  ["purple", "Violet"], ["pink", "Rose"], ["teal", "Turquoise"],
];

// Emoji ou court texte de la tuile, vérifié comme ConfigStore::cardEmoji: 16 octets UTF-8 au plus, sans caractère
// de contrôle ni « < > " & ». Rend "" sinon.
function cardEmoji(value) {
  const text = String(value || "").trim();
  if (new TextEncoder().encode(text).length > 16) return "";
  return /[\u0000-\u001f\u007f<>"&]/.test(text) ? "" : text;
}

// Recherche dans les cartes (nom, contenu), sans tenir compte des accents ni de la casse.
function filterCards(list, query) {
  const fold = (text) => String(text || "").normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase();
  const wanted = fold(query).trim();
  if (!wanted) return list;
  return list.filter((binding) => fold(`${binding.label} ${prettyName(binding.mediaPath)} ${binding.mediaPath}`).includes(wanted));
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

// ---------- Égalisation du volume des MP3 ----------
// Comme mp3gain: le champ global_gain de chaque granule (8 bits, pas de 1,5 dB) est décalé, sans réencoder ni
// perdre de qualité. La page mesure le niveau perçu (BS.1770, en LUFS) sur des extraits décodés par le
// navigateur, puis choisit le nombre de pas.

const MP3_GAIN_STEP_DB = 1.5;
const MP3_TARGET_LUFS = -16;
// Fichier laissé tel quel à moins d'un pas de la cible.
const MP3_TOLERANCE_DB = 1.5;
// Montée limitée pour ne pas écrêter plus de 0,01 % des échantillons (inaudible sur la boîte).
const MP3_MAX_CLIP_RATIO = 1e-4;
// 12 dB au plus: au-delà, c'est surtout le souffle d'un enregistrement très faible qui monterait.
const MP3_MAX_STEPS = 8;

const MP3_BITRATES = [
  [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320], // MPEG1 couche III
  [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160], // MPEG2 et 2.5 couche III
];
const MP3_RATES = { 3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000] };

// En-tête de trame MP3 (couche III) à l'octet i, ou null.
function mp3FrameHeader(bytes, i) {
  if (i + 4 > bytes.length || bytes[i] !== 0xff || (bytes[i + 1] & 0xe0) !== 0xe0) return null;
  const version = (bytes[i + 1] >> 3) & 3; // 3 MPEG1, 2 MPEG2, 0 MPEG2.5
  const layer = (bytes[i + 1] >> 1) & 3; // 1 = couche III
  const bitrateIndex = bytes[i + 2] >> 4;
  const rateIndex = (bytes[i + 2] >> 2) & 3;
  if (version === 1 || layer !== 1 || bitrateIndex === 0 || bitrateIndex === 15 || rateIndex === 3) return null;
  const mpeg1 = version === 3;
  const bitrate = MP3_BITRATES[mpeg1 ? 0 : 1][bitrateIndex] * 1000;
  const rate = MP3_RATES[version][rateIndex];
  const padding = (bytes[i + 2] >> 1) & 1;
  const mono = bytes[i + 3] >> 6 === 3;
  return {
    mpeg1,
    version,
    rate,
    mono,
    crc: !(bytes[i + 1] & 1),
    length: Math.floor(((mpeg1 ? 144 : 72) * bitrate) / rate) + padding,
    sideLength: mpeg1 ? (mono ? 17 : 32) : mono ? 9 : 17,
  };
}

// Une trame n'est retenue que si la suivante commence juste après (ou si c'est la dernière): une suite
// d'octets 0xFFE… au milieu d'une pochette ou d'un tag n'est pas prise pour une trame.
function mp3FrameAt(bytes, i, end) {
  const header = mp3FrameHeader(bytes, i);
  if (!header || i + header.length > end) return null;
  const next = i + header.length;
  if (next === end || end - next < 4) return header;
  const following = mp3FrameHeader(bytes, next);
  return following && following.version === header.version && following.rate === header.rate ? header : null;
}

// Premier octet d'une trame MP3 dans un extrait, ou -1.
function firstMp3Frame(bytes) {
  for (let i = 0; i + 4 <= bytes.length; i++) {
    if (mp3FrameAt(bytes, i, bytes.length)) return i;
  }
  return -1;
}

function readBits(bytes, pos, count) {
  let value = 0;
  for (let k = 0; k < count; k++) {
    value = (value << 1) | ((bytes[(pos + k) >> 3] >> (7 - ((pos + k) & 7))) & 1);
  }
  return value;
}

function writeBits(bytes, pos, count, value) {
  for (let k = 0; k < count; k++) {
    const bit = (value >> (count - 1 - k)) & 1;
    const index = (pos + k) >> 3;
    const shift = 7 - ((pos + k) & 7);
    bytes[index] = (bytes[index] & ~(1 << shift)) | (bit << shift);
  }
}

// CRC-16 des trames protégées (polynôme 0x8005): octets 2 et 3 de l'en-tête, puis informations annexes.
function mp3Crc(bytes, frame, sideLength) {
  let crc = 0xffff;
  const feed = (byte) => {
    for (let bit = 7; bit >= 0; bit--) {
      const top = ((crc >> 15) & 1) ^ ((byte >> bit) & 1);
      crc = (crc << 1) & 0xffff;
      if (top) crc ^= 0x8005;
    }
  };
  feed(bytes[frame + 2]);
  feed(bytes[frame + 3]);
  for (let k = 0; k < sideLength; k++) feed(bytes[frame + 6 + k]);
  return crc;
}

// Décale de `steps` pas le global_gain de chaque granule, en place. Tags ID3 et trame d'informations
// (Xing, Info, VBRI) laissés intacts. Rend le nombre de trames modifiées et de gains bornés à 0 ou 255.
function applyMp3Gain(bytes, steps) {
  let i = 0;
  while (i + 10 <= bytes.length && bytes[i] === 0x49 && bytes[i + 1] === 0x44 && bytes[i + 2] === 0x33) {
    const size = (bytes[i + 6] << 21) | (bytes[i + 7] << 14) | (bytes[i + 8] << 7) | bytes[i + 9];
    i += 10 + size + (bytes[i + 5] & 0x10 ? 10 : 0);
  }
  let end = bytes.length;
  if (end - i >= 128 && bytes[end - 128] === 0x54 && bytes[end - 127] === 0x41 && bytes[end - 126] === 0x47) end -= 128;

  let frames = 0;
  let clamped = 0;
  let first = true;
  while (i + 4 <= end) {
    const header = mp3FrameAt(bytes, i, end);
    if (!header) {
      i++;
      continue;
    }
    const side = i + 4 + (header.crc ? 2 : 0);
    if (first) {
      first = false;
      const tag = String.fromCharCode(...bytes.subarray(side + header.sideLength, side + header.sideLength + 4));
      if (tag === "Xing" || tag === "Info" || tag === "VBRI") {
        i += header.length;
        continue;
      }
    }
    const channels = header.mono ? 1 : 2;
    // Début du premier granule: main_data_begin, bits privés, puis scfsi (MPEG1 seulement).
    let pos = side * 8 + (header.mpeg1 ? 9 + (header.mono ? 5 : 3) + 4 * channels : 8 + (header.mono ? 1 : 2));
    const granuleBits = header.mpeg1 ? 59 : 63;
    for (let g = 0; g < (header.mpeg1 ? 2 : 1) * channels; g++) {
      // part2_3_length (12 bits), big_values (9), puis global_gain (8).
      const gainPos = pos + 21;
      const target = readBits(bytes, gainPos, 8) + steps;
      const gain = Math.max(0, Math.min(255, target));
      if (gain !== target) clamped++;
      writeBits(bytes, gainPos, 8, gain);
      pos += granuleBits;
    }
    if (header.crc) {
      const crc = mp3Crc(bytes, i, header.sideLength);
      bytes[i + 4] = crc >> 8;
      bytes[i + 5] = crc & 0xff;
    }
    frames++;
    i += header.length;
  }
  return { frames, clamped };
}

// Filtre de pondération K (BS.1770): plateau aigu puis passe-haut, recalculés pour la fréquence
// d'échantillonnage par la transformée bilinéaire (même calcul que libebur128, utilisé par ffmpeg).
// Rend deux biquads [b0, b1, b2, a1, a2].
function kWeighting(rate) {
  let k = Math.tan((Math.PI * 1681.974450955533) / rate);
  let q = 0.7071752369554196;
  const vh = Math.pow(10, 3.999843853973347 / 20);
  const vb = Math.pow(vh, 0.4996667741545416);
  let a0 = 1 + k / q + k * k;
  const shelf = [
    (vh + (vb * k) / q + k * k) / a0,
    (2 * (k * k - vh)) / a0,
    (vh - (vb * k) / q + k * k) / a0,
    (2 * (k * k - 1)) / a0,
    (1 - k / q + k * k) / a0,
  ];
  k = Math.tan((Math.PI * 38.13547087602444) / rate);
  q = 0.5003270373238773;
  a0 = 1 + k / q + k * k;
  const highPass = [1, -2, 1, (2 * (k * k - 1)) / a0, (1 - k / q + k * k) / a0];
  return [shelf, highPass];
}

// Mesures d'un extrait décodé (un Float32Array par canal): énergie des blocs de 400 ms (pas de 100 ms) après
// pondération K, nombre d'échantillons, et combien dépasseraient 0 dBFS pour chaque montée de 1 à MP3_MAX_STEPS pas.
// Les `skip` premiers échantillons sont ignorés (début d'extrait mal décodé, faute de trames précédentes).
function measureLevels(channels, rate, skip = 0) {
  const length = channels.length ? channels[0].length : 0;
  const hop = Math.round(rate / 10);
  const hops = Math.max(0, Math.floor((length - skip) / hop));
  const hopEnergy = new Float64Array(hops);
  const thresholds = [];
  for (let k = 1; k <= MP3_MAX_STEPS; k++) thresholds.push(Math.pow(10, (-MP3_GAIN_STEP_DB * k) / 20));
  const over = new Array(MP3_MAX_STEPS).fill(0);
  const [[b0, b1, b2, a1, a2], [c0, c1, c2, d1, d2]] = kWeighting(rate);
  let samples = 0;

  for (const data of channels) {
    // Deux biquads en série, variables locales: la boucle tourne sur des millions d'échantillons.
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
    for (let n = 0; n < skip + hops * hop; n++) {
      const x = data[n];
      const y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      const z = c0 * y + c1 * y1 + c2 * y2 - d1 * z1 - d2 * z2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      z2 = z1;
      z1 = z;
      if (n < skip) continue;
      hopEnergy[Math.floor((n - skip) / hop)] += z * z;
      const peak = x < 0 ? -x : x;
      samples++;
      // Seuils décroissants: un pic qui dépasse le seuil de k pas dépasse aussi ceux des montées plus fortes.
      for (let k = MP3_MAX_STEPS - 1; k >= 0 && peak > thresholds[k]; k--) over[k]++;
    }
  }

  const blocks = [];
  for (let b = 0; b + 4 <= hops; b++) {
    blocks.push((hopEnergy[b] + hopEnergy[b + 1] + hopEnergy[b + 2] + hopEnergy[b + 3]) / (4 * hop));
  }
  return { blocks, samples, over };
}

function mergeLevels(list) {
  const merged = { blocks: [], samples: 0, over: new Array(MP3_MAX_STEPS).fill(0) };
  for (const levels of list) {
    merged.blocks.push(...levels.blocks);
    merged.samples += levels.samples;
    levels.over.forEach((count, k) => (merged.over[k] += count));
  }
  return merged;
}

// Niveau intégré en LUFS (seuils absolu de −70 et relatif de −10), -Infinity pour un silence.
function gatedLoudness(blocks) {
  const lufs = (energy) => -0.691 + 10 * Math.log10(energy);
  const mean = (list) => list.reduce((sum, value) => sum + value, 0) / list.length;
  const audible = blocks.filter((energy) => energy > 0 && lufs(energy) > -70);
  if (!audible.length) return -Infinity;
  const relative = lufs(mean(audible)) - 10;
  const kept = audible.filter((energy) => lufs(energy) > relative);
  return lufs(mean(kept));
}

// Pas de global_gain à appliquer (positif: plus fort) et niveau mesuré.
function mp3GainSteps(levels) {
  const lufs = gatedLoudness(levels.blocks);
  if (!Number.isFinite(lufs) || !levels.samples) return { steps: 0, lufs };
  const missing = MP3_TARGET_LUFS - lufs;
  if (Math.abs(missing) <= MP3_TOLERANCE_DB) return { steps: 0, lufs };
  let steps = Math.max(-MP3_MAX_STEPS, Math.min(MP3_MAX_STEPS, Math.round(missing / MP3_GAIN_STEP_DB)));
  while (steps > 0 && levels.over[steps - 1] / levels.samples > MP3_MAX_CLIP_RATIO) steps--;
  return { steps, lufs };
}

// 4 -> "+6 dB", 3 -> "+4,5 dB", -2 -> "−3 dB".
function fmtGain(steps) {
  const db = steps * MP3_GAIN_STEP_DB;
  return `${db > 0 ? "+" : "−"}${String(Math.abs(db)).replace(".", ",")} dB`;
}

// ---------- Ordre des pistes ----------
// La boîte lit les pistes d'un dossier dans l'ordre des noms, sans tenir compte de la casse (strcasecmp,
// playback_rules::sortPlaylist): pour imposer un ordre, la page renomme les fichiers avec un numéro devant.

// Même comparaison que strcasecmp côté boîte (minuscules ASCII seulement, octets UTF-8 ensuite).
function compareTrackNames(a, b) {
  const bytes = (value) => new TextEncoder().encode(String(value).replace(/[A-Z]/g, (c) => c.toLowerCase()));
  const left = bytes(a);
  const right = bytes(b);
  for (let i = 0; i < Math.min(left.length, right.length); i++) {
    if (left[i] !== right[i]) return left[i] - right[i];
  }
  return left.length - right.length;
}

// « 03_Le loup.mp3 » -> { digits: "03", sep: "_", rest: "Le loup", ext: ".mp3" }; null sans numéro devant.
// Un nom fait d'un seul numéro (« 07.mp3 ») a un reste vide.
function trackPrefix(name) {
  const dot = name.lastIndexOf(".");
  const stem = dot > 0 ? name.slice(0, dot) : name;
  const ext = dot > 0 ? name.slice(dot) : "";
  const match = /^(\d{1,4})(?:(\s*[-_.]\s*|\s+)(\S.*))?$/.exec(stem);
  if (!match) return null;
  return { digits: match[1], sep: match[2] || "", rest: match[3] || "", ext };
}

// Renommages pour que `names` (noms de fichiers d'un même dossier, dans l'ordre voulu) soient lus dans cet ordre.
// Le style déjà présent est repris (« 01_ », « 1 - », « 001. »…), sinon « 01-». Rend les seuls changements,
// [{ from, to }], et `swap`: vrai si un nom visé est encore porté par un autre fichier (renommer en deux temps).
function trackOrderPlan(names) {
  const parsed = names.map(trackPrefix);
  const seps = new Map();
  let width = 2;
  for (const item of parsed) {
    if (!item) continue;
    width = Math.max(width, item.digits.length);
    if (item.sep) seps.set(item.sep, (seps.get(item.sep) || 0) + 1);
  }
  const sep = [...seps.entries()].sort((a, b) => b[1] - a[1])[0]?.[0] || "-";
  width = Math.max(width, String(names.length).length);
  const renames = [];
  names.forEach((name, index) => {
    const item = parsed[index];
    const number = String(index + 1).padStart(width, "0");
    let to;
    if (!item) to = `${number}${sep}${name}`;
    else if (!item.rest) to = `${number}${item.ext}`;
    else to = `${number}${sep}${item.rest}${item.ext}`;
    if (to !== name) renames.push({ from: name, to });
  });
  // Carte FAT: les noms ne se distinguent pas par la casse.
  const taken = new Set(names.map((name) => name.toLowerCase()));
  const swap = renames.some((item) => {
    const target = item.to.toLowerCase();
    return taken.has(target) && target !== item.from.toLowerCase();
  });
  return { renames, swap };
}
