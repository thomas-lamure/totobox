// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des fonctions pures de la page (web/lib.js), sans navigateur ni dépendance:
//   node --test scripts/test_web_lib.mjs        (make test-web)
// lib.js est un script de page (fonctions globales): il est exécuté dans un contexte isolé, et ses fonctions
// relevées à la fin.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const NAMES = [
  "LIBRARY_ROOT", "HOSTNAME_PATTERN", "esc", "baseName", "relPath", "prettyName", "fmtClock", "fmtDuration",
  "fmtBytes", "plural", "isAudioPath", "parentDir", "safeFolderName", "canMoveInto", "allFolders",
  "normalizeHostname", "hostnameFromName", "stripId3", "retargetMediaPaths", "originalPath", "trashItems", "trashListing", "restoreTargets", "folderDownloadPlan", "cardEmoji", "filterCards", "CARD_COLORS", "compareVersions", "parseRelease", "plainNotes",
  "mp3FrameHeader", "firstMp3Frame", "applyMp3Gain", "mp3Crc", "readBits", "writeBits", "kWeighting", "measureLevels", "mergeLevels",
  "gatedLoudness", "mp3GainSteps", "fmtGain", "compareTrackNames", "trackPrefix", "trackOrderPlan", "dragScrollSpeed",
];
const source = readFileSync(new URL("../web/lib.js", import.meta.url), "utf8");
// TextEncoder: présent dans tous les navigateurs, absent du contexte isolé.
const lib = vm.runInNewContext(`${source}\n;({ ${NAMES.join(", ")} })`, { TextEncoder });

test("le texte est échappé avant d'entrer dans le HTML", () => {
  assert.equal(lib.esc(`<img src=x onerror="alert('x')">&`), "&lt;img src=x onerror=&quot;alert(&#39;x&#39;)&quot;&gt;&amp;");
  assert.equal(lib.esc(null), "");
  assert.equal(lib.esc(42), "42");
});

test("noms et chemins affichés", () => {
  assert.equal(lib.baseName("/content/music/01-a.mp3"), "01-a.mp3");
  assert.equal(lib.baseName("/content/music/"), "music");
  assert.equal(lib.baseName(""), "");
  assert.equal(lib.relPath("/content"), "Tous les fichiers");
  assert.equal(lib.relPath("/content/music/comptines"), "music / comptines");
  assert.equal(lib.prettyName("/content/stories/02-chapitre_01.mp3"), "02 chapitre 01");
  assert.equal(lib.parentDir("/content/music/a.mp3"), "/content/music");
  assert.equal(lib.parentDir("/a.mp3"), "/content");
});

test("durées et tailles", () => {
  assert.equal(lib.fmtClock(83000), "1:23");
  assert.equal(lib.fmtClock(3723000), "1:02:03");
  assert.equal(lib.fmtClock(-5), "0:00");
  assert.equal(lib.fmtDuration(42), "42 s");
  assert.equal(lib.fmtDuration(420), "7 min");
  assert.equal(lib.fmtDuration(4500), "1 h 15");
  assert.equal(lib.fmtBytes(512), "512 o");
  assert.equal(lib.fmtBytes(1536), "1,5 Ko");
  assert.equal(lib.fmtBytes(32 * 1024 ** 3), "32 Go");
  assert.equal(lib.plural(1, "piste", "pistes"), "1 piste");
  assert.equal(lib.plural(3, "piste", "pistes"), "3 pistes");
});

test("fichiers audio reconnus comme par la boîte", () => {
  for (const name of ["a.mp3", "B.MP3", "c.m4a", "d.opus", "e.flac"]) assert.ok(lib.isAudioPath(name), name);
  for (const name of ["pochette.jpg", "mp3", "", undefined]) assert.ok(!lib.isAudioPath(name), String(name));
});

test("noms de dossiers sans accents ni caractères refusés par la boîte", () => {
  assert.equal(lib.safeFolderName("Été à la plage"), "Ete a la plage");
  assert.equal(lib.safeFolderName("a/b:c"), "a_b_c");
  assert.equal(lib.safeFolderName("Contes."), "Contes_");
  assert.equal(lib.safeFolderName("   "), "dossier");
});

