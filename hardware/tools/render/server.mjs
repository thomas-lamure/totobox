#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Serveur local de l'atelier de rendu: sert ce dossier et enregistre dans out/ les images envoyées par la page
// (POST /save?name=...). N'écoute que sur localhost.
//
//   node hardware/tools/render/server.mjs [port]     (8090 par défaut), puis http://localhost:8090

import { createServer } from "node:http";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { basename, dirname, extname, join, normalize, sep } from "node:path";
import { fileURLToPath } from "node:url";

const DIR = dirname(fileURLToPath(import.meta.url));
const OUT = join(DIR, "out");
const PORT = Number(process.argv[2] || 8090);
const TYPES = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".gltf": "model/gltf+json",
  ".glb": "model/gltf-binary", ".jpg": "image/jpeg", ".png": "image/png",
};

createServer(async (request, response) => {
  const url = new URL(request.url, "http://localhost");
  try {
    if (request.method === "POST" && url.pathname === "/save") {
      const chunks = [];
      for await (const chunk of request) chunks.push(chunk);
      const name = basename(url.searchParams.get("name") || "rendu.jpg");
      await mkdir(OUT, { recursive: true });
      await writeFile(join(OUT, name), Buffer.concat(chunks));
      console.log(`out/${name}`);
      response.writeHead(200, { "Content-Type": "application/json" });
      return response.end(JSON.stringify({ ok: true, name }));
    }
    const path = normalize(join(DIR, url.pathname === "/" ? "index.html" : decodeURIComponent(url.pathname)));
    if (!path.startsWith(DIR + sep)) throw Object.assign(new Error("hors du dossier"), { code: "ENOENT" });
    const data = await readFile(path);
    response.writeHead(200, { "Content-Type": TYPES[extname(path)] || "application/octet-stream", "Cache-Control": "no-store" });
    response.end(data);
  } catch (error) {
    response.writeHead(error.code === "ENOENT" ? 404 : 500);
    response.end(String(error.message || error));
  }
}).listen(PORT, "127.0.0.1", () => console.log(`Atelier de rendu: http://localhost:${PORT}`));
