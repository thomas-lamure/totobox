#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Provisionnement d'une boîte sur la ligne de production.

Pour chaque boîte branchée en USB (firmware et image LittleFS déjà flashés avec pio run -t upload / uploadfs):
  1. lit l'adresse MAC (esptool read_mac) et en déduit le nom du point d'accès, TotoBox-XXXX;
  2. tire un mot de passe (même format que la boîte: xxxx-xxxx-xxxx, sans caractères ambigus);
  3. l'écrit dans la partition "mfg" (voir partitions.csv et src/factory_data.h), puis la relit pour vérifier;
  4. ajoute une ligne au fichier des étiquettes (MAC, nom, mot de passe, contenu du QR code Wi-Fi).

Une boîte déjà présente dans le fichier des étiquettes garde son mot de passe: relancer le script sur la même
boîte réécrit la même valeur, l'étiquette reste juste.

Le fichier des étiquettes contient les mots de passe: il ne doit pas être versionné (production/ est ignoré).

Usage:
  python3 scripts/provision.py --port /dev/cu.usbmodem1101
  python3 scripts/provision.py --mac 24:6f:28:ab:12:34 --image mfg.bin   # sans boîte: image seule
"""

import argparse
import csv
import datetime
import json
import os
import re
import secrets
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
PARTITION_NAME = "mfg"
DEFAULT_LABELS = PROJECT_DIR / "production" / "labels.csv"

# Mêmes règles que src/network_names.cpp et src/factory_data.h.
SSID_PREFIX = "TotoBox-"
PASSWORD_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"
MAGIC = b"TBXF"
FORMAT_VERSION = 1
HEADER_SIZE = 16

LABEL_FIELDS = ["date", "mac", "ssid", "password", "wifi_qr"]


def fail(message):
    print(f"Erreur: {message}", file=sys.stderr)
    sys.exit(1)


def read_partition(name):
    """Renvoie (offset, taille) de la partition dans partitions.csv."""
    for line in (PROJECT_DIR / "partitions.csv").read_text().splitlines():
        fields = [field.strip() for field in line.split("#")[0].split(",")]
        if len(fields) >= 5 and fields[0] == name:
            return int(fields[3], 0), int(fields[4], 0)
    fail(f"partition {name} absente de partitions.csv")


def generate_password():
    groups = ["".join(secrets.choice(PASSWORD_ALPHABET) for _ in range(4)) for _ in range(3)]
    return "-".join(groups)


def ssid_from_mac(mac):
    # Sur l'ESP32-S3 (2 adresses universelles), la MAC du point d'accès ne diffère de la MAC de base que par le
    # premier octet: les deux derniers sont ceux lus par esptool.
    return f"{SSID_PREFIX}{mac[4]:02X}{mac[5]:02X}"


def parse_mac(text):
    parts = re.split(r"[:-]", text.strip())
    if len(parts) != 6:
        fail(f"adresse MAC invalide: {text}")
    return bytes(int(part, 16) for part in parts)


def format_mac(mac):
    return ":".join(f"{byte:02x}" for byte in mac)


def build_image(password, size):
    payload = json.dumps({"apPassword": password}, separators=(",", ":")).encode()
    header = MAGIC + bytes([FORMAT_VERSION, 0, 0, 0]) + struct.pack("<II", len(payload), zlib.crc32(payload))
    image = header + payload
    if len(image) > size:
        fail("données d'usine trop grandes pour la partition")
    return image + b"\xff" * (size - len(image))


def wifi_qr(ssid, password):
    # Format reconnu par les appareils photo des téléphones pour rejoindre un réseau.
    def escape(value):
        return re.sub(r'([\\;,:"])', r"\\\1", value)

    return f"WIFI:T:WPA;S:{escape(ssid)};P:{escape(password)};;"


def esptool_command():
    """esptool installé dans ce Python, sinon celui fourni par PlatformIO."""
    try:
        import esptool  # noqa: F401

        return [sys.executable, "-m", "esptool"]
    except ImportError:
        pass
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    script = core_dir / "packages" / "tool-esptoolpy" / "esptool.py"
    if script.exists():
        # Le Python de PlatformIO a pyserial, nécessaire à esptool; celui du système souvent pas.
        for python in (core_dir / "penv" / "bin" / "python", core_dir / "penv" / "Scripts" / "python.exe"):
            if python.exists():
                return [str(python), str(script)]
        return [sys.executable, str(script)]
    fail("esptool introuvable: installe-le (pip install esptool) ou installe PlatformIO")


def run_esptool(port, *args):
    command = esptool_command() + ["--chip", "esp32s3", "--port", port, *args]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout + result.stderr, file=sys.stderr)
        fail(f"esptool {args[-1] if args else ''} a échoué")
    return result.stdout


def read_mac(port):
    output = run_esptool(port, "--after", "no_reset", "read_mac")
    match = re.search(r"^MAC:\s*([0-9a-f:]{17})", output, re.MULTILINE | re.IGNORECASE)
    if not match:
        fail("adresse MAC introuvable dans la sortie d'esptool")
    return parse_mac(match.group(1))


def load_labels(path):
    if not path.exists():
        return []
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def append_label(path, row):
    path.parent.mkdir(parents=True, exist_ok=True)
    new_file = not path.exists()
    with open(path, "a", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=LABEL_FIELDS)
        if new_file:
            writer.writeheader()
        writer.writerow(row)


def main():
    parser = argparse.ArgumentParser(description="Programme le mot de passe d'usine d'une TotoBox.")
    parser.add_argument("--port", help="port série de la boîte (ex. /dev/cu.usbmodem1101)")
    parser.add_argument("--labels", type=Path, default=DEFAULT_LABELS, help="fichier CSV des étiquettes")
    parser.add_argument("--mac", help="sans boîte: adresse MAC à utiliser (avec --image)")
    parser.add_argument("--image", type=Path, help="sans boîte: écrit l'image de la partition dans ce fichier")
    parser.add_argument("--new-password", action="store_true", help="tire un nouveau mot de passe même si la boîte est connue")
    args = parser.parse_args()

    if not args.port and not (args.mac and args.image):
        parser.error("--port, ou --mac avec --image")

    offset, size = read_partition(PARTITION_NAME)
    mac = parse_mac(args.mac) if args.mac else read_mac(args.port)
    ssid = ssid_from_mac(mac)

    labels = load_labels(args.labels)
    known = next((row for row in reversed(labels) if row["mac"] == format_mac(mac)), None)
    password = known["password"] if known and not args.new_password else generate_password()
    image = build_image(password, size)

    if args.image:
        args.image.write_bytes(image)
        print(f"Image écrite: {args.image} ({size} octets, à flasher en 0x{offset:x})")
    if args.port:
        with tempfile.TemporaryDirectory() as workdir:
            image_path = Path(workdir) / "mfg.bin"
            readback_path = Path(workdir) / "readback.bin"
            image_path.write_bytes(image)
            run_esptool(args.port, "--after", "no_reset", "write_flash", f"0x{offset:x}", str(image_path))
            run_esptool(args.port, "--after", "hard_reset", "read_flash", f"0x{offset:x}", f"0x{size:x}", str(readback_path))
            if readback_path.read_bytes() != image:
                fail("la partition relue ne correspond pas à l'image écrite")
        print("Partition d'usine écrite et vérifiée.")

    if not known or args.new_password:
        append_label(
            args.labels,
            {
                "date": datetime.datetime.now().isoformat(timespec="seconds"),
                "mac": format_mac(mac),
                "ssid": ssid,
                "password": password,
                "wifi_qr": wifi_qr(ssid, password),
            },
        )

    print()
    print(f"  MAC          {format_mac(mac)}")
    print(f"  Wi-Fi        {ssid}")
    print(f"  Mot de passe {password}{'  (déjà connu, inchangé)' if known and not args.new_password else ''}")
    print(f"  QR code      {wifi_qr(ssid, password)}")
    print(f"  Étiquettes   {args.labels}")


if __name__ == "__main__":
    main()