test("nom réseau tiré du nom de la boîte", () => {
  assert.equal(lib.hostnameFromName("Chambre de Léo"), "chambre-de-leo");
  assert.equal(lib.hostnameFromName("  --Salon !! "), "salon");
  assert.equal(lib.hostnameFromName("x".repeat(40)), "x".repeat(32));
  assert.equal(lib.hostnameFromName("abc".repeat(10) + "-xyz"), "abcabcabcabcabcabcabcabcabcabc-x");
  assert.equal(lib.normalizeHostname("  TotoBox "), "totobox");
  assert.ok(lib.HOSTNAME_PATTERN.test("chambre-de-leo"));
  assert.ok(!lib.HOSTNAME_PATTERN.test("-salon"));
  assert.ok(!lib.HOSTNAME_PATTERN.test("x".repeat(33)));
});

test("déplacements permis", () => {
  assert.ok(lib.canMoveInto("/content/music/a.mp3", "/content/stories"));
  assert.ok(lib.canMoveInto("/content/music", "/content/musique"));
  assert.ok(!lib.canMoveInto("/content/music", "/content/music"));
  assert.ok(!lib.canMoveInto("/content/music", "/content/music/comptines"));
  const tree = [
    { path: "/content/music", type: "directory", children: [{ path: "/content/music/comptines", type: "directory", children: [] }, { path: "/content/music/a.mp3", type: "file" }] },
    { path: "/content/b.mp3", type: "file" },
  ];
  assert.deepEqual([...lib.allFolders(tree)], ["/content/music", "/content/music/comptines"]);
});

test("les cartes suivent un fichier ou un dossier renommé ou déplacé", () => {
  const list = [
    { uid: "A", mediaPath: "/content/music" },
    { uid: "B", mediaPath: "/content/music/comptines/01.mp3" },
    { uid: "C", mediaPath: "/content/musique" },
  ];
  assert.ok(lib.retargetMediaPaths(list, "/content/music", "/content/chansons"));
  assert.deepEqual(list.map((b) => b.mediaPath), ["/content/chansons", "/content/chansons/comptines/01.mp3", "/content/musique"]);
  assert.ok(!lib.retargetMediaPaths(list, "/content/autre", "/content/x"));
  assert.ok(!lib.retargetMediaPaths(list, "/content/chansons", "/content/chansons"));
  assert.ok(!lib.retargetMediaPaths(list, "/content/chansons", ""));
});

// En-tête ID3v2 de `size` octets de contenu (taille codée sur 4 x 7 bits).
function id3v2(size, flags = 0) {
  return [0x49, 0x44, 0x33, 4, 0, flags, (size >> 21) & 0x7f, (size >> 14) & 0x7f, (size >> 7) & 0x7f, size & 0x7f, ...new Array(size).fill(0)];
}

const AUDIO = [0xff, 0xfb, 0x90, 0x64, ...new Array(300).fill(0x55)];

test("tags ID3 retirés avant l'envoi", async () => {
  const id3v1 = [0x54, 0x41, 0x47, ...new Array(125).fill(0x20)];
  const file = new File([new Uint8Array([...id3v2(200), ...id3v2(20, 0x10), ...new Array(10).fill(0), ...AUDIO, ...id3v1])], "a.mp3", { type: "audio/mpeg" });
  const stripped = await lib.stripId3(file);
  assert.deepEqual([...new Uint8Array(await stripped.arrayBuffer())], AUDIO);
});

test("fichier sans tag ou non MP3 envoyé tel quel", async () => {
  const plain = new File([new Uint8Array(AUDIO)], "a.mp3");
  assert.equal(await lib.stripId3(plain), plain);
  const wav = new File([new Uint8Array([...id3v2(20), ...AUDIO])], "a.wav");
  assert.equal(await lib.stripId3(wav), wav);
  const tiny = new File([new Uint8Array([0x49, 0x44, 0x33])], "a.mp3");
  assert.equal(await lib.stripId3(tiny), tiny);
});

