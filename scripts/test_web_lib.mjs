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
  "normalizeHostname", "hostnameFromName", "stripId3", "retargetMediaPaths",
];
const source = readFileSync(new URL("../web/lib.js", import.meta.url), "utf8");
const lib = vm.runInNewContext(`${source}\n;({ ${NAMES.join(", ")} })`, {});

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