test("versions comparées nombre par nombre", () => {
  assert.ok(lib.compareVersions("1.0.10", "1.0.9") > 0);
  assert.ok(lib.compareVersions("v1.1.0", "1.0.99") > 0);
  assert.equal(lib.compareVersions("1.0.1", "1.0.1"), 0);
  assert.ok(lib.compareVersions("1.0.1", "2.0.0") < 0);
  assert.equal(lib.compareVersions("1.0", "1.0.0"), null);
  assert.equal(lib.compareVersions(undefined, "1.0.0"), null);
});

const HASH = "8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4";
const DOWNLOAD = "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/";

function release(block) {
  return {
    tag_name: "v1.1.0",
    html_url: "https://github.com/thomas-lamure/totobox/releases/tag/v1.1.0",
    body: `### Firmware\n\n- Nouveauté.\n\n### Installer\n\n- esptool…\n\n${block}`,
    assets: [
      { name: "totobox-1.1.0-firmware.bin", size: 1500000, browser_download_url: DOWNLOAD + "totobox-1.1.0-firmware.bin" },
      { name: "totobox-1.1.0-page.html.gz", size: 47000, browser_download_url: DOWNLOAD + "totobox-1.1.0-page.html.gz" },
    ],
  };
}

const BLOCK = `<!-- totobox-update {"version":"1.1.0","uiVersion":"8291aeae5293","firmware":{"name":"totobox-1.1.0-firmware.bin","size":1500000,"sha256":"${HASH}"},"page":{"name":"totobox-1.1.0-page.html.gz","size":47000,"sha256":"${HASH}"}} -->`;

test("la Release donne de quoi demander la mise à jour à la boîte", () => {
  const parsed = lib.parseRelease(release(BLOCK));
  assert.equal(parsed.version, "1.1.0");
  assert.equal(parsed.notes, "### Firmware\n\n- Nouveauté.");  // sans le mode d'emploi ni le bloc
  assert.deepEqual(JSON.parse(JSON.stringify(parsed.update)), {
    version: "1.1.0",
    uiVersion: "8291aeae5293",
    firmware: { url: DOWNLOAD + "totobox-1.1.0-firmware.bin", sha256: HASH, size: 1500000 },
    page: { url: DOWNLOAD + "totobox-1.1.0-page.html.gz", sha256: HASH, size: 47000 },
  });
});

test("Release sans mise à jour en ligne possible", () => {
  // Ancienne Release (sans bloc), fichier manquant ou de taille différente, bloc d'une autre version, bloc illisible.
  assert.equal(lib.parseRelease(release("")).update, null);
  const missing = release(BLOCK);
  missing.assets.pop();
  assert.equal(lib.parseRelease(missing).update, null);
  const resized = release(BLOCK);
  resized.assets[0].size = 1;
  assert.equal(lib.parseRelease(resized).update, null);
  assert.equal(lib.parseRelease(release(BLOCK.replace('"version":"1.1.0"', '"version":"1.0.9"'))).update, null);
  assert.equal(lib.parseRelease(release(BLOCK.replace(HASH, "abc"))).update, null);
  assert.equal(lib.parseRelease(release("<!-- totobox-update {pas du json} -->")).update, null);
  assert.equal(lib.parseRelease({ tag_name: "nightly" }), null);
  assert.equal(lib.parseRelease(null), null);
});

test("notes de version lisibles sans rendu Markdown", () => {
  assert.equal(lib.plainNotes("### Firmware\n\n- Carte `card_flow` **testée**.\n  - détail\n\n\n\n## Page"), "Firmware\n\n• Carte card_flow testée.\n  • détail\n\nPage");
  assert.equal(lib.plainNotes(undefined), "");
});

// ---------- Égalisation du volume des MP3 ----------

// Trame MPEG1 couche III à 128 kbit/s, 44,1 kHz (417 octets), global_gain de chaque granule = `gains`.
// Positions: 9 bits de main_data_begin, bits privés (5 en mono, 3 en stéréo), scfsi (4 par canal), puis
// granules de 59 bits où global_gain suit 21 bits.
function frame({ mono = false, crc = false, gains = null, tag = "" } = {}) {
  const bytes = new Uint8Array(417);
  bytes.set([0xff, crc ? 0xfa : 0xfb, 0x90, mono ? 0xc0 : 0x00]);
  const side = 4 + (crc ? 2 : 0);
  const channels = mono ? 1 : 2;
  let pos = side * 8 + 9 + (mono ? 5 : 3) + 4 * channels;
  const list = gains || new Array(2 * channels).fill(150);
  for (const gain of list) {
    lib.writeBits(bytes, pos + 21, 8, gain);
    pos += 59;
  }
  if (tag) bytes.set([...tag].map((c) => c.charCodeAt(0)), side + (mono ? 17 : 32));
  if (crc) {
    const value = lib.mp3Crc(bytes, 0, mono ? 17 : 32);
    bytes[4] = value >> 8;
    bytes[5] = value & 0xff;
  }
  return bytes;
}

function gainsOf(bytes, offset, { mono = false, crc = false } = {}) {
  const channels = mono ? 1 : 2;
  let pos = (offset + 4 + (crc ? 2 : 0)) * 8 + 9 + (mono ? 5 : 3) + 4 * channels;
  const out = [];
  for (let g = 0; g < 2 * channels; g++, pos += 59) out.push(lib.readBits(bytes, pos + 21, 8));
  return out;
}

function concat(...parts) {
  const out = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
  let offset = 0;
  for (const part of parts) {
    out.set(part, offset);
    offset += part.length;
  }
  return out;
}

test("en-tête de trame MP3", () => {
  const header = lib.mp3FrameHeader(frame(), 0);
  assert.equal(header.length, 417);
  assert.equal(header.mpeg1, true);
  assert.equal(header.mono, false);
  assert.equal(header.sideLength, 32);
  assert.equal(lib.mp3FrameHeader(new Uint8Array([0xff, 0xfd, 0x90, 0]), 0), null, "couche II refusée");
  assert.equal(lib.mp3FrameHeader(new Uint8Array([0xff, 0xfb, 0xf0, 0]), 0), null, "débit invalide");
  // MPEG2, 22,05 kHz, 32 kbit/s, mono: 72 × 32000 / 22050 = 104 octets.
  const mpeg2 = lib.mp3FrameHeader(new Uint8Array([0xff, 0xf3, 0x40, 0xc0]), 0);
  assert.equal(mpeg2.length, 104);
  assert.equal(mpeg2.sideLength, 9);
});

test("une fausse synchronisation n'est pas prise pour une trame", () => {
  const junk = new Uint8Array([1, 2, 0xff, 0xfb, 0x90, 0x00, 7, 8]);
  const bytes = concat(junk, frame(), frame());
  assert.equal(lib.firstMp3Frame(bytes), junk.length);
  assert.equal(lib.firstMp3Frame(new Uint8Array(100)), -1);
});

test("le gain de chaque granule est décalé, sans changer la taille", () => {
  const bytes = concat(frame({ gains: [150, 151, 152, 153] }), frame({ gains: [100, 101, 102, 103] }));
  const before = bytes.length;
  assert.deepEqual({ ...lib.applyMp3Gain(bytes, 3) }, { frames: 2, clamped: 0 });
  assert.equal(bytes.length, before);
  assert.deepEqual(gainsOf(bytes, 0), [153, 154, 155, 156]);
  assert.deepEqual(gainsOf(bytes, 417), [103, 104, 105, 106]);
  lib.applyMp3Gain(bytes, -3);
  assert.deepEqual(gainsOf(bytes, 0), [150, 151, 152, 153]);
});

test("gain en mono et borné à 0..255", () => {
  const bytes = concat(frame({ mono: true, gains: [254, 10] }), frame({ mono: true }));
  assert.equal(lib.applyMp3Gain(bytes, 4).clamped, 1);
  assert.deepEqual(gainsOf(bytes, 0, { mono: true }), [255, 14]);
});

test("tags ID3 et trame d'informations laissés intacts", () => {
  const id3 = new Uint8Array([0x49, 0x44, 0x33, 4, 0, 0, 0, 0, 0, 4, 0xff, 0xfb, 0x90, 0x00]);
  const info = frame({ tag: "Info" });
  const tail = new Uint8Array(128);
  tail.set([0x54, 0x41, 0x47]);
  const bytes = concat(id3, info, frame(), tail);
  const copy = bytes.slice();
  assert.equal(lib.applyMp3Gain(bytes, 2).frames, 1);
  assert.deepEqual(bytes.subarray(0, id3.length + 417), copy.subarray(0, id3.length + 417));
  assert.deepEqual(gainsOf(bytes, id3.length + 417), [152, 152, 152, 152]);
  assert.deepEqual(bytes.subarray(bytes.length - 128), tail);
});

test("le CRC des trames protégées est recalculé", () => {
  // Valeur de référence: CRC-16 (0x8005, départ 0xFFFF) d'une trame produite par lame -p, vérifiée par
  // ffmpeg -err_detect crccheck.
  const bytes = concat(frame({ crc: true }), frame({ crc: true }));
  lib.applyMp3Gain(bytes, 2);
  assert.deepEqual(gainsOf(bytes, 0, { crc: true }), [152, 152, 152, 152]);
  assert.equal((bytes[4] << 8) | bytes[5], lib.mp3Crc(bytes, 0, 32));
  const reference = Uint8Array.from(
    "fffa9264543c3d0df3b115430f7c400000000d20e000010d54550e2fec668000003480000004".match(/../g),
    (hex) => parseInt(hex, 16),
  );
  assert.equal(lib.mp3Crc(reference, 0, 32), 0x543c);
});

function sine(amplitude, seconds, rate = 48000, freq = 997) {
  const data = new Float32Array(seconds * rate);
  for (let n = 0; n < data.length; n++) data[n] = amplitude * Math.sin((2 * Math.PI * freq * n) / rate);
  return data;
}

test("niveau perçu conforme à BS.1770", () => {
  // Coefficients de référence de la norme à 48 kHz.
  const [shelf, highPass] = lib.kWeighting(48000);
  [1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585].forEach((value, i) =>
    assert.ok(Math.abs(shelf[i] - value) < 1e-9),
  );
  [1, -2, 1, -1.99004745483398, 0.99007225036621].forEach((value, i) => assert.ok(Math.abs(highPass[i] - value) < 1e-9));
  // EBU Tech 3341: sinus de 1 kHz à −23 dBFS sur les deux canaux -> −23 LUFS.
  const tone = sine(Math.pow(10, -23 / 20), 20, 48000, 1000);
  assert.ok(Math.abs(lib.gatedLoudness(lib.measureLevels([tone, tone], 48000).blocks) + 23) < 0.1);
  // Sinus de 997 Hz à 0 dBFS sur un seul canal: −3,01 LUFS, aussi à 44,1 kHz.
  const full = sine(1, 10, 44100);
  assert.ok(Math.abs(lib.gatedLoudness(lib.measureLevels([full], 44100).blocks) + 3.01) < 0.1);
  // Les silences ne comptent pas.
  const withSilence = new Float32Array(30 * 48000);
  withSilence.set(tone);
  assert.ok(Math.abs(lib.gatedLoudness(lib.measureLevels([withSilence, withSilence], 48000).blocks) + 23) < 0.1);
  assert.equal(lib.gatedLoudness(lib.measureLevels([new Float32Array(48000)], 48000).blocks), -Infinity);
});

test("échantillons qui écrêteraient selon la montée", () => {
  // Pic à 0,52 (−5,7 dBFS): dépasse 0 dBFS à partir de +6 dB (4 pas), pas à +4,5 dB.
  const levels = lib.measureLevels([sine(0.52, 2, 44100)], 44100);
  assert.equal(levels.samples, 88200);
  assert.equal(levels.over[2], 0);
  assert.ok(levels.over[3] > 0);
  const merged = lib.mergeLevels([levels, levels]);
  assert.equal(merged.samples, 2 * 88200);
  assert.equal(merged.over[3], 2 * levels.over[3]);
});

test("choix du nombre de pas", () => {
  const at = (lufs, over = []) => ({
    blocks: new Array(50).fill(Math.pow(10, (lufs + 0.691) / 10)),
    samples: 1e6,
    over: [0, 0, 0, 0, 0, 0, 0, 0].map((zero, k) => over[k] || zero),
  });
  // Contes trop faibles: montés.
  assert.equal(lib.mp3GainSteps(at(-22.2)).steps, 4);
  assert.equal(lib.mp3GainSteps(at(-21.3)).steps, 4);
  // … mais pas jusqu'à écrêter plus de 0,01 % des échantillons (Petit Poucet: 0,0247 % à +6 dB).
  assert.equal(lib.mp3GainSteps(at(-21.3, [3, 40, 93, 247])).steps, 3);
  assert.equal(lib.mp3GainSteps(at(-22.2, [9, 19, 50, 95])).steps, 4);
  // À moins d'un pas de la cible (Popi, −15 LUFS): inchangé.
  assert.equal(lib.mp3GainSteps(at(-15)).steps, 0);
  assert.equal(lib.mp3GainSteps(at(-17.4)).steps, 0);
  // Trop fort: baissé.
  assert.equal(lib.mp3GainSteps(at(-13)).steps, -2);
  // Montée bornée à 12 dB, silence laissé tel quel.
  assert.equal(lib.mp3GainSteps(at(-40)).steps, 8);
  assert.equal(lib.mp3GainSteps({ blocks: [], samples: 0, over: [] }).steps, 0);
});

test("gain affiché", () => {
  assert.equal(lib.fmtGain(4), "+6 dB");
  assert.equal(lib.fmtGain(3), "+4,5 dB");
  assert.equal(lib.fmtGain(-2), "−3 dB");
});

test("corbeille: éléments supprimés, pas les dossiers de rangement", () => {
  const library = [
    { path: "/content/music", type: "directory", children: [{ path: "/content/music/comptines", type: "directory", children: [] }] },
  ];
  const trash = [
    // /content/music/comptines/a.mp3 supprimé: rangé sous music/comptines, qui existent encore.
    { path: "/content/.trash/music", type: "directory", children: [
      { path: "/content/.trash/music/comptines", type: "directory", children: [
        { path: "/content/.trash/music/comptines/a.mp3", type: "file" },
      ] },
      { path: "/content/.trash/music/b.mp3", type: "file" },
    ] },
    // Dossier supprimé en entier: listé tel quel, avec son contenu.
    { path: "/content/.trash/contes", type: "directory", children: [{ path: "/content/.trash/contes/c.mp3", type: "file" }] },
    { path: "/content/.trash/d.mp3", type: "file" },
    // Dossiers vides (dernier fichier restauré par un ancien firmware): pas proposés.
    { path: "/content/.trash/vide", type: "directory", children: [{ path: "/content/.trash/vide/sous", type: "directory", children: [] }] },
  ];
  assert.deepEqual([...lib.trashItems(trash, library).map((item) => item.path)], [
    "/content/.trash/music/comptines/a.mp3",
    "/content/.trash/music/b.mp3",
    "/content/.trash/contes",
    "/content/.trash/d.mp3",
  ]);
  assert.equal(lib.originalPath("/content/.trash/music/b.mp3"), "/content/music/b.mp3");
  // Bibliothèque pas encore chargée: rien n'est pris pour un dossier de rangement.
  assert.equal(lib.trashItems(trash, null).length, 3);
});

test("téléchargement d'un dossier: même arborescence que sur la carte", () => {
  const library = [
    { path: "/content/music", type: "directory", children: [
      { path: "/content/music/comptines", type: "directory", children: [
        { path: "/content/music/comptines/a.mp3", type: "file", totalSize: 10 },
        { path: "/content/music/comptines/notes.txt", type: "file", totalSize: 1 },
      ] },
      { path: "/content/music/b.mp3", type: "file", totalSize: 20 },
    ] },
    { path: "/content/c.mp3", type: "file", totalSize: 30 },
    { path: "/content/.trash", type: "directory", children: [{ path: "/content/.trash/x.mp3", type: "file" }] },
  ];
  const plan = (folder) => JSON.parse(JSON.stringify(lib.folderDownloadPlan(library, folder)));
  assert.deepEqual(plan("/content/music"), [
    { path: "/content/music/comptines/a.mp3", dirs: ["music", "comptines"], name: "a.mp3", size: 10 },
    { path: "/content/music/b.mp3", dirs: ["music"], name: "b.mp3", size: 20 },
  ]);
  assert.deepEqual(plan("/content/music/comptines").map((item) => item.dirs.join("/")), ["comptines"]);
  // Racine: dossier "totobox", corbeille exclue.
  assert.deepEqual(plan("/content").map((item) => [...item.dirs, item.name].join("/")), [
    "totobox/music/comptines/a.mp3", "totobox/music/b.mp3", "totobox/c.mp3",
  ]);
  assert.deepEqual(plan("/content/absent"), []);
});

test("apparence des cartes: même contrôle que la boîte", () => {
  assert.equal(lib.cardEmoji(" 🦊 "), "🦊");
  assert.equal(lib.cardEmoji("🐻‍❄️"), "🐻‍❄️");
  assert.equal(lib.cardEmoji("AB"), "AB");
  assert.equal(lib.cardEmoji("<b>"), "");
  assert.equal(lib.cardEmoji("12345678901234567"), "");
  assert.equal(lib.cardEmoji(""), "");
  assert.deepEqual([...lib.CARD_COLORS.map(([value]) => value)], ["blue", "green", "amber", "red", "purple", "pink", "teal"]);
});

test("recherche de cartes sans accents ni casse", () => {
  const list = [
    { label: "Le Petit Prince", mediaPath: "/content/stories/petit-prince" },
    { label: "", mediaPath: "/content/music/comptines-de-noel" },
    { label: "Fée Clochette", mediaPath: "/content/x" },
  ];
  const labels = (query) => [...lib.filterCards(list, query)].map((item) => item.label || item.mediaPath);
  assert.equal(labels("").length, 3);
  assert.deepEqual(labels("prince"), ["Le Petit Prince"]);
  assert.deepEqual(labels("NOEL"), ["/content/music/comptines-de-noel"]);
  assert.deepEqual(labels("fee"), ["Fée Clochette"]);
  assert.deepEqual(labels("zzz"), []);
});

test("ordre des pistes: même tri que la boîte", () => {
  const sorted = ["b.mp3", "A.mp3", "_x.mp3", "a1.mp3", "10-c.mp3", "02-d.mp3"].sort(lib.compareTrackNames);
  assert.deepEqual([...sorted], ["02-d.mp3", "10-c.mp3", "_x.mp3", "A.mp3", "a1.mp3", "b.mp3"]);
});

test("ordre des pistes: numéro devant le nom", () => {
  assert.deepEqual({ ...lib.trackPrefix("03_Le loup.mp3") }, { digits: "03", sep: "_", rest: "Le loup", ext: ".mp3" });
  assert.deepEqual({ ...lib.trackPrefix("1 - Intro.mp3") }, { digits: "1", sep: " - ", rest: "Intro", ext: ".mp3" });
  assert.deepEqual({ ...lib.trackPrefix("07.mp3") }, { digits: "07", sep: "", rest: "", ext: ".mp3" });
  assert.equal(lib.trackPrefix("Chapitre 1.mp3"), null);
  assert.equal(lib.trackPrefix("2001 odyssee.mp3").digits, "2001");
  assert.equal(lib.trackPrefix("12abc.mp3"), null);
});

test("ordre des pistes: renommages", () => {
  const plain = (plan) => [...plan.renames].map(({ from, to }) => `${from} > ${to}`);
  // Sans numéro: « 01-».
  let plan = lib.trackOrderPlan(["Loup.mp3", "Agneau.mp3"]);
  assert.deepEqual(plain(plan), ["Loup.mp3 > 01-Loup.mp3", "Agneau.mp3 > 02-Agneau.mp3"]);
  assert.equal(plan.swap, false);
  // Numéros « 01_ » déjà là: même style, deux fichiers échangés.
  plan = lib.trackOrderPlan(["02_b.mp3", "01_a.mp3", "03_c.mp3"]);
  assert.deepEqual(plain(plan), ["02_b.mp3 > 01_b.mp3", "01_a.mp3 > 02_a.mp3"]);
  assert.equal(plan.swap, false);
  // Un nom visé encore porté par un autre fichier: deux temps.
  plan = lib.trackOrderPlan(["02.mp3", "01.mp3"]);
  assert.deepEqual(plain(plan), ["02.mp3 > 01.mp3", "01.mp3 > 02.mp3"]);
  assert.equal(plan.swap, true);
  // Style majoritaire repris, nouveau fichier numéroté pareil, largeur suivant le nombre de pistes.
  plan = lib.trackOrderPlan(["1 - a.mp3", "2 - b.mp3", "c.mp3"]);
  assert.deepEqual(plain(plan), ["1 - a.mp3 > 01 - a.mp3", "2 - b.mp3 > 02 - b.mp3", "c.mp3 > 03 - c.mp3"]);
  const many = Array.from({ length: 120 }, (_, i) => `t${i}.mp3`);
  assert.equal(lib.trackOrderPlan(many).renames[0].to, "001-t0.mp3");
  // Déjà dans l'ordre: rien à faire.
  assert.equal(lib.trackOrderPlan(["01-a.mp3", "02-b.mp3"]).renames.length, 0);
});

test("corbeille en dossiers: rangement, dossier supprimé, fichier", () => {
  const library = [{ path: "/content/stories", type: "directory", children: [] }];
  const trash = [
    { path: "/content/.trash/stories", type: "directory", children: [
      { path: "/content/.trash/stories/a.mp3", type: "file" },
      { path: "/content/.trash/stories/vieux", type: "directory", children: [
        { path: "/content/.trash/stories/vieux/1.mp3", type: "file" },
        { path: "/content/.trash/stories/vieux/2.mp3", type: "file" },
      ] },
      { path: "/content/.trash/stories/vide", type: "directory", children: [] },
    ] },
    { path: "/content/.trash/b.mp3", type: "file" },
  ];
  const kinds = (dir) => [...lib.trashListing(trash, library, dir)].map((e) => `${baseOf(e.path)}:${e.kind}:${e.tracks}`);
  const baseOf = (path) => path.split("/").pop();
  assert.deepEqual(kinds("/content/.trash"), ["stories:folder:3", "b.mp3:file:1"]);
  assert.deepEqual(kinds("/content/.trash/stories"), ["a.mp3:file:1", "vieux:deleted:2"]);
  assert.deepEqual(kinds("/content/.trash/stories/vieux"), ["1.mp3:file:1", "2.mp3:file:1"]);
  assert.equal(lib.trashListing(trash, library, "/content/.trash/absent"), null);
  // Un dossier de rangement est remplacé par son contenu; un dossier supprimé part entier.
  assert.deepEqual([...lib.restoreTargets(trash, library, ["/content/.trash/stories", "/content/.trash/b.mp3"])],
    ["/content/.trash/stories/a.mp3", "/content/.trash/stories/vieux", "/content/.trash/b.mp3"]);
  assert.deepEqual([...lib.restoreTargets(trash, library, ["/content/.trash/stories/vieux/2.mp3"])], ["/content/.trash/stories/vieux/2.mp3"]);
});

test("défilement automatique pendant un glissement", () => {
  // Zone qui défile de 100 à 600 px, bande de 56 px à chaque bord.
  assert.equal(lib.dragScrollSpeed(350, 100, 600), 0);
  assert.equal(lib.dragScrollSpeed(156, 100, 600), 0);
  assert.equal(lib.dragScrollSpeed(544, 100, 600), 0);
  assert.equal(lib.dragScrollSpeed(128, 100, 600), -480);
  assert.equal(lib.dragScrollSpeed(572, 100, 600), 480);
  assert.equal(lib.dragScrollSpeed(100, 100, 600), -960);
  // Au-delà du bord (pointeur sur l'en-tête ou le pied de la fenêtre): vitesse maximale.
  assert.equal(lib.dragScrollSpeed(20, 100, 600), -960);
  assert.equal(lib.dragScrollSpeed(900, 100, 600), 960);
  // Zone basse: bandes réduites au quart, le milieu reste immobile.
  assert.equal(lib.dragScrollSpeed(150, 100, 200), 0);
  assert.equal(lib.dragScrollSpeed(100, 100, 200), -960);
});
